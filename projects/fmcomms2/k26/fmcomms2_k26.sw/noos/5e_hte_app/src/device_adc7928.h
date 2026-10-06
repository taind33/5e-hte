/*******************************************************************************
 * @file    device_adc7928.h
 * @brief   Driver for the Analog Devices AD7928 (8-channel, 12-bit SAR ADC)
 *          used to read the "điện áp kích nổ" (EED/detonator trigger
 *          voltage) monitor channel(s), over the fabric AXI Quad SPI core
 *          (XPAR_SPI_0, base 0x80000000) per your confirmation.
 *
 *          Timing/format sourced from the AD7908/AD7918/AD7928 datasheet:
 *          https://www.analog.com/media/en/technical-documentation/data-sheets/AD7908_7918_7928.pdf
 *            - CS active low, DIN/DOUT both clocked on SCLK falling edge
 *              (CPOL=0, CPHA=1), 20 MHz SCLK max.
 *            - 16 SCLK cycles per conversion cycle: the 12-bit control
 *              register is shifted in MSB-first on the first 12 falling
 *              edges (remaining 4 clocks of the 16 are don't-care for
 *              input), while DOUT concurrently shifts out 16 bits =
 *              1 leading zero + 3-bit channel address + 12-bit result.
 *            - **1-cycle result latency**: the 16-bit result clocked out
 *              during a given 16-SCLK transfer corresponds to the channel
 *              that was selected by the *previous* transfer's control
 *              word, not the one being written during the current
 *              transfer. ADC7928_ReadAll() below handles this internally
 *              (9 transfers to sample all 8 channels once, with the address
 *              bits in the returned word cross-checked against the
 *              expected channel as a sanity check).
 *          t_QUIET (>=50 ns between conversions) is satisfied structurally:
 *          each XSpi_Transfer() call deasserts and reasserts CS, and the
 *          C function-call/loop overhead between two calls vastly exceeds
 *          50 ns on this core.
 ******************************************************************************/
#ifndef DEVICE_ADC7928_H
#define DEVICE_ADC7928_H

#include <stdint.h>
#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

int ADC7928_Init(void);

/* Reads all 8 channels once (9 SPI transfers total) into out_codes[0..7]
 * (raw 12-bit codes, 0..4095, straight binary). Returns XST_SUCCESS, or
 * XST_FAILURE if any returned frame's address bits didn't match the
 * expected channel (bus glitch / wiring problem). On XST_FAILURE the codes
 * already read so far are still copied out (best-effort telemetry). */
int ADC7928_ReadAll(uint16_t out_codes[ADC7928_NUM_CHANNELS]);

/* Converts a raw code to volts using ADC7928_VREF_VOLTS /
 * ADC7928_RANGE_0_TO_2VREF / ADC7928_CH_DIVIDER_RATIO from board_config.h.
 * TODO(confirm): meaningless until the real front-end divider ratio for
 * this channel is known - see board_config.h. */
float ADC7928_CodeToVolts(int channel, uint16_t code);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_ADC7928_H */
