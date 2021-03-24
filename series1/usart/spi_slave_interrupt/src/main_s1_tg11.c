/***************************************************************************//**
 * @file main_s1_tg11.c
 *
 * @brief This project demonstrates interrupt-driven use of the USART in
 * synchronous (SPI) slave mode.  The main loop transmits the specified number
 * of bytes and receives the byte that is shifted in by the master with each
 * outgoing one.
 *
 * The pins used in this example are defined below and are described in the
 * accompanying readme.txt file.
 *******************************************************************************
 * # License
 * <b>Copyright 2021 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 *******************************************************************************
 * # Evaluation Quality
 * This code has been minimally tested to ensure that it builds and is suitable
 * as a demonstration for evaluation purposes only. This code will be maintained
 * at the sole discretion of Silicon Labs.
 ******************************************************************************/

#include "em_device.h"
#include "em_chip.h"
#include "em_cmu.h"
#include "em_emu.h"
#include "em_gpio.h"
#include "em_usart.h"

// SPI ports, pins, and location(s)
#define US0MISO_PORT  gpioPortC
#define US0MISO_PIN   10
#define US0MISO_LOC   2
#define US0MOSI_PORT  gpioPortC
#define US0MOSI_PIN   11
#define US0MOSI_LOC   2
#define US0CLK_PORT   gpioPortA
#define US0CLK_PIN    12
#define US0CLK_LOC    5
#define US0CS_PORT    gpioPortC
#define US0CS_PIN     8
#define US0CS_LOC     2

/*
 * The TIMEPORT/TIMEPIN is not part of the SPI bus.  It shows when the
 * CPU responds to the master before, during, and after data transfer.
 * Use a logic analyzer to capture the activity on this pin along with
 * the bus traffic to understand the timing relationship between the
 * CPU and the SPI during interrupt-driven transfers.
 */
#define TIMEPORT      gpioPortC
#define TIMEPIN       14

// Size of the data buffers
#define BUFLEN  10

// Outgoing data
uint8_t outbuf[BUFLEN];

// Incoming data
uint8_t inbuf[BUFLEN];

// Position in the buffer
uint32_t bufpos;

/**************************************************************************//**
 * @brief
 *    GPIO initialization
 *****************************************************************************/
void initGPIO(void)
{
  CMU_ClockEnable(cmuClock_GPIO, true);

  // Configure MOSI pin as an input
  GPIO_PinModeSet(US0MOSI_PORT, US0MOSI_PIN, gpioModeInput, 0);

  // Configure MISO pin as an output
  GPIO_PinModeSet(US0MISO_PORT, US0MISO_PIN, gpioModePushPull, 0);

  // Configure CLK pin as an input
  GPIO_PinModeSet(US0CLK_PORT, US0CLK_PIN, gpioModeInput, 0);

  // Configure CS pin as an input pulled high
  GPIO_PinModeSet(US0CS_PORT, US0CS_PIN, gpioModeInput, 1);

  // Generate an interrupt on a CS pin high-to-low transition.
  GPIO_ExtIntConfig(US0CS_PORT, US0CS_PIN, US0CS_PIN, false, true, false);

  // Enable NVIC GPIO interrupt
#if (US0CS_PIN & 1)
  NVIC_ClearPendingIRQ(GPIO_ODD_IRQn);
  NVIC_EnableIRQ(GPIO_ODD_IRQn);
#else
  NVIC_ClearPendingIRQ(GPIO_EVEN_IRQn);
  NVIC_EnableIRQ(GPIO_EVEN_IRQn);
#endif

  // Enable the activity pin
  GPIO_PinModeSet(TIMEPORT, TIMEPIN, gpioModePushPull, 0);
}

/**************************************************************************//**
 * @brief
 *    USART0 initialization
 *****************************************************************************/
void initUSART0(void)
{
  CMU_ClockEnable(cmuClock_USART0, true);

  // Default asynchronous initializer (master mode, 1 Mbps, 8-bit data)
  USART_InitSync_TypeDef init = USART_INITSYNC_DEFAULT;

  init.master = false;  // Operate as a slave
  init.msbf = true;     // MSB first transmission for SPI compatibility
  init.enable = false;  // Do not enable yet

  // Configure but do not enable USART0
  USART_InitSync(USART0, &init);

  // Route USART0 RX, TX, and CLK to the specified pins.
  USART0->ROUTELOC0 = (US0MISO_LOC << _USART_ROUTELOC0_RXLOC_SHIFT) |
                      (US0MOSI_LOC << _USART_ROUTELOC0_TXLOC_SHIFT) |
                      (US0CLK_LOC << _USART_ROUTELOC0_CLKLOC_SHIFT) |
                      (US0CS_LOC << _USART_ROUTELOC0_CSLOC_SHIFT);

  // Enable USART interface pins
  USART0->ROUTEPEN = USART_ROUTEPEN_RXPEN | USART_ROUTEPEN_TXPEN |
                     USART_ROUTEPEN_CSPEN | USART_ROUTEPEN_CLKPEN;

  // Enable NVIC USART sources
  NVIC_ClearPendingIRQ(USART0_IRQn);
  NVIC_EnableIRQ(USART0_IRQn);
}

/**************************************************************************//**
 * @brief GPIO IRQHandler
 *****************************************************************************/
#if (US0CS_PIN & 1)
void GPIO_ODD_IRQHandler(void)
#else
void GPIO_EVEN_IRQHandler(void)
#endif
{
  // Clear the falling edge interrupt flag
  GPIO_IntClear(1 << US0CS_PIN);
}

/**************************************************************************//**
 * @brief
 *    USART0 interrupt handler
 *
 * @note
 *    EFM32TG11 has a single IRQ handler for each USART as opposed to the
 *    separate RX and TX IRQ handlers on other devices.  Although it would
 *    normally be prudent to query USART_IF for the source(s) of the
 *    interrupt(s), in SPI mode, RXDATAV denotes that the master and slave
 *    have exchanged one byte of data.
 *****************************************************************************/
void USART0_IRQHandler(void)
{
  // Drive the activity pin high to denote IRQ handler entry
  GPIO_PinOutSet(TIMEPORT, TIMEPIN);

  /*
   * Save the byte received concurrent with the transmission of the
   * last bit of the previous outgoing byte, and increment the buffer
   * position to the next byte.  Note that this read clears the
   * USART_IF_RXDATAV interrupt flag.
   */
  inbuf[bufpos++] = USART0->RXDATA;

  // If there are still bytes left to send, transmit the next one
  if (bufpos < BUFLEN)
    USART0->TXDATA = outbuf[bufpos];

  // Drive the activity pin low to denote IRQ handler exit
  GPIO_PinOutClear(TIMEPORT, TIMEPIN);
}

/**************************************************************************//**
 * @brief
 *    Main function
 *****************************************************************************/
int main(void)
{
  uint32_t i;

  // Chip errata
  CHIP_Init();

  // Initialize DCDC with kit specific parameters
  EMU_DCDCInit_TypeDef dcdcInit = EMU_DCDCINIT_DEFAULT;
  EMU_DCDCInit(&dcdcInit);

  // Run from the HFXO to maximize performance
  CMU_HFXOInit_TypeDef hfxoInit = CMU_HFXOINIT_DEFAULT;
  CMU_HFXOInit(&hfxoInit);
  CMU_ClockSelectSet(cmuClock_HF, cmuSelect_HFXO);

  /*
   * Set the optimal number of wait state for the selected HFCLK
   * frequency.  Note that the second parameter, which specifies the
   * voltage scaling level, is meaningless on Series 0 devices.
   */
  CMU_UpdateWaitStates(SystemCoreClockGet(), 0);

  // Initialize GPIO and USART0
  initGPIO();
  initUSART0();

  // Drive the activity pin high to show prep for data transfer
  GPIO_PinOutSet(TIMEPORT, TIMEPIN);

  while (1)
  {
    // Zero incoming buffer and populate outgoing data array
    for (i = 0; i < BUFLEN; i++)
    {
      inbuf[i] = 0;
      outbuf[i] = (uint8_t)i;
    }

    // Start at the beginning of the buffer
    bufpos = 0;

    // Enable the falling edge interrupt on the US0CS_PIN
    GPIO_IntClear(1 << US0CS_PIN);
    GPIO_IntEnable(1 << US0CS_PIN);

    // Drive the activity pin low when ready for CS assertion
    GPIO_PinOutClear(TIMEPORT, TIMEPIN);

    // Wait for falling edge on US0CS_PIN
    EMU_EnterEM1();

    /*
     * Drive the activity pin high on wake from EM1 immediately after
     * CS assertion
     */
    GPIO_PinOutSet(TIMEPORT, TIMEPIN);

    // Now enable the USART receiver and transmitter
    USART_Enable(USART0, usartEnable);

    // Enable receive data valid interrupt
    USART_IntEnable(USART0, USART_IEN_RXDATAV);

    /*
     * Transmit the first byte, then go into EM1.  The IRQ handler will
     * receive each incoming byte and transmit the next outgoing byte.
     */
    USART0->TXDATA = outbuf[bufpos];

    // Drive the activity pin low when ready to receive data
    GPIO_PinOutClear(TIMEPORT, TIMEPIN);

    // Wait in EM1 until all data is received
    while (bufpos < BUFLEN)
      EMU_EnterEM1();

    // Drive the activity pin high to show prep for next data transfer
    GPIO_PinOutSet(TIMEPORT, TIMEPIN);

    // Disable receive data interrupt
    USART_IntDisable(USART0, USART_IEN_RXDATAV);

    // Disable the falling edge interrupt on the US0CS_PIN
    GPIO_IntDisable(1 << US0CS_PIN);

    // Disable USART receiver and transmitter until next chip select
    USART_Enable(USART0, usartDisable);
  }
}
