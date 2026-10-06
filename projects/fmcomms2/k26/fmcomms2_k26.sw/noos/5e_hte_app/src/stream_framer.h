/*******************************************************************************
 * @file    stream_framer.h
 * @brief   RF spectrum of the AD9361 RX1 input: DMA-captures a burst of raw
 *          I/Q samples, computes an N-point complex DFT, converts each bin to
 *          dBFS and hands it to cmd_dispatcher.c to send as a "WAVD"
 *          msgid-39 frame every RF_WAVE_PERIOD_MS (board_app.c, 100 ms).
 *
 *          2026-10-05 (operator decision "phuong an B"): as many points as
 *          ONE kria message can carry. One payload is capped at
 *          KRIA_MAX_PAYLOAD_LEN = 255 bytes:
 *            "WAVD"(4) + n(1) + window(1) + n * int16 dBFS x100  -> n <= 124
 *          so N = 124 (was 32 points as uint32 linear magnitudes, "WAVE").
 *          124 is not a power of two -> direct DFT with precomputed
 *          twiddles: 124*124 complex MACs per frame, well under 1 ms on the
 *          Cortex-A53 at 10 frames/s. Bin spacing = Fs/124 (0.484 MHz at
 *          60 MSPS, was 1.875 MHz).
 *
 *          Per-bin value sent: 20*log10(|X_k| / (2048*N)) in 0.01 dB units
 *          (int16, -327.68..+327.67 dB). 0 dBFS = full-scale complex sine of
 *          the 12-bit ADC (2048, sign-extended to 16 bit by axi_adc). The
 *          app converts back to linear for its band-power (Parseval) sum.
 *
 *          Sample buffer layout: rx_adc_init in main.c configures
 *          num_channels=4 (I1,Q1,I2,Q2 interleaved, 16-bit signed each, 2R2T
 *          mode). RX1 = components 0 (I) / 1 (Q) of every 4-channel set.
 *
 *          NOT windowed (rectangular) - a Hann window would be a natural
 *          follow-up if peak sharpness matters later ("window" byte in the
 *          frame reports it, 0 = rectangular).
 ******************************************************************************/
#ifndef STREAM_FRAMER_H
#define STREAM_FRAMER_H

#include "xstatus.h"
#include <stdint.h>

/* Points per "WAVD" frame (also reported in "RFST"). <= 124 (wire budget). */
#define STREAM_FRAMER_FFT_SIZE      124U
#define STREAM_FRAMER_ADC_BITS      12U   /* AD9361 ADC, sign-extended to 16 bit by axi_adc */
#define STREAM_FRAMER_WINDOW_RECT   0U

#ifdef __cplusplus
extern "C" {
#endif

/* Sets up the AXI-DMAC RX channel + DFT twiddle table. Call once, AFTER a
 * successful Ad9361_Bringup() (BoardApp_Init() does). On failure
 * StreamFramer_CaptureAndSend() is a harmless no-op. */
int StreamFramer_Init(void);

/* One capture -> DFT -> "WAVD" frame. XST_FAILURE on DMA timeout etc.;
 * the caller just retries on a later tick (see board_app.c backoff). */
int StreamFramer_CaptureAndSend(void);

/* Health counters for the "DIAG" report (cmd_dispatcher.c). */
typedef struct {
    uint32_t dma_ok;          /* captures completed                       */
    uint32_t dma_timeout;     /* captures that never completed (500 ms)   */
    uint32_t dma_start_fail;  /* axi_dmac_transfer_start() errors         */
    uint16_t rx1_peak;        /* max |I|,|Q| of the last capture          */
} stream_framer_stats_t;
const stream_framer_stats_t *StreamFramer_GetStats(void);

#ifdef __cplusplus
}
#endif

#endif /* STREAM_FRAMER_H */
