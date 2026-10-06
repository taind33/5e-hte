/*******************************************************************************
 * @file    stream_framer.c
 * @brief   See stream_framer.h for the full design note. Short version:
 *          DMA-capture 124 RX1 I/Q samples -> 124-point complex DFT ->
 *          dBFS -> CmdDispatcher_SendWaveDb() ("WAVD").
 ******************************************************************************/
#include "stream_framer.h"
#include "cmd_dispatcher.h"
#include "board_app.h"
#include "parameters.h"
#include "axi_dmac.h"
#include "xil_cache.h"
#include "xstatus.h"
#include "xtime_l.h"
#include <stdint.h>
#include <string.h>
#include <math.h>

#define RF_FFT_SIZE                     STREAM_FRAMER_FFT_SIZE   /* points (DFT), see stream_framer.h */
#define RF_RX_CHANNELS                  4U    /* I1,Q1,I2,Q2 interleaved - 2R2T mode, rx_adc_init.num_channels in main.c */
#define RF_BYTES_PER_SAMPLE_COMPONENT   2U    /* 16-bit signed per I or per Q, matches ADI's AD9361_ADC_DAC_BYTES_PER_SAMPLE */
#define RF_CAPTURE_BYTES                (RF_FFT_SIZE * RF_RX_CHANNELS * RF_BYTES_PER_SAMPLE_COMPONENT) /* 124*4*2 = 992 */
/* Buffer rounded up to whole 32-byte cache lines so the cache maintenance
 * around the DMA never touches a line shared with another variable. */
#define RF_CAPTURE_BUF_BYTES            ((RF_CAPTURE_BYTES + 31U) & ~31U)

#define RF_FFT_PI                       3.14159265358979323846f
#define RF_DMA_TIMEOUT_MS               500U
#define RF_FULL_SCALE                   2048.0f   /* 12-bit ADC, complex full-scale sine */
#define RF_DB_FLOOR                     (-200.0f)

/* DMA capture destination: plain static buffer (.bss, DDR), cache-line
 * aligned - see the cache maintenance in StreamFramer_CaptureAndSend(). */
static uint8_t s_rx_capture[RF_CAPTURE_BUF_BYTES] __attribute__((aligned(32)));

/* DFT twiddles: e^{-j 2 pi m / N} = cos - j sin, m = 0..N-1. */
static float s_cos[RF_FFT_SIZE];
static float s_sin[RF_FFT_SIZE];

static struct axi_dmac *s_rx_dmac = NULL;
static int              s_ready   = 0;
static stream_framer_stats_t s_stats;

const stream_framer_stats_t *StreamFramer_GetStats(void)
{
    return &s_stats;
}

int StreamFramer_Init(void)
{
    struct axi_dmac_init rx_dmac_init;
    int32_t status;

    memset(&rx_dmac_init, 0, sizeof(rx_dmac_init));
    rx_dmac_init.name       = "rx_dmac";
    rx_dmac_init.base       = RX_DMA_BASEADDR;
    rx_dmac_init.irq_option = IRQ_DISABLED; /* polled, same convention as every other bus driver in this project (SPI/ADC7928/attenuator) */

    status = axi_dmac_init(&s_rx_dmac, &rx_dmac_init);
    if (status != 0) {
        s_ready = 0;
        return XST_FAILURE;
    }
    {
        uint32_t m;
        for (m = 0U; m < RF_FFT_SIZE; m++) {
            float a = 2.0f * RF_FFT_PI * (float)m / (float)RF_FFT_SIZE;
            s_cos[m] = cosf(a);
            s_sin[m] = sinf(a);
        }
    }
    s_ready = 1;
    return XST_SUCCESS;
}

/* Direct N-point complex DFT (N = 124 is not a power of two):
 *   X[k] = sum_n x[n] * e^{-j 2 pi k n / N}
 * with a precomputed twiddle table; (k*n) mod N advanced incrementally. */
static void StreamFramer_Dft(const float *xr, const float *xi, float *mag)
{
    uint32_t k, n;

    for (k = 0U; k < RF_FFT_SIZE; k++) {
        float    sr = 0.0f, si = 0.0f;
        uint32_t idx = 0U;
        for (n = 0U; n < RF_FFT_SIZE; n++) {
            /* (xr + j xi) * (cos - j sin) */
            sr += xr[n] * s_cos[idx] + xi[n] * s_sin[idx];
            si += xi[n] * s_cos[idx] - xr[n] * s_sin[idx];
            idx += k;
            if (idx >= RF_FFT_SIZE) {
                idx -= RF_FFT_SIZE;
            }
        }
        mag[k] = sqrtf(sr * sr + si * si);
    }
}

int StreamFramer_CaptureAndSend(void)
{
    struct axi_dma_transfer rx_transfer;
    int32_t  status;
    float    re[RF_FFT_SIZE];
    float    im[RF_FFT_SIZE];
    float    mag[RF_FFT_SIZE];
    int16_t  db100[RF_FFT_SIZE];
    uint32_t i;
    const int16_t *samples;

    if (!s_ready || (s_rx_dmac == NULL)) {
        return XST_FAILURE;
    }

    /* Polled DMA (IRQ_DISABLED): axi_dmac_transfer_wait_completion() in
     * this no-OS version waits for IRQ_PENDING == SOT|EOT, but ONLY the ISR
     * (axi_dmac_dev_to_mem_isr) ever clears that write-1-to-clear register
     * - so from the 2nd capture on, the stale SOT|EOT of the previous
     * capture made the wait return immediately, before the new transfer
     * had necessarily landed. Clear it ourselves before every start. */
    (void)axi_dmac_write(s_rx_dmac, AXI_DMAC_REG_IRQ_PENDING,
                         AXI_DMAC_IRQ_SOT | AXI_DMAC_IRQ_EOT);

    /* Clean+invalidate BEFORE the DMA writes the buffer, so no dirty cache
     * line covering it (e.g. from the .bss zero-fill at boot) can be
     * evicted on top of fresh DMA data later. Invalidate again after. */
    Xil_DCacheFlushRange((INTPTR)s_rx_capture, RF_CAPTURE_BUF_BYTES);

    memset(&rx_transfer, 0, sizeof(rx_transfer));
    rx_transfer.size         = RF_CAPTURE_BYTES;
    rx_transfer.transfer_done = false;
    rx_transfer.cyclic       = NO;
    rx_transfer.src_addr     = 0U; /* device side - fixed by the AXI-ADC-DMA hardware, not a real address */
    rx_transfer.dest_addr    = (uint32_t)(uintptr_t)s_rx_capture;

    status = axi_dmac_transfer_start(s_rx_dmac, &rx_transfer);
    if (status != 0) {
        s_stats.dma_start_fail++;
        return XST_FAILURE;
    }
    /* Own completion poll instead of axi_dmac_transfer_wait_completion():
     * that one sleeps in blind 1 ms steps (no_os_mdelay(1)) between checks,
     * during which nothing drains the RS422/PC RX FIFO (polled mode, 16 bytes
     * = ~1.4 ms at 115200) - host commands arriving then were lost. Here the
     * RX FIFO keeps being serviced while waiting, with the same 500 ms
     * timeout. */
    {
        XTime t0, now;
        uint32_t pending = 0U;
        const XTime timeout = ((XTime)COUNTS_PER_SECOND / 1000U) * (XTime)RF_DMA_TIMEOUT_MS;

        XTime_GetTime(&t0);
        for (;;) {
            (void)axi_dmac_read(s_rx_dmac, AXI_DMAC_REG_IRQ_PENDING, &pending);
            if ((pending & (AXI_DMAC_IRQ_SOT | AXI_DMAC_IRQ_EOT)) == (AXI_DMAC_IRQ_SOT | AXI_DMAC_IRQ_EOT)) {
                break;
            }
            BoardApp_ServiceRx();
            XTime_GetTime(&now);
            if ((now - t0) > timeout) {
                s_stats.dma_timeout++;
                return XST_FAILURE;
            }
        }
        /* Acknowledge (write-1-to-clear) right away as well. */
        (void)axi_dmac_write(s_rx_dmac, AXI_DMAC_REG_IRQ_PENDING, pending);
    }

    /* DMA writes bypass the CPU cache - invalidate before reading, exactly
     * as ADI's own reference capture example does for this same core. */
    Xil_DCacheInvalidateRange((INTPTR)s_rx_capture, RF_CAPTURE_BUF_BYTES);

    /* Buffer layout: RF_RX_CHANNELS (4) interleaved 16-bit little-endian
     * signed words per sample-set = I1,Q1,I2,Q2 - RX1 is components 0/1. */
    samples = (const int16_t *)s_rx_capture;
    {
        uint16_t peak = 0U;
        for (i = 0U; i < RF_FFT_SIZE; i++) {
            int32_t vi = samples[i * RF_RX_CHANNELS + 0U]; /* RX1 I */
            int32_t vq = samples[i * RF_RX_CHANNELS + 1U]; /* RX1 Q */
            uint32_t ai = (uint32_t)((vi < 0) ? -vi : vi);
            uint32_t aq = (uint32_t)((vq < 0) ? -vq : vq);
            if (ai > peak) peak = (uint16_t)((ai > 0xFFFFU) ? 0xFFFFU : ai);
            if (aq > peak) peak = (uint16_t)((aq > 0xFFFFU) ? 0xFFFFU : aq);
            re[i] = (float)vi;
            im[i] = (float)vq;
        }
        s_stats.dma_ok++;
        s_stats.rx1_peak = peak;   /* 2047 = 12-bit full scale -> clipping */
    }

    StreamFramer_Dft(re, im, mag);

    /* |X_k| -> dBFS x100 (0 dBFS = full-scale complex sine, |X| = 2048*N). */
    for (i = 0U; i < RF_FFT_SIZE; i++) {
        float db = RF_DB_FLOOR;
        if (mag[i] > 0.0f) {
            db = 20.0f * log10f(mag[i] / (RF_FULL_SCALE * (float)RF_FFT_SIZE));
            if (db < RF_DB_FLOOR) {
                db = RF_DB_FLOOR;
            }
        }
        db100[i] = (int16_t)lroundf(db * 100.0f);   /* -20000..~+600 fits int16 */
    }

    CmdDispatcher_SendWaveDb(db100, RF_FFT_SIZE, (uint8_t)STREAM_FRAMER_WINDOW_RECT);
    return XST_SUCCESS;
}
