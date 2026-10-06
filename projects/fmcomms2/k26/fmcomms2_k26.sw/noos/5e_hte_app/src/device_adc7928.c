#include "device_adc7928.h"
#include "xspi.h"
#include "xstatus.h"
#include <string.h>

static XSpi SpiAdc;

/* AD7928 12-bit control register field positions (see file header). */
#define CR_WRITE   (1U << 11)
#define CR_SEQ     (0U << 10)
#define CR_PM_NORMAL (3U << 4)   /* PM1:PM0 = 11 = normal mode (no shutdown) */
#define CR_SHADOW  (0U << 3)
#define CR_RANGE   ((ADC7928_RANGE_0_TO_2VREF ? 0U : 1U) << 1) /* 0=0..2Vref, 1=0..Vref */
#define CR_CODING  (1U << 0)     /* 1 = straight binary                      */

static uint16_t BuildControlWord(uint8_t channel)
{
    uint16_t ctrl12 = (uint16_t)(CR_WRITE | CR_SEQ |
                                  (((uint16_t)channel & 0x7U) << 6) |
                                  CR_PM_NORMAL | CR_SHADOW | CR_RANGE | CR_CODING);
    /* control register occupies the first 12 of 16 SCLK cycles (MSB
     * first); the remaining 4 low-order bits are don't-care on input. */
    return (uint16_t)(ctrl12 << 4);
}

/* One 16-bit (2-byte, MSB-first) full-duplex transfer. Returns the 16-bit
 * word clocked out on DOUT while tx_word was clocked in on DIN. */
static int Transfer16(uint16_t tx_word, uint16_t *rx_word)
{
    uint8_t tx[2];
    uint8_t rx[2] = {0, 0};
    int status;

    tx[0] = (uint8_t)((tx_word >> 8) & 0xFFU);
    tx[1] = (uint8_t)(tx_word & 0xFFU);

    status = XSpi_Transfer(&SpiAdc, tx, rx, 2);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }
    *rx_word = (uint16_t)(((uint16_t)rx[0] << 8) | (uint16_t)rx[1]);
    return XST_SUCCESS;
}

int ADC7928_Init(void)
{
    XSpi_Config *cfg;
    int status;

    cfg = XSpi_LookupConfig(ADC7928_SPI_DEVICE_ID);
    if (cfg == NULL) {
        return XST_FAILURE;
    }
    status = XSpi_CfgInitialize(&SpiAdc, cfg, cfg->BaseAddress);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    /* CPOL=0 (clock idle low, do NOT set ACTIVE_LOW), CPHA=1 (sample on
     * trailing/falling edge -> set CLK_PHASE_1) - matches AD7928's "DIN/
     * DOUT clocked on SCLK falling edge". MSB-first is the core's default
     * (LSB_MSB_FIRST bit left clear), matching AD7928's MSB-first framing. */
    status = XSpi_SetOptions(&SpiAdc, XSP_MASTER_OPTION | XSP_CLK_PHASE_1_OPTION);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    status = XSpi_SetSlaveSelect(&SpiAdc, ADC7928_SLAVE_SELECT);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    status = XSpi_Start(&SpiAdc);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }
    /* Polled/blocking use of XSpi_Transfer(), no ISR registered. */
    XSpi_IntrGlobalDisable(&SpiAdc);

    return XST_SUCCESS;
}

int ADC7928_ReadAll(uint16_t out_codes[ADC7928_NUM_CHANNELS])
{
    uint16_t rx;
    uint8_t  ch;
    int      overall_status = XST_SUCCESS;

    memset(out_codes, 0, sizeof(uint16_t) * ADC7928_NUM_CHANNELS);

    /* Prime the pipeline: select channel 0, discard the (stale) result. */
    if (Transfer16(BuildControlWord(0U), &rx) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    for (ch = 0U; ch < ADC7928_NUM_CHANNELS; ch++) {
        uint8_t next_ch = (uint8_t)((ch + 1U) % ADC7928_NUM_CHANNELS);

        if (Transfer16(BuildControlWord(next_ch), &rx) != XST_SUCCESS) {
            overall_status = XST_FAILURE;
            break;
        }

        /* rx now holds the result for 'ch' (selected by the PREVIOUS
         * transfer), format: [15]=0 [14:12]=addr [11:0]=data. */
        {
            uint8_t  addr = (uint8_t)((rx >> 12) & 0x7U);
            uint16_t data = (uint16_t)(rx & 0x0FFFU);

            if (addr != ch) {
                /* Bus glitch / miswiring - keep the sample (best-effort
                 * telemetry) but flag it to the caller. */
                overall_status = XST_FAILURE;
            }
            out_codes[ch] = data;
        }
    }

    return overall_status;
}

float ADC7928_CodeToVolts(int channel, uint16_t code)
{
    static const float divider[ADC7928_NUM_CHANNELS] = ADC7928_CH_DIVIDER_RATIO;
    float fullscale = ADC7928_VREF_VOLTS * (ADC7928_RANGE_0_TO_2VREF ? 2.0f : 1.0f);
    float volts_at_pin = ((float)code / 4096.0f) * fullscale;
    float ratio = 1.0f;

    if ((channel >= 0) && (channel < (int)ADC7928_NUM_CHANNELS)) {
        ratio = divider[channel];
    }
    if (ratio == 0.0f) {
        ratio = 1.0f;
    }
    return volts_at_pin / ratio;
}
