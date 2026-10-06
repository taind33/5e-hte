/*******************************************************************************
 * @file    kria_helper.h
 * @brief   Inner application-message layer ("kria protocol"). This is a
 *          byte-for-byte port of App/backend/kria_helper.h (the Qt/C++ side)
 *          to freestanding embedded C, so that the exact same struct layouts
 *          and encode/decode state machine are used on both ends of the
 *          RS422 link. DO NOT change field order/sizes here without making
 *          the identical change on the Qt side (App/backend/kria_helper.h),
 *          otherwise the two ends will silently misinterpret payloads.
 *
 *          Wire layout notes:
 *            - all multi-byte struct fields are packed (see #pragma pack)
 *              and are written by the host compiler (x86, little-endian)
 *              and read by this target (Cortex-A53, also little-endian),
 *              so a straight memcpy of the packed struct is endianness-safe
 *              on this pairing. If a big-endian host is ever used instead,
 *              this assumption must be revisited.
 *
 *          msgid map (identical on both ends):
 *            35  fwi_msg_t        AC power set/ack   (isAC = "AC")
 *            36  fwi_msg_t        DC power set/ack   (isAC = "DC")
 *            37  timedelay_msg_t  Optical delay set/ack
 *            38  annuation_q_msg_t Attenuator set/ack, 0.25 dB resolution
 *                                 (7-byte payload). The old 6-byte
 *                                 annuation_msg_t (whole dB) is still
 *                                 ACCEPTED by the firmware for backward
 *                                 compatibility - see HandleAtt().
 *            39  cmd_msg_t        Generic/diag command (host->device) and
 *                                 device->host streams identified by a
 *                                 4-byte magic: "WAVE" (RF FFT spectrum),
 *                                 "RFST" (rf_status_msg_t, AD9361 RX config),
 *                                 "DIAG" (board_diag_msg_t, health counters)
 *                                 and "LOGT" (board log line, text)
 *            40  adc7928_msg_t    ADC7928 8-channel telemetry (device->host)
 *            41  pwr_query_msg_t  host->device: read one bench supply now
 *                                 ("refresh" button / F5 in the app)
 *            43  pwr_raw_msg_t    host->device: send one raw text command to a
 *                                 supply ("ac raw ..." / "dc raw ..." in the
 *                                 app terminal), reply returned in 44
 *            44  pwr_raw_reply_msg_t device->host: the supply's reply line
 *            42  pwr_status_msg_t device->host: result of a supply
 *                                 transaction (refresh, set-confirmation or
 *                                 RESET): confirmed output state + measured
 *                                 V/I/F read back from the supply itself
 ******************************************************************************/
#ifndef KRIA_HELPER_H
#define KRIA_HELPER_H

#include <stdint.h>
#include <string.h>

#define KRIA_MAX_PAYLOAD_LEN 255U

#pragma pack(push, 1)

/* LEGACY attenuator payload (6 bytes) - whole dB only. Kept so a Qt app
 * built before the 0.25 dB change still works against this firmware
 * (HandleAtt() tells the two apart by payload length). New senders must use
 * annuation_q_msg_t below. */
typedef struct __annuation_msg {
    char    msg[4];         /* "ATT" + NUL                                  */
    uint8_t att_number;     /* 1, 2 or 3                                    */
    uint8_t value;          /* whole dB, 0..31                              */
} annuation_msg_t;

/* Attenuator payload, 0.25 dB resolution (7 bytes). F1956 is 7-bit,
 * 0.25 dB/step, 0..31.75 dB (board_config.h ATT_STEP_DB/ATT_MAX_STEPS), so
 * the wire value is the step count directly: dB = value_qdb * 0.25,
 * valid 0..127. A different LENGTH than the legacy struct on purpose: an
 * older firmware that only knows the 6-byte struct rejects this one with a
 * NACK (length check in HandleAtt()) instead of misreading the step count
 * as whole dB and setting 4x the requested attenuation. */
typedef struct __annuation_q_msg {
    char     msg[4];        /* "ATQ" + NUL                                  */
    uint8_t  att_number;    /* 1, 2 or 3                                    */
    uint16_t value_qdb;     /* 0.25 dB steps, 0..127 (= 0..31.75 dB)         */
} annuation_q_msg_t;

typedef struct __timedelay_msg {
    char     msg[4];        /* "DEL" + NUL                                  */
    uint16_t value;         /* delay in ns: 0 / 100 / 270 / 400              */
} timedelay_msg_t;

typedef struct __fwi_msg {
    char     msg[4];        /* "FWI" + NUL                                  */
    char     isAC[3];       /* "AC" or "DC" + NUL                            */
    uint16_t voltage;       /* volts, integer                                */
    uint16_t frequency;     /* AC: integer Hz.
                                DC: reused field carries current, fixed-point
                                x100 (centiamps): 2.35 A -> 235, decoded by
                                dividing by 100.0f in HandleFwi(). Keep in
                                sync with setDcPower() in the Qt app.        */
    uint8_t  state;         /* 0 = OFF, 1 = ON                               */
} fwi_msg_t;

typedef struct __cmd_msg {
    char    msg[64];        /* free-form command name, NUL-terminated       */
    uint8_t value[4];       /* up to 3 numeric args, 0xFF terminated         */
} cmd_msg_t;

/* ADC7928 8-channel telemetry (device -> host), msgid 40.
 * raw_code[i] = 12-bit conversion result (0..4095) for physical channel i,
 * straight-binary coding (CODING=1), input range 0..2*VREF
 * (ADC7928_RANGE_0_TO_2VREF=1 in board_config.h, see device_adc7928.c).
 * Voltage scaling (resistor divider on the "kich no" analog front-end) is
 * NOT known yet - see board_config.h ADC7928_CH_DIVIDER_RATIO TODO.
 */
typedef struct __adc7928_msg {
    char     msg[4];        /* "ADC" + NUL                                  */
    uint16_t raw_code[8];   /* 12-bit codes, one per channel, 0 if unread    */
} adc7928_msg_t;

/* AD9361 RX configuration report (device -> host), msgid 39 with 4-byte
 * magic "RFST" (no NUL - same convention as the "WAVE" stream). Values are
 * the ones read back from the chip ONCE at the end of Ad9361_Bringup() in
 * main.c (cached in g_rf_status, ad9361_phy.h) - nothing here touches the
 * AD9361 over SPI at runtime (SPI0 is shared with the F1956 attenuators,
 * see board_app.c SHARED-SPI-BUS NOTE). The Qt app uses rx_fs_hz/fft_size
 * to put a real frequency axis under the "WAVE" spectrum and rx_lo_hz to
 * show which RF frequency the receiver is actually tuned to. */
typedef struct __rf_status_msg {
    char     msg[4];        /* "RFST"                                       */
    uint64_t rx_lo_hz;      /* RX LO (centre of the displayed spectrum)     */
    uint32_t rx_fs_hz;      /* RX sample rate at the DMA interface          */
    uint32_t rx_bw_hz;      /* RX analog RF bandwidth                       */
    int32_t  rx1_gain_db;   /* RX1 gain read back from the chip             */
    uint8_t  gain_mode;     /* enum rf_gain_ctrl_mode: 0=MGC 1=fast AGC
                               2=slow AGC 3=hybrid AGC                       */
    uint8_t  fft_size;      /* points per "WAVE" frame (32)                  */
    uint8_t  adc_bits;      /* AD9361 ADC resolution (12)                    */
    uint8_t  window;        /* 0 = rectangular (no window)                   */
} rf_status_msg_t;

/* Board health report (device -> host), msgid 39 with 4-byte magic "DIAG",
 * every ~3 s and right after the host (re)connects. Lets the app say WHY
 * there is no spectrum (AD9361 init failed / RX DMA never completes / ADC
 * clipping...) without a JTAG/UART console. */
typedef struct __board_diag_msg {
    char     msg[4];          /* "DIAG"                                     */
    uint8_t  version;         /* 1                                          */
    uint8_t  ad9361_ok;       /* 1 = ad9361_init() + setup succeeded         */
    uint8_t  rf_stream_ok;    /* 1 = RX DMA (stream_framer) initialized      */
    uint8_t  adc7928_ok;
    uint8_t  att_ok;
    uint8_t  odly_ok;
    uint8_t  ac_uart_ok;
    uint8_t  dc_uart_ok;
    int32_t  ad9361_status;   /* ad9361_init() return code, 0 = OK          */
    uint32_t uptime_s;
    uint32_t dma_ok;          /* RX captures completed                      */
    uint32_t dma_timeout;     /* RX captures that never completed (500 ms)  */
    uint32_t dma_start_fail;  /* axi_dmac_transfer_start() errors           */
    uint16_t rx1_peak;        /* max |I|,|Q| of the last capture; 12-bit ADC
                                 sign-extended -> 2047 = full scale (clip)  */
    uint16_t reserved;
    uint32_t pc_rx_bytes;     /* bytes received from the PC (RS422)         */
    uint32_t pc_frames_ok;    /* valid VIP frames received from the PC      */
    uint32_t pc_frames_bad;   /* VIP frames dropped (CRC/header errors)     */
    uint32_t pc_rx_dropped;   /* bytes lost because the RX ring was full    */
    char     build[24];       /* firmware build time, __DATE__ " " __TIME__ */
} board_diag_msg_t;

/* "LOGT" (msgid 39): 4-byte magic, 1 byte level, then the text (no NUL). */
#define BOARD_LOG_INFO          0U
#define BOARD_LOG_WARN          1U
#define BOARD_LOG_ERROR         2U
#define BOARD_LOG_MAX_TEXT      200U

/* Bench-supply identifiers used by msgid 41/42. */
#define PWR_DEV_AC              1U
#define PWR_DEV_DC              2U

/* pwr_status_msg_t.reason */
#define PWR_REASON_READ         0U  /* msgid 41 refresh request            */
#define PWR_REASON_SET          1U  /* confirmation of an msgid 35/36 set  */
#define PWR_REASON_RESET        2U  /* confirmation of a "RESET" (msgid 39) */
#define PWR_REASON_RAW          3U  /* raw text command (msgid 43 -> 44)   */

/* pwr_status_msg_t.result */
#define PWR_RESULT_OK           0U  /* supply answered; for SET/RESET also no
                                       SCPI error and output == requested    */
#define PWR_RESULT_NO_REPLY     1U  /* a query timed out (cable/baud/power)  */
#define PWR_RESULT_DEV_ERROR    2U  /* SYST:ERR? reported an error (dev_err) */
#define PWR_RESULT_OUT_MISMATCH 3U  /* OUTP? differs from requested ON/OFF   */
#define PWR_RESULT_NOT_READY    4U  /* supply UART failed to initialize      */
#define PWR_RESULT_ABORTED      5U  /* superseded by a newer command         */
#define PWR_RESULT_BAD_REPLY    6U  /* reply could not be parsed             */
#define PWR_RESULT_BUSY         7U  /* supply busy with another transaction  */

/* pwr_status_msg_t.valid bits */
#define PWR_VALID_VOLT          0x01U
#define PWR_VALID_CURR          0x02U
#define PWR_VALID_FREQ          0x04U
#define PWR_VALID_OUTPUT        0x08U
#define PWR_VALID_ERR           0x10U

/* msgid 41, host -> device: read one supply now. */
typedef struct __pwr_query_msg {
    char    msg[4];         /* "PWQ" + NUL                                  */
    uint8_t dev;            /* PWR_DEV_AC / PWR_DEV_DC                       */
    uint8_t flags;          /* reserved, 0                                   */
} pwr_query_msg_t;

/* msgid 42, device -> host. Fixed-point integers (not float) so both ends
 * agree on the bytes without relying on float layout. */
typedef struct __pwr_status_msg {
    char     msg[4];        /* "PWS" + NUL                                  */
    uint8_t  dev;           /* PWR_DEV_AC / PWR_DEV_DC                       */
    uint8_t  reason;        /* PWR_REASON_*                                  */
    uint8_t  result;        /* PWR_RESULT_*                                  */
    uint8_t  output;        /* 0 = OFF, 1 = ON, 0xFF = unknown               */
    uint8_t  valid;         /* PWR_VALID_* bits: which fields below are real */
    uint8_t  req_seq;       /* kria seq of the triggering command (SET), 0   */
    int16_t  dev_err;       /* SCPI error code from SYST:ERR? (AC), 0 = none */
    int32_t  volt_mv;       /* measured voltage, mV                          */
    int32_t  curr_ma;       /* measured current, mA                          */
    int32_t  freq_mhz;      /* measured frequency, mHz (AC only)             */
} pwr_status_msg_t;

/* msgid 43, host -> device: raw text command for one supply (no "\r\n").
 * A command containing '?' is a query: the reply line comes back in 44.
 * Otherwise it is written; for AC the firmware then asks SYST:ERR? and
 * OUTP? so the app learns whether it was accepted and the output state. */
#define PWR_RAW_TEXT_MAX        64U
typedef struct __pwr_raw_msg {
    char    msg[4];                     /* "RAW" + NUL                     */
    uint8_t dev;                        /* PWR_DEV_AC / PWR_DEV_DC          */
    uint8_t len;                        /* text length (<= 63)              */
    char    text[PWR_RAW_TEXT_MAX];     /* NUL-terminated                   */
} pwr_raw_msg_t;

/* msgid 44, device -> host. */
#define PWR_RAW_REPLY_MAX       96U
typedef struct __pwr_raw_reply_msg {
    char    msg[4];                     /* "RAWR"                          */
    uint8_t dev;
    uint8_t result;                     /* PWR_RESULT_*                    */
    uint8_t output;                     /* 0/1, 0xFF unknown (AC writes)   */
    uint8_t valid;                      /* PWR_VALID_OUTPUT / PWR_VALID_ERR */
    int16_t dev_err;                    /* SYST:ERR? after an AC write     */
    uint8_t len;                        /* reply text length               */
    char    text[PWR_RAW_REPLY_MAX];    /* reply line (query), NUL-padded  */
} pwr_raw_reply_msg_t;

typedef struct _kria_message {
    uint8_t seq;
    uint8_t msgid;
    uint8_t len;
    uint8_t payload64[KRIA_MAX_PAYLOAD_LEN];
} kria_message_t;

#pragma pack(pop)

/* Wire-size guards - must match the other end of the link exactly (Qt
 * backend/kria_helper.h <-> firmware src/kria_helper.h). A mismatch here is a
 * protocol break, so fail the build instead of misparsing at runtime. */
_Static_assert(sizeof(annuation_msg_t) == 6, "annuation_msg_t wire size changed");
_Static_assert(sizeof(annuation_q_msg_t) == 7, "annuation_q_msg_t wire size changed");
_Static_assert(sizeof(timedelay_msg_t) == 6, "timedelay_msg_t wire size changed");
_Static_assert(sizeof(fwi_msg_t) == 12, "fwi_msg_t wire size changed");
_Static_assert(sizeof(cmd_msg_t) == 68, "cmd_msg_t wire size changed");
_Static_assert(sizeof(adc7928_msg_t) == 20, "adc7928_msg_t wire size changed");
_Static_assert(sizeof(rf_status_msg_t) == 28, "rf_status_msg_t wire size changed");
_Static_assert(sizeof(board_diag_msg_t) == 76, "board_diag_msg_t wire size changed");
_Static_assert(sizeof(pwr_raw_msg_t) == 70, "pwr_raw_msg_t wire size changed");
_Static_assert(sizeof(pwr_raw_reply_msg_t) == 107, "pwr_raw_reply_msg_t wire size changed");
_Static_assert(sizeof(pwr_query_msg_t) == 6, "pwr_query_msg_t wire size changed");
_Static_assert(sizeof(pwr_status_msg_t) == 24, "pwr_status_msg_t wire size changed");

typedef enum {
    KRIA_STATE_IDLE = 0,
    KRIA_STATE_GOT_SEQ,
    KRIA_STATE_GOT_MSGID,
    KRIA_STATE_GOT_PAYLOAD
} kria_state_t;

typedef struct __kria_status {
    uint8_t      msg_received;
    kria_state_t parse_state;
    uint8_t      packet_idx;
} kria_status_t;

static inline uint8_t kria_decode(kria_message_t *rxmsg, kria_status_t *status, uint8_t c)
{
    status->msg_received = 0U;
    switch (status->parse_state) {
    case KRIA_STATE_IDLE:
        rxmsg->seq = c;
        status->parse_state = KRIA_STATE_GOT_SEQ;
        break;
    case KRIA_STATE_GOT_SEQ:
        rxmsg->msgid = c;
        status->parse_state = KRIA_STATE_GOT_MSGID;
        break;
    case KRIA_STATE_GOT_MSGID:
        rxmsg->len = c;
        status->packet_idx = 0U;
        /* NOTE: len is uint8_t (0..255) and KRIA_MAX_PAYLOAD_LEN is 255, so
         * "len > KRIA_MAX_PAYLOAD_LEN" can never be true - payload64[] is
         * sized to exactly KRIA_MAX_PAYLOAD_LEN, so there is no overflow to
         * guard against either. Removed the tautological branch (it compiled
         * to dead code and gcc -Wtype-limits correctly flags it); if
         * KRIA_MAX_PAYLOAD_LEN is ever lowered below 255, re-add the check. */
        if (rxmsg->len == 0U) {
            status->msg_received = 1U;
            status->parse_state = KRIA_STATE_IDLE;
        } else {
            status->parse_state = KRIA_STATE_GOT_PAYLOAD;
        }
        break;
    case KRIA_STATE_GOT_PAYLOAD:
        rxmsg->payload64[status->packet_idx++] = c;
        if (status->packet_idx >= rxmsg->len) {
            status->msg_received = 1U;
            status->parse_state = KRIA_STATE_IDLE;
        }
        break;
    default:
        status->parse_state = KRIA_STATE_IDLE;
        break;
    }
    return status->msg_received;
}

static inline uint16_t kria_msg_to_send_buffer(uint8_t *buff, const kria_message_t *msg)
{
    buff[0] = msg->seq;
    buff[1] = msg->msgid;
    buff[2] = msg->len;
    if (msg->len > 0U) {
        memcpy(&buff[3], msg->payload64, msg->len);
    }
    return (uint16_t)(3U + msg->len);
}

static inline void kria_msg_output_encode(int seq, int msg_id, const void *p_struct_data, kria_message_t *msg)
{
    msg->seq   = (uint8_t)seq;
    msg->msgid = (uint8_t)msg_id;
    msg->len   = 0U;

    if (p_struct_data == 0) {
        return;
    }

    switch (msg_id) {
    case 35:
    case 36:
        msg->len = (uint8_t)sizeof(fwi_msg_t);
        break;
    case 37:
        msg->len = (uint8_t)sizeof(timedelay_msg_t);
        break;
    case 38:
        msg->len = (uint8_t)sizeof(annuation_q_msg_t);
        break;
    case 39:
        msg->len = (uint8_t)sizeof(cmd_msg_t);
        break;
    case 40:
        msg->len = (uint8_t)sizeof(adc7928_msg_t);
        break;
    case 41:
        msg->len = (uint8_t)sizeof(pwr_query_msg_t);
        break;
    case 42:
        msg->len = (uint8_t)sizeof(pwr_status_msg_t);
        break;
    case 43:
        msg->len = (uint8_t)sizeof(pwr_raw_msg_t);
        break;
    case 44:
        msg->len = (uint8_t)sizeof(pwr_raw_reply_msg_t);
        break;
    default:
        return;
    }
    memcpy(msg->payload64, p_struct_data, msg->len);
}

/* Small helper used only on the firmware side: builds a 1-byte ACK payload
 * (payload64[0] = 1 success / 0 fail) for msgid 35..38, matching what
 * DashboardManager::parseIncomingData() already expects (it reads
 * payload64[0] as the success flag for those msgids). */
static inline void kria_msg_build_ack(int seq, int msg_id, uint8_t success, kria_message_t *msg)
{
    uint8_t val = success ? 1U : 0U;
    msg->seq    = (uint8_t)seq;
    msg->msgid  = (uint8_t)msg_id;
    msg->len    = 1U;
    msg->payload64[0] = val;
}

#endif /* KRIA_HELPER_H */
