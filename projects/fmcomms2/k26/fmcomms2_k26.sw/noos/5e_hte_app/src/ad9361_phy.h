/*******************************************************************************
 * @file    ad9361_phy.h
 * @brief   Shared AD9361 state between main.c (which owns the bring-up) and
 *          the rest of the firmware.
 *
 *          - g_ad9361_phy: the initialized phy handle (hook for future
 *            runtime control - frequency/gain changes wired into the
 *            RS422/PC command dispatcher).
 *          - g_rf_status: RX configuration read back from the chip ONCE at
 *            the end of Ad9361_Bringup(), BEFORE BoardApp_Init() reclaims
 *            the shared SPI0 controller for the F1956 attenuators. The
 *            RF status report ("RFST", cmd_dispatcher.c) is built from this
 *            cache only - nothing reads the AD9361 over SPI at runtime (see
 *            board_app.c SHARED-SPI-BUS NOTE).
 ******************************************************************************/
#ifndef AD9361_PHY_H
#define AD9361_PHY_H

#include <stdint.h>

struct ad9361_rf_phy;

/* Set by main.c once ad9361_init() succeeds during boot. NULL until then
 * (and permanently NULL if bring-up failed, in which case main() halts
 * before BoardApp_Init() ever runs - see main.c). */
extern struct ad9361_rf_phy *g_ad9361_phy;

typedef struct {
    uint8_t  valid;         /* 1 once Ad9361_Bringup() filled it in        */
    uint64_t rx_lo_hz;
    uint32_t rx_fs_hz;
    uint32_t rx_bw_hz;
    int32_t  rx1_gain_db;
    uint8_t  gain_mode;     /* enum rf_gain_ctrl_mode value                */
} ad9361_rf_status_t;

/* Defined in main.c. */
extern ad9361_rf_status_t g_rf_status;

/* AD9361 bring-up result (main.c). A failed bring-up no longer halts the
 * board: the RS422 link, AC/DC, attenuators and delay keep working, the RF
 * spectrum is disabled and the app is told why ("DIAG" + boot log). */
extern uint8_t g_ad9361_ok;          /* 1 = ad9361_init() + setup succeeded */
extern int32_t g_ad9361_init_status; /* ad9361_init() return code (0 = OK)  */

#endif /* AD9361_PHY_H */
