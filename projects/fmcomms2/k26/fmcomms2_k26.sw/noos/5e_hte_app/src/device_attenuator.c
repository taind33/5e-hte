#include "device_attenuator.h"
#include "board_config.h"
#include "sleep.h"   /* usleep() - Xilinx standalone BSP header, not <unistd.h> */
#include <stdint.h>

static XSpiPs SpiAtt;
static XGpio  GpioLe1, GpioLe2, GpioLe3;

/* MSB<->LSB bit reversal of one byte - see file header for why. */
static uint8_t ReverseBits(uint8_t x)
{
    x = (uint8_t)((((x >> 0) & 0x01) << 7) |
                   (((x >> 1) & 0x01) << 6) |
                   (((x >> 2) & 0x01) << 5) |
                   (((x >> 3) & 0x01) << 4) |
                   (((x >> 4) & 0x01) << 3) |
                   (((x >> 5) & 0x01) << 2) |
                   (((x >> 6) & 0x01) << 1) |
                   (((x >> 7) & 0x01) << 0));
    return x;
}

int Attenuator_Init(void)
{
    XSpiPs_Config *cfg;
    int status;

    cfg = XSpiPs_LookupConfig(ATT_SPI_DEVICE_ID);
    if (cfg == NULL) {
        return XST_FAILURE;
    }
    status = XSpiPs_CfgInitialize(&SpiAtt, cfg, cfg->BaseAddress);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }
    status = XSpiPs_SelfTest(&SpiAtt);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }
    status = XSpiPs_SetOptions(&SpiAtt, XSPIPS_MASTER_OPTION);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }
    /* 100 MHz / 64 ~= 1.5625 MHz, well under the F1956's 25 MHz max SCLK. */
    status = XSpiPs_SetClkPrescaler(&SpiAtt, XSPIPS_CLK_PRESCALE_64);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    XGpio_Initialize(&GpioLe1, GPIO_LE_ID_1);
    XGpio_Initialize(&GpioLe2, GPIO_LE_ID_2);
    XGpio_Initialize(&GpioLe3, GPIO_LE_ID_3);

    XGpio_DiscreteWrite(&GpioLe1, 1, 0);
    XGpio_DiscreteWrite(&GpioLe2, 1, 0);
    XGpio_DiscreteWrite(&GpioLe3, 1, 0);

    return XST_SUCCESS;
}

static XGpio *LeForChannel(int channel)
{
    if (channel == 2) return &GpioLe2;
    if (channel == 3) return &GpioLe3;
    return &GpioLe1; /* channel 1 or any invalid value defaults to CH1 */
}

void Attenuator_SetDb(int channel, float db_value)
{
    XGpio *le = LeForChannel(channel);
    uint8_t steps;
    uint8_t tx[2];

    if (db_value < 0.0f) db_value = 0.0f;
    if (db_value > ATT_MAX_DB) db_value = ATT_MAX_DB;

    steps = (uint8_t)(db_value * 4.0f);              /* 0.25 dB per LSB    */
    if (steps > ATT_MAX_STEPS) steps = ATT_MAX_STEPS; /* 7-bit, D7 fixed 0  */

    /* byte0 = attenuation word (D6..D0), byte1 = address word (A2..A0=0,
     * i.e. broadcast/"any" address - LE selects which physical part
     * latches, so the address word content does not need to discriminate
     * between the 3 parts on this board). Both bytes are bit-reversed so
     * that the XSpiPs's native MSB-first shift-out presents D0 (resp. A0)
     * first on MOSI, matching the F1956's LSB-first framing. */
    tx[0] = ReverseBits(steps & 0x7FU);
    tx[1] = ReverseBits(0x00U);

    XGpio_DiscreteWrite(le, 1, 0);           /* LE low (idle)              */
    usleep(1);                               /* >= data/LE setup (10 ns)   */
    (void)XSpiPs_PolledTransfer(&SpiAtt, tx, NULL, 2);
    usleep(1);                               /* >= data hold before latch  */
    XGpio_DiscreteWrite(le, 1, 1);           /* LE rising edge -> latch    */
    usleep(1);                               /* >= LE min pulse width      */
    XGpio_DiscreteWrite(le, 1, 0);           /* LE back to idle low        */
}
