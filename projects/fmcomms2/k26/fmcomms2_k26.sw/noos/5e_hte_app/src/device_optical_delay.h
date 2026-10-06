/*******************************************************************************
 * @file    device_optical_delay.h
 * @brief   2-bit GPIO (TTL) control of the optical delay line module
 *          (0 / 100 / 270 / 400 ns). Ported unchanged from the validated
 *          src_att_pwr_odly/main.cpp (HW_SetOpticalDelay).
 ******************************************************************************/
#ifndef DEVICE_OPTICAL_DELAY_H
#define DEVICE_OPTICAL_DELAY_H

#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

int  OpticalDelay_Init(void);
void OpticalDelay_Set(Delay_value value);
/* Maps a requested delay in ns (0/100/270/400) to Delay_value and applies
 * it; returns 1 if ns was a recognized value, 0 otherwise (falls back to
 * 0 ns on unrecognized input, matching the validated reference code). */
int  OpticalDelay_SetNs(int ns);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_OPTICAL_DELAY_H */
