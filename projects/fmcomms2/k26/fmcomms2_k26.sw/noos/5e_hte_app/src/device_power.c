/*******************************************************************************
 * @file    device_power.c
 * @brief   AC/DC bench-supply control + readback as non-blocking
 *          transactions - see device_power.h for the design.
 ******************************************************************************/
#include "device_power.h"
#include "board_config.h"
#include "board_app.h"   /* BoardApp_ServiceRx(): keep the RS422/PC RX FIFO
                          * drained while a supply command is being sent */
#include "kria_helper.h" /* PWR_DEV_* / PWR_REASON_* / PWR_RESULT_* / PWR_VALID_* */
#include "xtime_l.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define PL_CMD_MAX      72U   /* raw commands up to 63 chars + NUL */
#define PL_LINE_MAX     96U
#define PL_MAX_STEPS    14U
#define PL_RESULTQ_LEN  4U

typedef enum {
    STEP_WRITE = 0,         /* send command, then wait gap_ms              */
    STEP_QUERY              /* send command, wait for one reply line       */
} pl_step_kind_t;

typedef enum {
    F_NONE = 0,
    F_ERR_DRAIN,            /* SYST:ERR? repeated until 0 - before a set    */
    F_ERR,                  /* SYST:ERR? after a set - result is reported   */
    F_OUTPUT,               /* ON/OFF                                       */
    F_VOLT,
    F_CURR,
    F_FREQ,
    F_RAW                   /* raw reply text, stored unparsed          */
} pl_field_t;

typedef struct {
    uint8_t  kind;
    uint8_t  field;
    uint16_t gap_ms;
    char     cmd[PL_CMD_MAX];
} pl_step_t;

typedef enum {
    ST_IDLE = 0,
    ST_NEXT,                /* ready to run steps[idx]                     */
    ST_WAIT_REPLY,          /* query sent, waiting for a reply line        */
    ST_GAP                  /* pacing pause, then ST_NEXT                  */
} pl_state_t;

typedef struct {
    XUartNs550     *uart;
    uint8_t         dev;
    volatile pl_state_t st;

    pl_step_t       steps[PL_MAX_STEPS];
    uint8_t         nsteps;
    uint8_t         idx;
    uint8_t         drain_count;
    int8_t          expect_output;      /* -1 = do not check              */
    XTime           deadline;

    /* RX line assembly (filled by PowerLink_ServiceRx) */
    char            line[PL_LINE_MAX];
    uint8_t         line_len;
    char            reply[PL_LINE_MAX];
    volatile uint8_t reply_ready;

    power_result_t  res;
} pl_ctx_t;

static pl_ctx_t       s_ctx[2];         /* [0] = AC, [1] = DC              */
static power_result_t s_resq[PL_RESULTQ_LEN];
static uint8_t        s_resq_head = 0U;
static uint8_t        s_resq_count = 0U;

/* ------------------------------------------------------------------------ */
static XTime MsToTicks(uint32_t ms)
{
    return ((XTime)COUNTS_PER_SECOND / 1000U) * (XTime)ms;
}

static XTime Now(void)
{
    XTime t;
    XTime_GetTime(&t);
    return t;
}

static pl_ctx_t *CtxFor(uint8_t dev)
{
    if (dev == PWR_DEV_AC) {
        return &s_ctx[0];
    }
    if (dev == PWR_DEV_DC) {
        return &s_ctx[1];
    }
    return NULL;
}

void Device_UartSendString(XUartNs550 *instance, const char *str)
{
    while (*str != '\0') {
        while (XUartNs550_IsSending(instance)) {
            BoardApp_ServiceRx();
        }
        XUartNs550_Send(instance, (u8 *)str, 1);
        str++;
    }
    while (XUartNs550_IsSending(instance)) {
        BoardApp_ServiceRx();
    }
}

static void SendLine(XUartNs550 *u, const char *cmd)
{
    Device_UartSendString(u, cmd);
    Device_UartSendString(u, "\r\n");
}

/* ---------------- result queue ------------------------------------------ */
static void ResultPush(const power_result_t *r)
{
    uint8_t slot;

    if (s_resq_count == PL_RESULTQ_LEN) {
        /* Cannot happen with 2 supplies (at most one aborted + one finished
         * each), but never block: drop the oldest. */
        s_resq_head = (uint8_t)((s_resq_head + 1U) % PL_RESULTQ_LEN);
        s_resq_count--;
    }
    slot = (uint8_t)((s_resq_head + s_resq_count) % PL_RESULTQ_LEN);
    s_resq[slot] = *r;
    s_resq_count++;
}

static int ResultPop(power_result_t *out)
{
    if (s_resq_count == 0U) {
        return 0;
    }
    *out = s_resq[s_resq_head];
    s_resq_head = (uint8_t)((s_resq_head + 1U) % PL_RESULTQ_LEN);
    s_resq_count--;
    return 1;
}

/* ---------------- reply parsing ----------------------------------------- */
static int IsNumStart(const char *p)
{
    if ((*p >= '0') && (*p <= '9')) {
        return 1;
    }
    if ((*p == '+') || (*p == '-') || (*p == '.')) {
        const char *q = p + 1;
        if (*q == '.') {
            q++;
        }
        return (*q >= '0') && (*q <= '9');
    }
    return 0;
}

/* first != 0: first number in the line, else the last one. */
static int ParseNumber(const char *line, int first, double *out)
{
    const char *p = line;
    int found = 0;

    while (*p != '\0') {
        if (IsNumStart(p)) {
            char  *end = NULL;
            double v = strtod(p, &end);
            if ((end != NULL) && (end > p)) {
                *out = v;
                found = 1;
                if (first) {
                    return 1;
                }
                p = end;
                continue;
            }
        }
        p++;
    }
    return found;
}

static int ParseOnOff(const char *line, uint8_t *out)
{
    const char *p = line;
    double v;

    while ((*p == ' ') || (*p == '\t')) {
        p++;
    }
    if (((p[0] == 'O') || (p[0] == 'o')) && ((p[1] == 'N') || (p[1] == 'n'))) {
        *out = 1U;
        return 1;
    }
    if (((p[0] == 'O') || (p[0] == 'o')) && ((p[1] == 'F') || (p[1] == 'f'))) {
        *out = 0U;
        return 1;
    }
    if (ParseNumber(line, 0, &v)) {
        *out = (v != 0.0) ? 1U : 0U;
        return 1;
    }
    return 0;
}

/* ---------------- transaction building ---------------------------------- */
static void AddStep(pl_ctx_t *c, uint8_t kind, uint8_t field, uint16_t gap_ms, const char *cmd)
{
    pl_step_t *s;

    if ((c->nsteps >= PL_MAX_STEPS) || (cmd == NULL) || (cmd[0] == '\0')) {
        return;     /* "" = command not available (e.g. DC_CMD_QUERY_OUTPUT) */
    }
    s = &c->steps[c->nsteps++];
    s->kind   = kind;
    s->field  = field;
    s->gap_ms = gap_ms;
    (void)snprintf(s->cmd, sizeof(s->cmd), "%s", cmd);
}

static void Finish(pl_ctx_t *c, uint8_t result)
{
    c->res.result = result;
    if (result == PWR_RESULT_OK) {
        if (((c->res.valid & PWR_VALID_ERR) != 0U) && (c->res.dev_err != 0)) {
            c->res.result = PWR_RESULT_DEV_ERROR;
        } else if ((c->expect_output >= 0) && ((c->res.valid & PWR_VALID_OUTPUT) != 0U)
                   && (c->res.output != (uint8_t)c->expect_output)) {
            c->res.result = PWR_RESULT_OUT_MISMATCH;
        }
    }
    ResultPush(&c->res);
    c->st = ST_IDLE;
    c->nsteps = 0U;
    c->idx = 0U;
}

/* Prepare ctx for a new transaction. Returns NULL if the supply is not
 * ready (a NOT_READY result has then already been queued). */
static pl_ctx_t *Begin(uint8_t dev, uint8_t reason, const power_ack_ctx_t *ack)
{
    pl_ctx_t *c = CtxFor(dev);
    int settle = 0;

    if (c == NULL) {
        return NULL;
    }
    if (c->st != ST_IDLE) {
        /* Superseded: report the old one (its ACK, if any, goes out as a
         * failure). If it was waiting for a reply, let that late reply
         * arrive and be discarded before the new transaction talks. */
        settle = (c->st == ST_WAIT_REPLY);
        Finish(c, PWR_RESULT_ABORTED);
    }

    memset(&c->res, 0, sizeof(c->res));
    c->res.dev    = dev;
    c->res.reason = reason;
    c->res.output = 0xFFU;
    if (ack != NULL) {
        c->res.ack = *ack;
    }
    c->nsteps        = 0U;
    c->idx           = 0U;
    c->drain_count   = 0U;
    c->expect_output = -1;
    c->reply_ready   = 0U;
    c->line_len      = 0U;

    if (c->uart == NULL) {
        c->res.result = PWR_RESULT_NOT_READY;
        ResultPush(&c->res);
        c->st = ST_IDLE;
        return NULL;
    }

    if (settle) {
        c->st = ST_GAP;
        c->deadline = Now() + MsToTicks(PWR_REPLY_TIMEOUT_MS);
    } else {
        c->st = ST_NEXT;
    }
    return c;
}

static void AddAcReadback(pl_ctx_t *c)
{
    AddStep(c, STEP_QUERY, F_ERR,    0U, AC_CMD_QUERY_ERROR);
    AddStep(c, STEP_QUERY, F_OUTPUT, 0U, AC_CMD_QUERY_OUTPUT);
    AddStep(c, STEP_QUERY, F_VOLT,   0U, AC_CMD_MEAS_VOLT);
    AddStep(c, STEP_QUERY, F_CURR,   0U, AC_CMD_MEAS_CURR);
    AddStep(c, STEP_QUERY, F_FREQ,   0U, AC_CMD_MEAS_FREQ);
}

static void AddDcReadback(pl_ctx_t *c)
{
    AddStep(c, STEP_QUERY, F_OUTPUT, 0U, DC_CMD_QUERY_OUTPUT);   /* "" -> skipped */
    AddStep(c, STEP_QUERY, F_VOLT,   0U, DC_CMD_MEAS_VOLT);
    AddStep(c, STEP_QUERY, F_CURR,   0U, DC_CMD_MEAS_CURR);
}

void PowerLink_StartRead(uint8_t dev)
{
    pl_ctx_t *c = CtxFor(dev);

    if ((c != NULL) && (c->st != ST_IDLE)) {
        return;     /* merged: the running transaction reports values too */
    }
    c = Begin(dev, PWR_REASON_READ, NULL);
    if (c == NULL) {
        return;
    }
    if (dev == PWR_DEV_AC) {
        AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, AC_CMD_REMOTE);
        AddAcReadback(c);
    } else {
        AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, DC_CMD_REMOTE);
        AddDcReadback(c);
    }
}

void PowerLink_StartAcSet(float voltage, float freq, int state, const power_ack_ctx_t *ack)
{
    pl_ctx_t *c;
    char      cmd[PL_CMD_MAX];
    long      v10, f10;

    c = Begin(PWR_DEV_AC, PWR_REASON_SET, ack);
    if (c == NULL) {
        return;
    }

    if (!state) {
        /* OFF = output off only. The voltage/frequency fields of an OFF
         * command are not sent: they carry nothing meaningful (the app
         * sends 0/0 when its inputs are empty "--"), and the old path
         * turned 0 V into NORM:VOLT:AC 80.0 (UVP clamp) + NORM:FREQ 0.0. */
        AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, AC_CMD_REMOTE);
        AddStep(c, STEP_QUERY, F_ERR_DRAIN, 0U, AC_CMD_QUERY_ERROR);
        AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, "OUTP OFF");
        AddAcReadback(c);
        c->expect_output = 0;
        return;
    }

    if (voltage > AC_OVP_LIMIT)   voltage = AC_OVP_LIMIT;
    if (voltage < AC_UVP_VOLTAGE) voltage = AC_UVP_VOLTAGE;
    if (freq < 0.0f)              freq = 0.0f;
    /* Same "%d.%d" text as the verified code, but rounded to 0.1 as a whole
     * (old split int/frac could print e.g. "99.10" for 99.96). Integer
     * inputs - all the app sends - produce identical strings. */
    v10 = lroundf(voltage * 10.0f);
    f10 = lroundf(freq * 10.0f);

    AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, AC_CMD_REMOTE);
    AddStep(c, STEP_QUERY, F_ERR_DRAIN, 0U, AC_CMD_QUERY_ERROR);
    AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, AC_CMD_MODE);
    (void)snprintf(cmd, sizeof(cmd), "NORM:VOLT:AC %ld.%ld", v10 / 10, v10 % 10);
    AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, cmd);
    (void)snprintf(cmd, sizeof(cmd), "NORM:FREQ %ld.%ld", f10 / 10, f10 % 10);
    AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, cmd);
    AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, "OUTP ON");
    AddAcReadback(c);
    c->expect_output = 1;
}

void PowerLink_StartDcSet(float voltage, float current, int state, const power_ack_ctx_t *ack)
{
    pl_ctx_t *c;
    char      cmd[PL_CMD_MAX];

    c = Begin(PWR_DEV_DC, PWR_REASON_SET, ack);
    if (c == NULL) {
        return;
    }

    if (!state) {
        /* OFF = "#0 SW0" only (see the AC case above). */
        AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, DC_CMD_REMOTE);
        AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, "#0 SW0");
        AddDcReadback(c);
        c->expect_output = 0;
        return;
    }

    if (voltage > DC_MAX_VOLTAGE) voltage = DC_MAX_VOLTAGE;
    if (voltage < 0.0f)           voltage = 0.0f;
    if (current > DC_MAX_CURRENT) current = DC_MAX_CURRENT;
    if (current < 0.0f)           current = 0.0f;

    /* Order and pacing exactly as the verified sequence. */
    AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, DC_CMD_REMOTE);
    (void)snprintf(cmd, sizeof(cmd), "#0 ICN %.2f", (double)((current / DC_MAX_CURRENT) * 100.0f));
    AddStep(c, STEP_WRITE, F_NONE, 10U, cmd);
    AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, "#0 SW1");
    (void)snprintf(cmd, sizeof(cmd), "#0 VCN %.2f", (double)((voltage / DC_MAX_VOLTAGE) * 100.0f));
    AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, cmd);
    AddDcReadback(c);
    c->expect_output = 1;
}

void PowerLink_StartRaw(uint8_t dev, const char *text)
{
    pl_ctx_t *c = CtxFor(dev);
    power_result_t r;

    if (c == NULL) {
        return;
    }
    if ((c->uart != NULL) && (c->st != ST_IDLE)) {
        /* Never abort a set/refresh for a manual raw command: report busy. */
        memset(&r, 0, sizeof(r));
        r.dev = dev;
        r.reason = PWR_REASON_RAW;
        r.result = PWR_RESULT_BUSY;
        r.output = 0xFFU;
        ResultPush(&r);
        return;
    }
    c = Begin(dev, PWR_REASON_RAW, NULL);
    if (c == NULL) {
        return;     /* NOT_READY already queued */
    }
    if (strchr(text, '?') != NULL) {
        AddStep(c, STEP_QUERY, F_RAW, 0U, text);
    } else {
        AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, text);
        if (dev == PWR_DEV_AC) {
            /* Was it accepted, and what is the output now? */
            AddStep(c, STEP_QUERY, F_ERR, 0U, AC_CMD_QUERY_ERROR);
            AddStep(c, STEP_QUERY, F_OUTPUT, 0U, AC_CMD_QUERY_OUTPUT);
        }
    }
}

void PowerLink_StartOff(uint8_t dev)
{
    pl_ctx_t *c = Begin(dev, PWR_REASON_RESET, NULL);

    if (c == NULL) {
        return;
    }
    if (dev == PWR_DEV_AC) {
        AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, AC_CMD_REMOTE);
        AddStep(c, STEP_QUERY, F_ERR_DRAIN, 0U, AC_CMD_QUERY_ERROR);
        AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, "OUTP OFF");
        AddAcReadback(c);
    } else {
        AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, DC_CMD_REMOTE);
        AddStep(c, STEP_WRITE, F_NONE, 10U, "#0 ICN 0.00");
        AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, "#0 SW0");
        AddStep(c, STEP_WRITE, F_NONE, PWR_WRITE_GAP_MS, "#0 VCN 0.00");
        AddDcReadback(c);
    }
    c->expect_output = 0;
}

/* ---------------- runtime ----------------------------------------------- */
void PowerLink_Init(XUartNs550 *ac_uart, XUartNs550 *dc_uart)
{
    memset(s_ctx, 0, sizeof(s_ctx));
    s_ctx[0].uart = ac_uart;
    s_ctx[0].dev  = PWR_DEV_AC;
    s_ctx[1].uart = dc_uart;
    s_ctx[1].dev  = PWR_DEV_DC;
    s_resq_head = 0U;
    s_resq_count = 0U;
}

int PowerLink_IsReady(uint8_t dev)
{
    pl_ctx_t *c = CtxFor(dev);
    return (c != NULL) && (c->uart != NULL);
}

void PowerLink_ServiceRx(void)
{
    uint32_t k;

    for (k = 0U; k < 2U; k++) {
        pl_ctx_t *c = &s_ctx[k];

        if (c->uart == NULL) {
            continue;
        }
        while (XUartNs550_IsReceiveData(c->uart->BaseAddress)) {
            char ch = (char)XUartNs550_ReadReg(c->uart->BaseAddress, XUN_RBR_OFFSET);

            if ((c->st != ST_WAIT_REPLY) || c->reply_ready) {
                c->line_len = 0U;       /* unsolicited / late byte: discard */
                continue;
            }
            if ((ch == '\r') || (ch == '\n')) {
                if (c->line_len == 0U) {
                    continue;           /* empty line, e.g. LF after CR      */
                }
                c->line[c->line_len] = '\0';
                c->line_len = 0U;
                /* Some instruments echo the command line - skip the echo. */
                if (strcmp(c->line, c->steps[c->idx].cmd) == 0) {
                    continue;
                }
                memcpy(c->reply, c->line, sizeof(c->reply));
                c->reply_ready = 1U;
            } else if (c->line_len < (PL_LINE_MAX - 1U)) {
                c->line[c->line_len++] = ch;
            }
        }
    }
}

/* Handle a received reply for the current query step. Returns 0 if the
 * transaction finished (Finish() called), 1 to continue. */
static int ApplyReply(pl_ctx_t *c)
{
    pl_step_t *s = &c->steps[c->idx];
    double     v = 0.0;
    uint8_t    onoff = 0U;

    switch (s->field) {
    case F_ERR_DRAIN:
        if (ParseNumber(c->reply, 1, &v) && (v != 0.0) && (c->drain_count < PWR_ERR_DRAIN_MAX)) {
            c->drain_count++;
            return 2;                   /* repeat this same step */
        }
        return 1;                       /* queue empty (or unparsable): go on */
    case F_ERR:
        if (!ParseNumber(c->reply, 1, &v)) {
            Finish(c, PWR_RESULT_BAD_REPLY);
            return 0;
        }
        c->res.dev_err = (int16_t)v;
        c->res.valid |= PWR_VALID_ERR;
        return 1;
    case F_OUTPUT:
        if (!ParseOnOff(c->reply, &onoff)) {
            Finish(c, PWR_RESULT_BAD_REPLY);
            return 0;
        }
        c->res.output = onoff;
        c->res.valid |= PWR_VALID_OUTPUT;
        return 1;
    case F_RAW:
        (void)snprintf(c->res.raw_reply, sizeof(c->res.raw_reply), "%s", c->reply);
        c->res.has_raw_reply = 1U;
        return 1;
    case F_VOLT:
    case F_CURR:
    case F_FREQ:
        if (!ParseNumber(c->reply, 0, &v)) {
            Finish(c, PWR_RESULT_BAD_REPLY);
            return 0;
        }
        if (s->field == F_VOLT) {
            c->res.volt = (float)v;
            c->res.valid |= PWR_VALID_VOLT;
        } else if (s->field == F_CURR) {
            c->res.curr = (float)v;
            c->res.valid |= PWR_VALID_CURR;
        } else {
            c->res.freq = (float)v;
            c->res.valid |= PWR_VALID_FREQ;
        }
        return 1;
    default:
        return 1;
    }
}

static void Step(pl_ctx_t *c)
{
    XTime now = Now();

    switch (c->st) {
    case ST_IDLE:
        return;

    case ST_GAP:
        if (now >= c->deadline) {
            c->st = ST_NEXT;
        }
        return;

    case ST_NEXT: {
        pl_step_t *s;

        if (c->idx >= c->nsteps) {
            Finish(c, PWR_RESULT_OK);
            return;
        }
        s = &c->steps[c->idx];
        if (s->kind == STEP_WRITE) {
            SendLine(c->uart, s->cmd);
            c->idx++;
            c->st = ST_GAP;
            c->deadline = Now() + MsToTicks(s->gap_ms);
        } else {
            /* Flush anything stale, then arm the reply wait BEFORE sending
             * so an echo / fast reply is assembled, not discarded. */
            PowerLink_ServiceRx();
            c->line_len = 0U;
            c->reply_ready = 0U;
            c->st = ST_WAIT_REPLY;
            c->deadline = Now() + MsToTicks(PWR_REPLY_TIMEOUT_MS);
            SendLine(c->uart, s->cmd);
        }
        return;
    }

    case ST_WAIT_REPLY:
        if (c->reply_ready) {
            int r = ApplyReply(c);
            if (r == 0) {
                return;                 /* finished */
            }
            if (r == 1) {
                c->idx++;
            }
            c->reply_ready = 0U;
            c->st = ST_GAP;
            c->deadline = Now() + MsToTicks(PWR_INTER_CMD_GAP_MS);
            return;
        }
        if (now >= c->deadline) {
            if (c->steps[c->idx].field == F_ERR_DRAIN) {
                /* No answer to the pre-set error drain: still send the set
                 * commands (never block operator control on readback); the
                 * confirmation queries after it will report "no reply". */
                c->idx++;
                c->st = ST_NEXT;
                return;
            }
            Finish(c, PWR_RESULT_NO_REPLY);
        }
        return;

    default:
        c->st = ST_IDLE;
        return;
    }
}

int PowerLink_Poll(power_result_t *out)
{
    PowerLink_ServiceRx();
    Step(&s_ctx[0]);
    Step(&s_ctx[1]);
    return ResultPop(out);
}
