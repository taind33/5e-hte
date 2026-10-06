/*******************************************************************************
 * @file    vip_helper.h
 * @brief   Outer framing layer (VIP): sync + header + CRC16 header-CRC +
 *          inner kria_message_t payload + CRC32 payload-CRC.
 *
 *          This is a byte-for-byte protocol port of App/backend/vip_helper.h
 *          / vip_helper.cpp (the Qt/C++ side, already written and compiled
 *          into the dashboard app, but not yet wired into
 *          DashboardManager::sendCommand()/parseIncomingData() - that wiring
 *          is planned for the App-side delivery pass). Every constant,
 *          struct layout and CRC algorithm below MUST stay identical to the
 *          Qt side or the two ends will reject each other's frames.
 *
 *          Frame on the wire (all multi-byte fields little-endian):
 *
 *            offset  size  field
 *            0       2     SYNC            = 0xEB90 (byte0=0x90, byte1=0xEB)
 *            2       1     frameType       (vip_frame_type_t)
 *            3       1     headerSize      = VIP_HEADER_LEN (9)
 *            4       1     control         (bit0 = PING_REQUEST)
 *            5       2     frameSeq
 *            7       2     dataLen         (0 for PING_STD/ACK_STD)
 *            9       1     desId
 *            10      1     srcId
 *            11      2     headerCRC16     CRC16-CCITT-FALSE over [2..10]
 *            13      dataLen  kria payload (seq,msgid,len,payload[len])
 *                             - present only if dataLen > 0
 *            13+dataLen 4  msg_crc32       CRC32-IEEE over the dataLen bytes
 *                             - present only if dataLen > 0
 *
 *          CRC16: CCITT-FALSE, poly 0x1021, init 0xFFFF, no reflect, no
 *                 final xor - identical bit-loop to VIP_CRC16() in
 *                 App/backend/vip_helper.cpp.
 *          CRC32: IEEE 802.3, poly 0xEDB88320 (reflected), init 0xFFFFFFFF,
 *                 final xor 0xFFFFFFFF - identical to VIP_CRC32() there,
 *                 implemented here with a bit-loop instead of a lookup
 *                 table (frames are short and infrequent on this link;
 *                 no ISR-context calls), to avoid a RAM table + lazy-init
 *                 race on a bare-metal single-core app.
 ******************************************************************************/
#ifndef VIP_HELPER_H
#define VIP_HELPER_H

#include <stdint.h>
#include "kria_helper.h"

#define VIP_SYNC_VALUE       0xEB90U
#define VIP_HEADER_LEN       9U
#define VIP_HEADER_CRC_LEN   2U
#define VIP_MSG_CRC_LEN      4U

#define VIP_MIN_FRAME_LEN    (2U + VIP_HEADER_LEN + VIP_HEADER_CRC_LEN)
#define VIP_MAX_MSG_LEN      (3U + 255U)
#define VIP_MAX_FRAME_LEN    (VIP_MIN_FRAME_LEN + VIP_MAX_MSG_LEN + VIP_MSG_CRC_LEN)

typedef enum {
    VIP_FRAME_DATA_STD = 10U,
    VIP_FRAME_PING_STD = 11U,
    VIP_FRAME_ACK_STD  = 12U
} vip_frame_type_t;

#define VIP_CTRL_PING_REQUEST_MASK   0x01U

#ifndef VIP_NODE_ID_SELF
#define VIP_NODE_ID_SELF        0x01U   /* this board's node id                */
#endif
#define VIP_NODE_ID_HOST        0x00U   /* Qt dashboard host                    */
#define VIP_NODE_ID_BROADCAST   0xFFU

#pragma pack(push, 1)

typedef struct {
    uint8_t  frameType;
    uint8_t  headerSize;
    uint8_t  control;
    uint16_t frameSeq;
    uint16_t dataLen;
    uint8_t  desId;
    uint8_t  srcId;
} vip_header_t;

typedef struct {
    uint16_t       SYNC;
    vip_header_t   header;
    uint16_t       headerCRC;
    kria_message_t msg;      /* only meaningful when header.dataLen > 0      */
    uint32_t       msg_crc;
} vip_msg_t;

#pragma pack(pop)

/* ---- encoding ------------------------------------------------------- */
uint32_t VIP_Encode_Data(uint8_t des_id, uint8_t src_id, uint16_t frame_seq,
                          const uint8_t *inner_msg, uint16_t inner_len,
                          uint8_t *out, uint32_t out_cap);
uint32_t VIP_Encode_Ping(uint8_t des_id, uint8_t src_id, uint16_t frame_seq,
                          uint8_t *out, uint32_t out_cap);
uint32_t VIP_Encode_Ack(uint8_t des_id, uint8_t src_id, uint16_t frame_seq,
                         uint8_t *out, uint32_t out_cap);
uint16_t VIP_NextTxSeq(void);

/* ---- decoding FSM ----------------------------------------------------- */
typedef enum {
    VIP_STATE_SYNC_1 = 0,
    VIP_STATE_SYNC_2,
    VIP_STATE_HEADER,
    VIP_STATE_HEADER_CRC,
    VIP_STATE_MSG_PAYLOAD,
    VIP_STATE_MSG_CRC
} vip_state_t;

typedef enum {
    VIP_ERR_NONE = 0,
    VIP_ERR_HEADER_CRC,
    VIP_ERR_MSG_CRC,
    VIP_ERR_BAD_HEADER_SIZE
} vip_decode_error_t;

typedef struct {
    uint8_t             msg_received;
    vip_state_t         parse_state;
    vip_decode_error_t  last_error;

    uint8_t  header_idx;
    uint8_t  header_raw[VIP_HEADER_LEN];

    uint8_t  crc_buf[4];
    uint8_t  crc_idx;

    uint16_t payload_idx;
    uint8_t  msg_raw[VIP_MAX_MSG_LEN];
} vip_status_t;

void    VIP_Decode_Init(vip_status_t *status);
uint8_t VIP_Decode(vip_msg_t *rxmsg, vip_status_t *status, uint8_t byte);

/* ---- routing / auto-ack ------------------------------------------------ */
typedef enum {
    VIP_ROUTE_LOCAL = 0,
    VIP_ROUTE_FORWARD,
    VIP_ROUTE_UNKNOWN
} vip_route_action_t;

vip_route_action_t VIP_Route_Lookup(uint8_t des_id);

typedef enum {
    VIP_PROCESS_IGNORE = 0,
    VIP_PROCESS_FORWARD,
    VIP_PROCESS_REPLY_READY,
    VIP_PROCESS_DATA_READY
} vip_process_result_t;

vip_process_result_t VIP_Process_Frame(const vip_msg_t *rxmsg,
                                        uint8_t *reply_buf, uint32_t reply_cap,
                                        uint32_t *reply_len);

/* ---- CRC primitives (exposed for tooling/tests) ------------------------ */
uint16_t VIP_CRC16(const uint8_t *data, uint32_t len);
uint32_t VIP_CRC32(const uint8_t *data, uint32_t len);

#endif /* VIP_HELPER_H */
