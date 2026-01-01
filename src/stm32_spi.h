/*
 * STM32 SPI Host Driver for SNANDer
 * 
 * Copyright (C) 2026
 */

#ifndef __STM32_SPI_H__
#define __STM32_SPI_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Set serial device path */
void stm32_set_serial_device(const char *dev);

/* Open serial port for STM32 device */
int stm32_spi_init(void);

/* Close serial port */
int stm32_spi_shutdown(void);

/* Send command to STM32 to perform SPI operations */
int stm32_spi_send_command(unsigned int writecnt, unsigned int readcnt, 
                          const unsigned char *writearr, unsigned char *readarr);

/* Control Chip Select pin */
int stm32_enable_pins(bool enable);

#endif /* __STM32_SPI_H__ */
