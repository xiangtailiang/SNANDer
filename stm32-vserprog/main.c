/*
 * SNANDer STM32 Programmer Main
 * Custom USB CDC protocol for SPI NAND Flash programming
 * Based on stm32-vserprog
 *
 * Copyright (C) 2026
 * This program is free software under GPL v2
 */

#include <stdbool.h>

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/usb/usbd.h>
#ifdef STM32F0
#include <libopencm3/stm32/crs.h>
#include <libopencm3/stm32/syscfg.h>
#endif /* STM32F0 */

#include "board.h"
#include "usbcdc.h"
#include "spi.h"
#include "snander_protocol.h"

#define SPI_DEFAULT_CLOCK 9000000  /* 9MHz default */

#ifdef STM32F0
#define LED_ENABLE()  { \
  gpio_mode_setup(BOARD_PORT_LED, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, BOARD_PIN_LED); \
  gpio_set_output_options(BOARD_PORT_LED, GPIO_OTYPE_PP, GPIO_OSPEED_LOW, BOARD_PIN_LED); \
}
#define LED_DISABLE() gpio_mode_setup(BOARD_PORT_LED, GPIO_MODE_INPUT, GPIO_PUPD_NONE, BOARD_PIN_LED);
#else
#define LED_ENABLE()  gpio_set_mode(BOARD_PORT_LED, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, BOARD_PIN_LED)
#define LED_DISABLE() gpio_set_mode(BOARD_PORT_LED, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, BOARD_PIN_LED)
#endif /* STM32F0 */

#if BOARD_LED_HIGH_IS_BUSY
#define LED_BUSY()    gpio_set(BOARD_PORT_LED, BOARD_PIN_LED)
#define LED_IDLE()    gpio_clear(BOARD_PORT_LED, BOARD_PIN_LED)
#else
#define LED_BUSY()    gpio_clear(BOARD_PORT_LED, BOARD_PIN_LED)
#define LED_IDLE()    gpio_set(BOARD_PORT_LED, BOARD_PIN_LED)
#endif /* BOARD_LED_HIGH_IS_BUSY */

int main(void) {
  uint32_t i;

  rcc_periph_clock_enable(BOARD_RCC_LED);
  LED_ENABLE();
  LED_BUSY();

/* Setup clock accordingly */
#ifdef GD32F103
  /* GD32F103 specific clock setup - not supported for now */
  rcc_clock_setup_in_hse_8mhz_out_72mhz();
#else
#ifdef STM32F0
  rcc_clock_setup_in_hsi48_out_48mhz();
  rcc_periph_clock_enable(RCC_SYSCFG_COMP);
  SYSCFG_CFGR1 |= SYSCFG_CFGR1_PA11_PA12_RMP;
  rcc_periph_clock_enable(RCC_CRS);
  crs_autotrim_usb_enable();
  rcc_set_usbclk_source(RCC_HSI48);
#else
  rcc_clock_setup_in_hse_8mhz_out_72mhz();
#endif /* STM32F0 */
#endif /* GD32F103 */

  rcc_periph_clock_enable(RCC_GPIOA); /* For USB */

/* STM32F0x2 has internal pullup and does not need AFIO */
#ifndef STM32F0
  rcc_periph_clock_enable(BOARD_RCC_USB_PULLUP);
  rcc_periph_clock_enable(RCC_AFIO); /* For SPI */
#endif /* STM32F0 */

#if BOARD_USE_DEBUG_PINS_AS_GPIO
  gpio_primary_remap(AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_OFF, AFIO_MAPR_TIM2_REMAP_FULL_REMAP);
#endif

/* Setup GPIO to pull up the D+ high. (STM32F0x2 has internal pullup.) */
#ifndef STM32F0
  gpio_set_mode(BOARD_PORT_USB_PULLUP, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, BOARD_PIN_USB_PULLUP);
#if BOARD_USB_HIGH_IS_PULLUP
  gpio_set(BOARD_PORT_USB_PULLUP, BOARD_PIN_USB_PULLUP);
#else
  gpio_clear(BOARD_PORT_USB_PULLUP, BOARD_PIN_USB_PULLUP);
#endif /* BOARD_USB_HIGH_IS_PULLUP */
#endif /* STM32F0 */

  usbcdc_init();
#ifdef HAS_BOARD_INIT
  board_init();
#endif

  /* Initialize SPI */
  spi_setup(SPI_DEFAULT_CLOCK);
  
  /* Initialize protocol handler */
  protocol_init();

  /* The loop. */
  while (true) {
    /* Wait and blink if USB is not ready. */
    LED_IDLE();
    while (!usb_ready) {
      LED_DISABLE();
      for (i = 0; i < rcc_ahb_frequency / 150; i ++) {
        asm("nop");
      }
      LED_ENABLE();
      for (i = 0; i < rcc_ahb_frequency / 150; i ++) {
        asm("nop");
      }
    }

    /* Handle protocol commands */
    protocol_handle();
  }

  return 0;
}

/* Interrupts */

static void signal_fault(void) {
  uint32_t i;

  while (true) {
    LED_ENABLE();
    LED_BUSY();
    for (i = 0; i < 5000000; i ++) {
      asm("nop");
    }
    LED_DISABLE();
    for (i = 0; i < 5000000; i ++) {
      asm("nop");
    }
  }
}

void nmi_handler(void)
__attribute__ ((alias ("signal_fault")));

void hard_fault_handler(void)
__attribute__ ((alias ("signal_fault")));
