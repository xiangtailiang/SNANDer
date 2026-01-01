/*
 * SNANDer Protocol for STM32
 * Custom USB CDC protocol for SPI NAND Flash programming
 * 
 * Copyright (C) 2026
 * This program is free software under GPL v2
 */

#ifndef __SNANDER_PROTOCOL_H__
#define __SNANDER_PROTOCOL_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * Protocol Format:
 * Request:  [CMD:1B] [LEN:2B LE] [DATA:N bytes]
 * Response: [CMD:1B] [STATUS:1B] [LEN:2B LE] [DATA:N bytes]
 */

/* Command definitions */
#define CMD_NOP         0x00    /* No operation / handshake */
#define CMD_CFG_SPI     0x01    /* Configure SPI speed */
#define CMD_CS_CTRL     0x02    /* CS control */
#define CMD_SPI_WRITE   0x03    /* Write data (without CS change) */
#define CMD_SPI_READ    0x04    /* Read data (without CS change) */
#define CMD_RESET       0xFF    /* Reset device */

/* Status codes */
#define STATUS_OK       0x00
#define STATUS_ERR      0x01

/* CS control values */
#define CS_LOW          0x00
#define CS_HIGH         0x01

/* SPI speed values (72MHz / divider) */
#define SPI_SPEED_36M   0       /* /2 */
#define SPI_SPEED_18M   1       /* /4 */
#define SPI_SPEED_9M    2       /* /8 - default */
#define SPI_SPEED_4M5   3       /* /16 */
#define SPI_SPEED_2M25  4       /* /32 */
#define SPI_SPEED_1M125 5       /* /64 */

/* Protocol version */
#define PROTOCOL_VERSION 0x01

/* Maximum data length per packet */
#define MAX_PACKET_DATA 512

/* Initialize protocol handler */
void protocol_init(void);

/* Main protocol handler - call in main loop */
void protocol_handle(void);

#endif /* __SNANDER_PROTOCOL_H__ */
