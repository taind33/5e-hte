/*******************************************************************************
 * @file    board_app.h
 * @brief   Entry points for the 5E-HTE board-control app (AC/DC power, F1956
 *          attenuators, optical delay line, ADC7928 telemetry - all over the
 *          RS422/PC link using the VIP+kria binary protocol).
 *
 *          AD9361 bring-up itself is written directly in main.c (see its
 *          file header); this file only covers everything main.c calls
 *          AFTER that: AC/DC power, F1956 attenuators, optical delay,
 *          ADC7928, RS422/PC link.
 ******************************************************************************/
#ifndef BOARD_APP_H
#define BOARD_APP_H

#include <stdint.h>
#include "kria_helper.h"   /* BOARD_LOG_* levels */

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at boot, after AD9361 setup (main.c's Ad9361_Bringup()) has
 * finished -
 * see the SHARED-SPI-BUS NOTE in board_app.c for why that order matters.
 * Brings up the AC/DC UARTs, the F1956 attenuator SPI/GPIO, the optical
 * delay GPIO, the ADC7928 SPI, and the RS422/PC link (interrupt-driven or
 * polled - see board_config.h). Halts with an xil_printf() error message if
 * a required peripheral fails to initialize (ADC7928 failure is non-fatal -
 * see board_app.c).
 */
void BoardApp_Init(void);

/* Call FIRST in main(), before the AD9361 bring-up: brings up only the
 * RS422/PC link so boot log lines reach the app live (see BoardLog()). */
void BoardApp_InitLink(void);

/* Log a line to the console (xil_printf) AND to the app ("LOGT"); kept in a
 * small ring and replayed whenever the app (re)connects. level:
 * BOARD_LOG_INFO / BOARD_LOG_WARN / BOARD_LOG_ERROR (kria_helper.h).
 * Call only from main-loop context (it transmits on the PC UART). */
void BoardLog(uint8_t level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Called by the dispatcher on every host PING (reconnect detection). */
void BoardApp_OnHostPing(void);

/* Call once per pass of main()'s superloop. Non-blocking: services at most
 * one incoming RS422/PC byte and, on an idle pass, runs whichever periodic
 * task is due (ADC7928 telemetry ~1 s, RF "WAVE" spectrum ~300 ms, "RFST"
 * RX-config report ~3 s - all timed on the ARMv8 global timer).
 */
void BoardApp_PollOnce(void);

/* Polled-RX mode (no UART interrupt wired, UART_PC_HAS_INTR==0): moves any
 * bytes waiting in the RS422/PC UART's 16-byte hardware FIFO into the
 * firmware's software RX ring. Called from every blocking wait (UART TX
 * loops, AC/DC supply command pacing) so host commands are not lost while
 * the firmware is busy. No-op in interrupt mode and before the link is up. */
void BoardApp_ServiceRx(void);

/* usleep() replacement for long blocking waits: same minimum delay, but
 * keeps calling BoardApp_ServiceRx() every ~50 us while waiting. */
void BoardApp_DelayUs(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_APP_H */
