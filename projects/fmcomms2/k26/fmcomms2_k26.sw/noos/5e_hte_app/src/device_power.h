/*******************************************************************************
 * @file    device_power.h
 * @brief   AC (ITECH IT-M7700, SCPI ASCII) and DC (Matsusada, "#0 ..." ASCII)
 *          bench power-supply control over RS232 (XUartNs550, polled), WITH
 *          readback: every set is confirmed by the supply itself, and the
 *          app can ask for the live measured values at any time.
 *
 *          The set command strings and their pacing are unchanged from the
 *          bench-verified src_att_pwr_odly/main.cpp sequence. What is new:
 *
 *          - Everything runs as a NON-BLOCKING transaction per supply
 *            (PowerLink_*): a list of steps "write command / wait" and
 *            "send query / wait for reply line". PowerLink_Poll() advances
 *            it from the main superloop, so the RF spectrum, ADC telemetry
 *            and PC commands keep running while a supply is answering.
 *          - One query in flight per supply at a time (IT-M7700 rule: the
 *            whole reply must be received before the next command).
 *          - Results (confirmed output state, measured V/I/F, SCPI error
 *            code, or "no reply") come back through PowerLink_Poll() and are
 *            reported to the app by cmd_dispatcher.c as msgid 42.
 *
 *          All supply command strings and timeouts: board_config.h.
 ******************************************************************************/
#ifndef DEVICE_POWER_H
#define DEVICE_POWER_H

#include <stdint.h>
#include "xuartns550.h"
#include "xuartns550_i.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Who to ACK (msgid 35/36 set commands) once the supply has confirmed. */
typedef struct {
    uint8_t  has_ack;
    uint8_t  des_id;
    uint8_t  inner_seq;
    uint8_t  msgid;
    uint16_t frame_seq;
} power_ack_ctx_t;

/* One finished transaction. Values only meaningful where `valid` says so
 * (PWR_VALID_* in kria_helper.h). */
typedef struct {
    uint8_t         dev;        /* PWR_DEV_AC / PWR_DEV_DC                   */
    uint8_t         reason;     /* PWR_REASON_*                              */
    uint8_t         result;     /* PWR_RESULT_*                              */
    uint8_t         output;     /* 0 OFF, 1 ON, 0xFF unknown                 */
    uint8_t         valid;      /* PWR_VALID_*                               */
    int16_t         dev_err;    /* SCPI error code (AC), 0 = none            */
    float           volt;       /* V                                         */
    float           curr;       /* A                                         */
    float           freq;       /* Hz                                        */
    power_ack_ctx_t ack;        /* copied from the start call                */
    uint8_t         has_raw_reply;
    char            raw_reply[96]; /* raw query reply (PWR_REASON_RAW)       */
} power_result_t;

/* Raw string send, "\r\n" NOT appended (kept for compatibility). */
void Device_UartSendString(XUartNs550 *instance, const char *str);

/* NULL uart = that supply's UART failed to initialize: every request for it
 * completes immediately with PWR_RESULT_NOT_READY. */
void PowerLink_Init(XUartNs550 *ac_uart, XUartNs550 *dc_uart);

/* Start a transaction. A new SET/RESET aborts whatever that supply was
 * doing (the aborted one is reported with PWR_RESULT_ABORTED); a READ while
 * a transaction is already running is merged into it (that transaction
 * reports measured values anyway), so repeated refresh presses never queue
 * up traffic on the supply. */
void PowerLink_StartRead(uint8_t dev);
/* AC: state ON -> full verified sequence, voltage clamped to
 * AC_UVP_VOLTAGE..AC_OVP_LIMIT, same "%d.%d" formatting as before.
 * state OFF -> "OUTP OFF" only (voltage/freq ignored). */
void PowerLink_StartAcSet(float voltage, float freq, int state, const power_ack_ctx_t *ack);
/* DC: state ON -> REN, ICN, SW1, VCN as before (% of full scale, clamped to
 * DC_MAX_VOLTAGE/DC_MAX_CURRENT). state OFF -> "#0 SW0" only. */
void PowerLink_StartDcSet(float voltage, float current, int state, const power_ack_ctx_t *ack);
/* "RESET": output OFF only (AC: no voltage/frequency is sent any more - the
 * old reset path pushed NORM:VOLT:AC 80.0 / NORM:FREQ 0.0 to the supply).
 * DC: ICN 0, SW0, VCN 0 as before. */
void PowerLink_StartOff(uint8_t dev);

/* Raw text command from the app terminal ("ac raw ..."/"dc raw ...").
 * Contains '?' -> query, reply line returned unparsed. Otherwise written;
 * AC writes are followed by SYST:ERR? + OUTP?. Never aborts a running
 * transaction: completes with PWR_RESULT_BUSY instead. */
void PowerLink_StartRaw(uint8_t dev, const char *text);

/* Drain both supply UART RX FIFOs into the line assemblers. Cheap, never
 * sends anything - safe to call from any wait loop (BoardApp_ServiceRx()). */
void PowerLink_ServiceRx(void);

/* Advance both state machines. Returns 1 and fills *out when a transaction
 * finished (call again until it returns 0). May transmit a command to a
 * supply (a few ms at 9600 baud, PC RX keeps being serviced meanwhile). */
int PowerLink_Poll(power_result_t *out);

/* 1 if the supply UART initialized. */
int PowerLink_IsReady(uint8_t dev);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_POWER_H */
