/*******************************************************************************
 * @file    cmd_dispatcher.h
 * @brief   Binary command dispatcher for the RS422/PC link: feeds received
 *          bytes through the VIP framing decoder (vip_helper.c), unwraps
 *          the inner kria_message_t (kria_helper.h), dispatches msgid
 *          35/36/37/38/39/41 to the AC/DC/attenuator/optical-delay/generic/
 *          supply-readback handlers, sends device->host telemetry/streams (ADC7928
 *          msgid 40, "WAVE" and "RFST" on msgid 39), and sends back the
 *          VIP-wrapped ACK frames the
 *          Qt dashboard app already expects (payload64[0] = 1 success / 0
 *          fail on msgid 35..38 - see App/backend/dashboardmanager.cpp
 *          parseIncomingData()).
 *
 *          This REPLACES the ASCII Parser_ExecutePacket()/Parser_Process()
 *          from the reference src_att_pwr_odly/main.cpp, which spoke a
 *          plain-text "AC,220,50,1\n" protocol that the compiled Qt app
 *          does not (and never did) send - the app has only ever emitted
 *          binary kria_message_t frames.
 ******************************************************************************/
#ifndef CMD_DISPATCHER_H
#define CMD_DISPATCHER_H

#include "xuartns550.h"
#include "kria_helper.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* pc_uart: RS422 link to the Qt host - received bytes are fed in via
 * CmdDispatcher_HandleByte() and ACK/telemetry frames are transmitted back
 * out on this same UART. Called FIRST at boot (BoardApp_InitLink(), before
 * the AD9361 bring-up) so boot log lines reach the app live. The AC/DC
 * supply UARTs are owned by device_power.c (PowerLink_Init()). */
void CmdDispatcher_Init(XUartNs550 *pc_uart);

/* PC-link health counters for the "DIAG" report. */
void CmdDispatcher_GetLinkStats(uint32_t *rx_bytes, uint32_t *frames_ok, uint32_t *frames_bad);

/* "DIAG" health report and "LOGT" log line (msgid 39 streams). */
void CmdDispatcher_SendDiag(const board_diag_msg_t *d);
void CmdDispatcher_SendLog(uint8_t level, const char *text);

/* Tells the dispatcher which SPI/GPIO devices initialized. Commands for a
 * device that did not (ATT / optical delay) are NACKed instead of being
 * written to half-initialized hardware. ac_uart/dc_uart passed as NULL to
 * CmdDispatcher_Init() mean the same for the AC/DC supplies. */
void CmdDispatcher_SetDeviceReady(int att_ready, int odly_ready);

/* Feed one byte received from the PC/RS422 UART (called from the main
 * superloop after popping a byte out of the ISR-fed circular buffer - see
 * main.cpp). Runs the VIP decode state machine and, once a full frame is
 * validated, dispatches it and transmits any resulting reply. */
void CmdDispatcher_HandleByte(uint8_t byte);

/* Builds an adc7928_msg_t (msgid 40) from the given raw codes and sends it
 * VIP-wrapped to the host. Call this periodically from the main superloop
 * (e.g. every ~200 ms) - see main.cpp. */
void CmdDispatcher_SendAdcTelemetry(const uint16_t codes[8]);

/* RF spectrum frame "WAVD" (msgid 39, device->host): 4-byte magic, n,
 * window (0 = rectangular), then n little-endian int16 bins in dBFS x 0.01,
 * natural DFT bin order (not fftshifted). n is clamped to 124 (one payload).
 * See stream_framer.h. Replaces the old 32-point uint32 "WAVE" frame. */
void CmdDispatcher_SendWaveDb(const int16_t *db100, uint32_t count, uint8_t window);

/* Sends the cached AD9361 RX configuration (g_rf_status, filled once by
 * main.c's Ad9361_Bringup()) as an "RFST" frame (msgid 39, rf_status_msg_t)
 * so the app can label the "WAVE" spectrum with real frequencies. No SPI
 * access. No-op if bring-up never filled the cache. */
void CmdDispatcher_SendRfStatus(void);

/* Drives the AC/DC supply transactions (device_power.c PowerLink_*) and
 * reports every finished one to the host: a msgid 42 pwr_status_msg_t
 * (confirmed output state + measured V/I/F, or why it failed), followed by
 * the msgid 35/36 ACK when the transaction was a set command. Call once per
 * superloop pass (BoardApp_PollOnce()). */
void CmdDispatcher_PollPower(void);

#ifdef __cplusplus
}
#endif

#endif /* CMD_DISPATCHER_H */
