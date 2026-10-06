/*******************************************************************************
 * @file    board_app.c
 * @brief   5E-HTE board-control app: AC/DC bench power control, F1956
 *          attenuator control, optical delay line control, ADC7928
 *          telemetry, all driven over the RS422/PC link using the VIP+kria
 *          binary protocol (vip_helper.c / kria_helper.h / cmd_dispatcher.c).
 *
 *          Called from main.c as BoardApp_Init() (once, after AD9361 setup)
 *          then BoardApp_PollOnce() (once per pass of main()'s superloop).
 *          Periodic work (ADC7928 telemetry, RF "WAVE" spectrum, "RFST" RX
 *          config report) is scheduled on the ARMv8 global timer (XTime).
 *
 *          IMPORTANT FINDING (see board_config.h for the full note): the
 *          xparameters.h you supplied wires NO interrupt line for any of
 *          the 3 UartNs550 cores to the GIC (only the two AD9361 DMA IRQs
 *          are connected), unlike what src_att_pwr_odly/main.cpp assumed.
 *          This file therefore:
 *            - initializes the AC/DC supply UARTs without interrupt; their
 *              replies (readback / set confirmation, device_power.c) are
 *              polled from BoardApp_ServiceRx() - at 9600 baud the 16-byte
 *              RX FIFO holds ~16 ms, far more than any gap between polls;
 *            - initializes the RS422/PC link with interrupt-driven RX when
 *              UART_PC_HAS_INTR==1 (byte -> circular buffer -> main loop,
 *              same architecture as the validated reference code), or
 *              falls back to direct polling of the UART in the main loop
 *              when UART_PC_HAS_INTR==0 (the case for your current
 *              xparameters.h), so the firmware still builds and runs against
 *              the exact hardware you described - please confirm which case
 *              is actually true for your board.
 *
 *          SHARED-SPI-BUS NOTE: PSU_SPI_0 is used by BOTH the AD9361 (via
 *          main.c's Ad9361_Bringup(), which runs and finishes BEFORE
 *          BoardApp_Init() is called) and the 3x F1956 attenuators here
 *          (device_attenuator.c). Attenuator_Init() (called first thing in
 *          BoardApp_Init() below) unconditionally reprograms the shared
 *          XSpiPs controller's mode/clock for the F1956's needs, which is
 *          exactly the reclaim this ordering requires - do not reorder
 *          Ad9361_Bringup()/BoardApp_Init() in main.c, and if a future
 *          change makes the AD9361 driver issue SPI transfers again at
 *          runtime (e.g. live frequency/gain changes wired into the
 *          dispatcher), re-review this - the two drivers do not currently
 *          arbitrate the bus against each other beyond "AD9361 goes first,
 *          then only the attenuator ever touches SPI0 again".
 ******************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "sleep.h"    /* usleep()/sleep() - Xilinx standalone BSP header, NOT
                       * POSIX <unistd.h> (bare-metal has no such thing; the
                       * vendor no-OS code agrees - see
                       * noos/drivers/platform/xilinx/xilinx_delay.c and
                       * ad9361/src/main.c, both use "sleep.h"/<sleep.h>). */

#include "xparameters.h"
#include "xuartns550.h"
#include "xuartns550_i.h"
#include "xstatus.h"
#include "xscugic.h"
#include "xil_exception.h"
#include "xil_printf.h"
#include "xtime_l.h"  /* XTime_GetTime()/COUNTS_PER_SECOND - ARMv8 global timer */

#include "board_config.h"
#include "board_app.h"
#include "device_attenuator.h"
#include "device_optical_delay.h"
#include "device_adc7928.h"
#include "cmd_dispatcher.h"
#include "stream_framer.h"
#include "device_power.h"
#include "ad9361_phy.h"
#include "kria_helper.h"

#define RAW_SIZE                1

/* Software RX ring for the RS422/PC link (power of two). Filled by the UART
 * ISR when UART_PC_HAS_INTR==1, or by BoardApp_ServiceRx() in polled mode;
 * drained one byte per BoardApp_PollOnce(). Single producer/single consumer
 * on one core, so plain volatile indices are enough (no locking). */
#define PC_RX_RING_SIZE         1024U
#define PC_RX_RING_MASK         (PC_RX_RING_SIZE - 1U)

/* Periodic tasks, scheduled on the ARMv8 global timer (XTime) instead of
 * the old "count idle passes" scheme: that one restarted from zero on every
 * received byte, so steady PC traffic (e.g. the app's PING every 2 s plus a
 * few commands) could delay telemetry/spectrum indefinitely, and its real
 * period drifted with loop overhead.
 *  - ADC7928 telemetry: ~1 s (human-facing rate).
 *  - RF spectrum ("WAVD", 124-pt): 100 ms (operator request 2026-10-05; a
 *    274-byte frame takes ~24 ms at 115200 -> ~24 % of the RS422 link),
 *    RX1.
 *  - RF config report ("RFST"): every ~3 s, so an app that connects AFTER
 *    the board booted still learns LO/sample rate within a few seconds. */
#define ADC_TELEMETRY_PERIOD_MS    1000U
#define RF_WAVE_PERIOD_MS          100U
#define RF_STATUS_PERIOD_MS        3000U
#define RF_WAVE_RETRY_MS           2000U    /* after 3 failed captures in a row */
#define IDLE_SLEEP_US              20U      /* nothing to do this pass        */
#define DELAY_SERVICE_CHUNK_US     50U      /* BoardApp_DelayUs() granularity */

#if UART_PC_HAS_INTR
/* This project's only GIC user (AD9361 bring-up in main.c does not need
 * interrupts), so board_app.c owns it outright - no cross-file sharing to
 * coordinate. */
static XScuGic InterruptController;
#endif

static XUartNs550 UartAcPwr;
static XUartNs550 UartDcPwr;
static XUartNs550 UartPc;

/* Set only once the RS422/PC link is up - BoardApp_PollOnce() is a no-op
 * before that (previously a failed BoardApp_Init() returned early and the
 * superloop then polled an uninitialized UART at base address 0). */
static int s_pc_link_ready = 0;

static volatile uint8_t  s_rx_ring[PC_RX_RING_SIZE];
static volatile uint32_t s_rx_head = 0U;   /* producer index */
static volatile uint32_t s_rx_tail = 0U;   /* consumer index */
static volatile uint32_t s_rx_dropped = 0U;

static XTime s_next_adc_tick;
static XTime s_next_wave_tick;
static XTime s_next_rfst_tick;
static int   s_adc_ok = 0;
static int   s_rf_stream_ok = 0;

static void RxRing_Push(uint8_t b)
{
    uint32_t next = (s_rx_head + 1U) & PC_RX_RING_MASK;
    if (next == s_rx_tail) {
        s_rx_dropped++;                 /* full: drop newest, keep counting */
        return;
    }
    s_rx_ring[s_rx_head] = b;
    s_rx_head = next;
}

static int RxRing_Pop(uint8_t *b)
{
    if (s_rx_tail == s_rx_head) {
        return 0;
    }
    *b = s_rx_ring[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1U) & PC_RX_RING_MASK;
    return 1;
}

static XTime MsToTicks(uint32_t ms)
{
    return ((XTime)COUNTS_PER_SECOND / 1000U) * (XTime)ms;
}

#if UART_PC_HAS_INTR
static uint8_t g_RxBytePc[RAW_SIZE];

static volatile int TotalReceivedCount;
static volatile int TotalSentCount;
static volatile int TotalErrorCount;

static void UartNs550IntrHandler(void *CallBackRef, u32 Event, unsigned int EventData);
static int  UartNs550SetupIntrSystem(XScuGic *IntcInstancePtr, XUartNs550 *UartInstancePtr, u16 UartIntrId);
#endif

/* AC/DC bench-supply UART init: FIFOs on, no interrupt - RX (supply
 * replies) is polled by PowerLink_ServiceRx() via BoardApp_ServiceRx(). */
static int UART_InitSupply(XUartNs550 *UartInstancePtr, u16 UartDeviceId, u32 baud)
{
    int Status;

    Status = XUartNs550_Initialize(UartInstancePtr, UartDeviceId);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }
    Status = XUartNs550_SetBaudRate(UartInstancePtr, baud);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }
    XUartNs550_SetOptions(UartInstancePtr, XUN_OPTION_FIFOS_ENABLE);
    return XST_SUCCESS;
}

/* RS422/PC link init: interrupt-driven when the hardware provides one,
 * polled fallback otherwise (see file header). */
static int UART_InitPcLink(XUartNs550 *UartInstancePtr, u16 UartDeviceId, u32 baud)
{
    int Status;

    Status = XUartNs550_Initialize(UartInstancePtr, UartDeviceId);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }
    Status = XUartNs550_SetBaudRate(UartInstancePtr, baud);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

#if UART_PC_HAS_INTR
    Status = UartNs550SetupIntrSystem(&InterruptController, UartInstancePtr, UART_PC_INTR_ID);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }
    XUartNs550_SetHandler(UartInstancePtr, UartNs550IntrHandler, UartInstancePtr);
    XUartNs550_SetOptions(UartInstancePtr, XUN_OPTION_DATA_INTR | XUN_OPTION_FIFOS_ENABLE);
#else
    XUartNs550_SetOptions(UartInstancePtr, XUN_OPTION_FIFOS_ENABLE);
#endif
    return XST_SUCCESS;
}

/* ---------------- board log + health report (shown in the app) ---------
 * Boot/runtime messages used to go only to xil_printf (JTAG/UART console).
 * BoardLog() still prints there, AND keeps the last BOOTLOG_LINES lines and
 * sends each one to the app ("LOGT"); on every host (re)connect the whole
 * log is replayed, followed by a "DIAG" health report - so the operator
 * sees e.g. "AD9361 khoi tao that bai (status=-22)" in the app Terminal
 * without a console cable. */
#define BOOTLOG_LINES           24U
#define HOST_RECONNECT_GAP_MS   5000U
#define DIAG_PERIOD_MS          3000U

static char    s_log_text[BOOTLOG_LINES][BOARD_LOG_MAX_TEXT + 1U];
static uint8_t s_log_level[BOOTLOG_LINES];
static uint32_t s_log_count = 0U;          /* total lines ever logged */
static XTime   s_boot_tick;
static XTime   s_last_ping_tick;
static int     s_seen_ping = 0;
static int     s_replay_pending = 0;
static XTime   s_next_diag_tick;
static int     s_app_ready = 0;            /* BoardApp_Init() finished    */
static int     s_att_ok = 0, s_odly_ok = 0, s_ac_ok = 0, s_dc_ok = 0;

void BoardLog(uint8_t level, const char *fmt, ...)
{
    char    line[BOARD_LOG_MAX_TEXT + 1U];
    va_list ap;
    uint32_t slot;

    va_start(ap, fmt);
    (void)vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    xil_printf("%s\r\n", line);

    slot = s_log_count % BOOTLOG_LINES;
    memcpy(s_log_text[slot], line, sizeof(line));
    s_log_level[slot] = level;
    s_log_count++;

    if (s_pc_link_ready) {
        CmdDispatcher_SendLog(level, line);
    }
}

void BoardApp_OnHostPing(void)
{
    XTime now;

    XTime_GetTime(&now);
    if (!s_seen_ping || ((now - s_last_ping_tick) > MsToTicks(HOST_RECONNECT_GAP_MS))) {
        s_replay_pending = 1;      /* app (re)connected: replay in PollOnce */
    }
    s_seen_ping = 1;
    s_last_ping_tick = now;
}

static void SendDiag(void)
{
    board_diag_msg_t d;
    const stream_framer_stats_t *st = StreamFramer_GetStats();
    XTime now;

    memset(&d, 0, sizeof(d));
    memcpy(d.msg, "DIAG", 4U);
    d.version       = 1U;
    d.ad9361_ok     = g_ad9361_ok;
    d.rf_stream_ok  = (uint8_t)s_rf_stream_ok;
    d.adc7928_ok    = (uint8_t)s_adc_ok;
    d.att_ok        = (uint8_t)s_att_ok;
    d.odly_ok       = (uint8_t)s_odly_ok;
    d.ac_uart_ok    = (uint8_t)s_ac_ok;
    d.dc_uart_ok    = (uint8_t)s_dc_ok;
    d.ad9361_status = g_ad9361_init_status;
    XTime_GetTime(&now);
    d.uptime_s      = (uint32_t)((now - s_boot_tick) / (XTime)COUNTS_PER_SECOND);
    d.dma_ok         = st->dma_ok;
    d.dma_timeout    = st->dma_timeout;
    d.dma_start_fail = st->dma_start_fail;
    d.rx1_peak       = st->rx1_peak;
    CmdDispatcher_GetLinkStats(&d.pc_rx_bytes, &d.pc_frames_ok, &d.pc_frames_bad);
    d.pc_rx_dropped  = s_rx_dropped;
    (void)snprintf(d.build, sizeof(d.build), "%s %s", __DATE__, __TIME__);
    CmdDispatcher_SendDiag(&d);
}

static void ReplayLog(void)
{
    uint32_t first = (s_log_count > BOOTLOG_LINES) ? (s_log_count - BOOTLOG_LINES) : 0U;
    uint32_t i;

    for (i = first; i < s_log_count; i++) {
        uint32_t slot = i % BOOTLOG_LINES;
        CmdDispatcher_SendLog(s_log_level[slot], s_log_text[slot]);
    }
}

/* Step 1 of boot (main.c, BEFORE Ad9361_Bringup()): RS422/PC link only, so
 * every boot log line - including the AD9361 bring-up result, or the last
 * line before it hangs - reaches the app live. Touches no SPI device. */
void BoardApp_InitLink(void)
{
    XTime_GetTime(&s_boot_tick);
    if (UART_InitPcLink(&UartPc, UART_PC_DEVICE_ID, UART_PC_BAUDRATE) != XST_SUCCESS) {
        xil_printf("Loi nghiem trong: Khoi tao UART RS422 PC that bai - khong the giao tiep voi app.\r\n");
        return;
    }
#if UART_PC_HAS_INTR
    XUartNs550_Recv(&UartPc, g_RxBytePc, RAW_SIZE);
#endif
    CmdDispatcher_Init(&UartPc);
    s_pc_link_ready = 1;
    BoardLog(BOARD_LOG_INFO, "5E-HTE firmware build %s %s - RS422/PC san sang (%s).",
             __DATE__, __TIME__, UART_PC_HAS_INTR ? "RX ngat" : "RX polling");
}

/* Step 2 of boot (main.c, AFTER Ad9361_Bringup(), whether it succeeded or
 * not): everything else. Order matters for the shared SPI0 controller (see
 * file header): the attenuator init must run after Ad9361_Bringup(). Device
 * failures are NOT fatal: commands for a failed device are NACKed and the
 * reason is in the app (boot log + "DIAG"). */
void BoardApp_Init(void)
{
    XTime now;

    if (!s_pc_link_ready) {
        BoardApp_InitLink();
    }

    s_att_ok = (Attenuator_Init() == XST_SUCCESS);
    if (!s_att_ok) {
        BoardLog(BOARD_LOG_ERROR, "Loi: Khoi tao SPI/GPIO suy hao (F1956) - lenh ATT se bi NACK.");
    }
    s_odly_ok = (OpticalDelay_Init() == XST_SUCCESS);
    if (!s_odly_ok) {
        BoardLog(BOARD_LOG_ERROR, "Loi: Khoi tao GPIO delay quang - lenh DELAY se bi NACK.");
    }
    s_adc_ok = (ADC7928_Init() == XST_SUCCESS);
    if (!s_adc_ok) {
        BoardLog(BOARD_LOG_WARN, "Canh bao: Khoi tao ADC7928 that bai - khong co telemetry ADC.");
    }
    /* RX DMA needs the RX AXI-ADC core that ad9361_init() brings up - skip
     * it entirely if the AD9361 bring-up failed. */
    if (g_ad9361_ok) {
        s_rf_stream_ok = (StreamFramer_Init() == XST_SUCCESS);
        if (!s_rf_stream_ok) {
            BoardLog(BOARD_LOG_ERROR, "Loi: Khoi tao RX DMA (pho RF) that bai - khong co WAVE.");
        }
    } else {
        s_rf_stream_ok = 0;
        BoardLog(BOARD_LOG_ERROR, "Pho RF tat: AD9361 chua khoi tao duoc (status=%d). AC/DC/ATT/Delay van hoat dong.",
                 (int)g_ad9361_init_status);
    }

    /* Bench-supply UART baud rates come from board_config.h. */
    s_ac_ok = (UART_InitSupply(&UartAcPwr, UART_AC_PWR_DEVICE_ID, UART_AC_PWR_BAUDRATE) == XST_SUCCESS);
    if (!s_ac_ok) {
        BoardLog(BOARD_LOG_ERROR, "Loi: Khoi tao UART nguon AC - lenh AC se bi NACK.");
    }
    s_dc_ok = (UART_InitSupply(&UartDcPwr, UART_DC_PWR_DEVICE_ID, UART_DC_PWR_BAUDRATE) == XST_SUCCESS);
    if (!s_dc_ok) {
        BoardLog(BOARD_LOG_ERROR, "Loi: Khoi tao UART nguon DC - lenh DC se bi NACK.");
    }
    PowerLink_Init(s_ac_ok ? &UartAcPwr : NULL, s_dc_ok ? &UartDcPwr : NULL);
    CmdDispatcher_SetDeviceReady(s_att_ok, s_odly_ok);

    if (!s_pc_link_ready) {
        return;     /* no PC link: nothing can be controlled (logged above) */
    }

    XTime_GetTime(&now);
    s_next_adc_tick  = now + MsToTicks(ADC_TELEMETRY_PERIOD_MS);
    s_next_wave_tick = now + MsToTicks(RF_WAVE_PERIOD_MS);
    s_next_rfst_tick = now;                 /* send the RF config right away */
    s_next_diag_tick = now;                 /* and the health report         */
    s_app_ready = 1;

    BoardLog(BOARD_LOG_INFO, "Board san sang: AD9361 %s, pho RF %s, ATT %s, Delay %s, ADC7928 %s, UART AC %s, UART DC %s.",
             g_ad9361_ok ? "OK" : "LOI", s_rf_stream_ok ? "OK" : "TAT",
             s_att_ok ? "OK" : "LOI", s_odly_ok ? "OK" : "LOI", s_adc_ok ? "OK" : "LOI",
             s_ac_ok ? "OK" : "LOI", s_dc_ok ? "OK" : "LOI");
}

void BoardApp_ServiceRx(void)
{
    /* Supply replies (AC/DC RS232): always polled, both modes. Only reads
     * bytes into line buffers - never transmits, so it is safe from inside
     * any wait loop. */
    PowerLink_ServiceRx();
#if !UART_PC_HAS_INTR
    /* Polled mode: move everything the UART FIFO holds into the software
     * ring. Called from the main loop AND from every blocking wait in this
     * firmware (UART TX, AC/DC command pacing - see BoardApp_DelayUs()), so
     * host bytes are not lost to a 16-byte hardware FIFO overrun while e.g.
     * a ~13 ms "WAVE" frame or a ~250 ms AC-supply command sequence is
     * being sent. In interrupt mode the ISR already does this - no-op. */
    if (!s_pc_link_ready) {
        return;
    }
    while (XUartNs550_IsReceiveData(UartPc.BaseAddress)) {
        RxRing_Push((uint8_t)XUartNs550_ReadReg(UartPc.BaseAddress, XUN_RBR_OFFSET));
    }
#endif
}

void BoardApp_DelayUs(uint32_t us)
{
    while (us >= DELAY_SERVICE_CHUNK_US) {
        BoardApp_ServiceRx();
        usleep(DELAY_SERVICE_CHUNK_US);
        us -= DELAY_SERVICE_CHUNK_US;
    }
    if (us > 0U) {
        usleep(us);
    }
    BoardApp_ServiceRx();
}

void BoardApp_PollOnce(void)
{
    uint8_t rx_byte = 0U;
    XTime   now;

    if (!s_app_ready) {
        return;
    }

    BoardApp_ServiceRx();
    /* AC/DC supply transactions (non-blocking): sends the next command of a
     * set/refresh sequence when due and reports finished ones (msgid 42 +
     * the deferred msgid 35/36 ACK). */
    CmdDispatcher_PollPower();
    if (RxRing_Pop(&rx_byte)) {
        CmdDispatcher_HandleByte(rx_byte);
        return;                 /* drain host bytes first, tasks next pass */
    }

    XTime_GetTime(&now);

    /* App (re)connected: replay the board log, then the health report. */
    if (s_replay_pending) {
        s_replay_pending = 0;
        ReplayLog();
        s_next_diag_tick = now;
        return;
    }

    if (now >= s_next_diag_tick) {
        s_next_diag_tick = now + MsToTicks(DIAG_PERIOD_MS);
        SendDiag();
        return;
    }

    if (s_adc_ok && (now >= s_next_adc_tick)) {
        uint16_t codes[ADC7928_NUM_CHANNELS];
        s_next_adc_tick = now + MsToTicks(ADC_TELEMETRY_PERIOD_MS);
        if (ADC7928_ReadAll(codes) == XST_SUCCESS) {
            CmdDispatcher_SendAdcTelemetry(codes);
        }
        /* On failure simply skip this tick - next tick retries. */
        return;
    }

    if (now >= s_next_rfst_tick) {
        s_next_rfst_tick = now + MsToTicks(RF_STATUS_PERIOD_MS);
        CmdDispatcher_SendRfStatus();
        return;
    }

    if (s_rf_stream_ok && (now >= s_next_wave_tick)) {
        static uint32_t fail_streak = 0U;
        /* DMA timeout etc. -> skip this tick (see stream_framer.h). Each
         * failed capture blocks up to 500 ms (host bytes are still buffered,
         * but answered only afterwards): after 3 failures in a row, retry
         * only every 2 s so PING/commands stay responsive while the RX data
         * path is broken. Counted in "DIAG" (dma_timeout). */
        if (StreamFramer_CaptureAndSend() == XST_SUCCESS) {
            fail_streak = 0U;
        } else if (fail_streak < 0xFFFFU) {
            fail_streak++;
        }
        XTime_GetTime(&now);
        if (fail_streak >= 3U) {
            s_next_wave_tick = now + MsToTicks(RF_WAVE_RETRY_MS);
        } else {
            /* Fixed 100 ms grid (not "100 ms after this frame finished",
             * which drifted to ~124 ms by the ~24 ms frame transmit time). */
            s_next_wave_tick += MsToTicks(RF_WAVE_PERIOD_MS);
            if (s_next_wave_tick <= now) {
                s_next_wave_tick = now + MsToTicks(RF_WAVE_PERIOD_MS);
            }
        }
        return;
    }

    usleep(IDLE_SLEEP_US);
}

#if UART_PC_HAS_INTR
static void UartNs550IntrHandler(void *CallBackRef, u32 Event, unsigned int EventData)
{
    XUartNs550 *UartNs550Ptr = (XUartNs550 *)CallBackRef;

    if (Event == XUN_EVENT_SENT_DATA) {
        TotalSentCount = EventData;
    }

    if (Event == XUN_EVENT_RECV_DATA) {
        TotalReceivedCount = EventData;
        if (UartNs550Ptr == &UartPc) {
            RxRing_Push(g_RxBytePc[0]);
            while (XUartNs550_IsReceiveData(UartNs550Ptr->BaseAddress)) {
                RxRing_Push((uint8_t)XUartNs550_ReadReg(UartNs550Ptr->BaseAddress, XUN_RBR_OFFSET));
            }
            XUartNs550_Recv(&UartPc, g_RxBytePc, RAW_SIZE);
        }
    }

    if (Event == XUN_EVENT_RECV_TIMEOUT) {
        TotalReceivedCount = EventData;
    }

    if (Event == XUN_EVENT_RECV_ERROR) {
        TotalReceivedCount = EventData;
        TotalErrorCount++;
        (void)XUartNs550_GetLastErrors(UartNs550Ptr);
    }
}

static int UartNs550SetupIntrSystem(XScuGic *IntcInstancePtr, XUartNs550 *UartInstancePtr, u16 UartIntrId)
{
    int Status;
    XScuGic_Config *IntcConfig;

    IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);
    if (NULL == IntcConfig) {
        return XST_FAILURE;
    }
    Status = XScuGic_CfgInitialize(IntcInstancePtr, IntcConfig, IntcConfig->CpuBaseAddress);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    XScuGic_SetPriorityTriggerType(IntcInstancePtr, UartIntrId, 0xA0, 0x3);

    Status = XScuGic_Connect(IntcInstancePtr, UartIntrId,
                              (Xil_ExceptionHandler)XUartNs550_InterruptHandler,
                              UartInstancePtr);
    if (Status != XST_SUCCESS) {
        return Status;
    }
    XScuGic_Enable(IntcInstancePtr, UartIntrId);

    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
                                  (Xil_ExceptionHandler)XScuGic_InterruptHandler,
                                  IntcInstancePtr);
    Xil_ExceptionEnable();

    return XST_SUCCESS;
}
#endif /* UART_PC_HAS_INTR */
