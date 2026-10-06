#include "device_optical_delay.h"
#include "xgpio.h"

static XGpio GpioDelay;

int OpticalDelay_Init(void)
{
    XGpio_Initialize(&GpioDelay, GPIO_DELAY_ID);
    XGpio_DiscreteWrite(&GpioDelay, 1, (u32)ODLY_0NS);
    return XST_SUCCESS;
}

void OpticalDelay_Set(Delay_value value)
{
    XGpio_DiscreteWrite(&GpioDelay, 1, (u32)value);
}

int OpticalDelay_SetNs(int ns)
{
    Delay_value val = ODLY_0NS;
    int known = 1;

    switch (ns) {
    case 0:   val = ODLY_0NS;   break;
    case 100: val = ODLY_100NS; break;
    case 270: val = ODLY_270NS; break;
    case 400: val = ODLY_400NS; break;
    default:  val = ODLY_0NS; known = 0; break;
    }
    OpticalDelay_Set(val);
    return known;
}
