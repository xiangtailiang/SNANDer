/*
 * SNANDer Protocol for STM32
 * Custom USB CDC protocol for SPI NAND Flash programming
 * 
 * Copyright (C) 2026
 * This program is free software under GPL v2
 */

#include <string.h>
#include "snander_protocol.h"
#include "usbcdc.h"
#include "spi.h"
#include "board.h"

#include <libopencm3/stm32/gpio.h>

/* LED macros from board.h */
#if BOARD_LED_HIGH_IS_BUSY
#define LED_BUSY()    gpio_set(BOARD_PORT_LED, BOARD_PIN_LED)
#define LED_IDLE()    gpio_clear(BOARD_PORT_LED, BOARD_PIN_LED)
#else
#define LED_BUSY()    gpio_clear(BOARD_PORT_LED, BOARD_PIN_LED)
#define LED_IDLE()    gpio_set(BOARD_PORT_LED, BOARD_PIN_LED)
#endif

/* Internal buffer for SPI data */
static uint8_t data_buffer[MAX_PACKET_DATA];

/* Send response header */
static void send_response(uint8_t cmd, uint8_t status, uint16_t len) {
    usbcdc_putc(cmd);
    usbcdc_putc(status);
    usbcdc_putc(len & 0xFF);
    usbcdc_putc((len >> 8) & 0xFF);
}

/* Read 16-bit little-endian value from USB */
static uint16_t read_u16_le(void) {
    uint16_t val = usbcdc_getc();
    val |= (uint16_t)usbcdc_getc() << 8;
    return val;
}

void protocol_init(void) {
    /* Initialize SPI with default speed (9MHz) */
    spi_setup(9000000);
}

void protocol_handle(void) {
    uint8_t cmd;
    uint16_t len;
    uint8_t speed;
    uint8_t cs_state;
    uint32_t i;
    
    /* Wait for command byte */
    cmd = usbcdc_getc();
    
    LED_BUSY();
    
    switch (cmd) {
        case CMD_NOP:
            /* Handshake - return protocol version */
            len = read_u16_le();
            for (i = 0; i < len; i++) usbcdc_getc();
            
            send_response(CMD_NOP, STATUS_OK, 1);
            usbcdc_putc(PROTOCOL_VERSION);
            break;
            
        case CMD_CFG_SPI:
            /* Configure SPI speed */
            len = read_u16_le();
            if (len >= 1) {
                speed = usbcdc_getc();
                /* Consume any extra bytes */
                for (i = 1; i < len; i++) usbcdc_getc();
                
                /* Map speed index to Hz */
                uint32_t speed_hz;
                switch (speed) {
                    case SPI_SPEED_36M:   speed_hz = 36000000; break;
                    case SPI_SPEED_18M:   speed_hz = 18000000; break;
                    case SPI_SPEED_9M:    speed_hz = 9000000;  break;
                    case SPI_SPEED_4M5:   speed_hz = 4500000;  break;
                    case SPI_SPEED_2M25:  speed_hz = 2250000;  break;
                    case SPI_SPEED_1M125: speed_hz = 1125000;  break;
                    default:              speed_hz = 9000000;  break;
                }
                
                spi_setup(speed_hz);
                send_response(CMD_CFG_SPI, STATUS_OK, 0);
            } else {
                send_response(CMD_CFG_SPI, STATUS_ERR, 0);
            }
            break;
            
        case CMD_CS_CTRL:
            /* CS control */
            len = read_u16_le();
            if (len >= 1) {
                cs_state = usbcdc_getc();
                /* Consume any extra bytes */
                for (i = 1; i < len; i++) usbcdc_getc();
                
                if (cs_state == CS_LOW) {
                    SPI_SELECT();
                } else {
                    SPI_UNSELECT();
                }
                send_response(CMD_CS_CTRL, STATUS_OK, 0);
            } else {
                send_response(CMD_CS_CTRL, STATUS_ERR, 0);
            }
            break;
            
        case CMD_SPI_WRITE:
            /* Write data to SPI */
            len = read_u16_le();
            if (len > 0 && len <= MAX_PACKET_DATA) {
                /* Read data from USB */
                for (i = 0; i < len; i++) {
                    data_buffer[i] = usbcdc_getc();
                }
                /* Write to SPI */
                spi_write_bytes(data_buffer, len);
                send_response(CMD_SPI_WRITE, STATUS_OK, 0);
            } else if (len > MAX_PACKET_DATA) {
                /* Data too large - still need to consume it */
                for (i = 0; i < len; i++) usbcdc_getc();
                send_response(CMD_SPI_WRITE, STATUS_ERR, 0);
            } else {
                send_response(CMD_SPI_WRITE, STATUS_OK, 0);
            }
            break;
            
        case CMD_SPI_READ:
            /* Read data from SPI */
            len = read_u16_le();
            if (len > 0 && len <= MAX_PACKET_DATA) {
                /* Read from SPI */
                spi_read_bytes(data_buffer, len);
                /* Send response with data */
                send_response(CMD_SPI_READ, STATUS_OK, len);
                for (i = 0; i < len; i++) {
                    usbcdc_putc(data_buffer[i]);
                }
            } else if (len > MAX_PACKET_DATA) {
                send_response(CMD_SPI_READ, STATUS_ERR, 0);
            } else {
                send_response(CMD_SPI_READ, STATUS_OK, 0);
            }
            break;
            
        case CMD_RESET:
            /* Reset - just acknowledge */
            len = read_u16_le();
            /* Consume any data */
            for (i = 0; i < len; i++) usbcdc_getc();
            send_response(CMD_RESET, STATUS_OK, 0);
            /* Optionally trigger actual reset here */
            break;
            
        default:
            /* Unknown command - try to recover by reading length and discarding */
            len = read_u16_le();
            for (i = 0; i < len; i++) usbcdc_getc();
            send_response(cmd, STATUS_ERR, 0);
            break;
    }
    
    LED_IDLE();
}
