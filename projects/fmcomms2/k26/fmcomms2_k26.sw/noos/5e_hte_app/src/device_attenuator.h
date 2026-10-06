/*******************************************************************************
 * @file    device_attenuator.h
 * @brief   Driver for the 3x Renesas/IDT F1956 7-bit digital step
 *          attenuators (0..31.75 dB, 0.25 dB/step), shared PSU_SPI_0 bus,
 *          per-device latch-enable on GPIO (LE1/LE2/LE3).
 *
 *          Ported from the already hardware-validated
 *          src_att_pwr_odly/main.cpp (F1956_Set_Attenuation /
 *          HW_SetAttenuation / HW_ReverseBit) with the serial-frame timing
 *          made explicit against the F1956 datasheet:
 *            https://www.mouser.com/datasheet/2/698/REN_F1956_DST_20230421-2930778.pdf
 *          16-bit shift register = 8-bit attenuation word + 8-bit address
 *          word, shifted LSB-first overall (attenuation D0 first ... address
 *          A7 last). XSpiPs shifts MSB-first per byte, so each byte is bit-
 *          reversed before the transfer (HW_ReverseBit) to present D0/A0
 *          first on the wire - this is exactly what the validated reference
 *          code already does; this port keeps that behaviour unchanged.
 *          Max SCLK 25 MHz (PSU_SPI_0 is run at ~1.56 MHz here via
 *          XSPIPS_CLK_PRESCALE_64 on a 100 MHz input clock - comfortably
 *          inside spec). LE min pulse width 30 ns / data & LE setup-hold
 *          10 ns are covered by an explicit 1 us guard delay around the
 *          latch pulse (DelayAttLatch_us) - generous vs. the ns-level
 *          datasheet requirement, deliberately so no scope time was needed
 *          to "prove" a tighter number is safe on this board.
 ******************************************************************************/
#ifndef DEVICE_ATTENUATOR_H
#define DEVICE_ATTENUATOR_H

#include "xspips.h"
#include "xgpio.h"
#include "xstatus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Must be called once after GPIO_Init()/basic SPI bring-up. Initializes
 * PSU_SPI_0 as SPI master for the attenuator bus and drives all 3 LE lines
 * low (idle). Returns XST_SUCCESS/XST_FAILURE. */
int Attenuator_Init(void);

/* Sets channel 1/2/3 to db_value dB (clamped to 0..31.75, rounded down to
 * the nearest 0.25 dB step). Blocking (~a few us). */
void Attenuator_SetDb(int channel, float db_value);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_ATTENUATOR_H */
