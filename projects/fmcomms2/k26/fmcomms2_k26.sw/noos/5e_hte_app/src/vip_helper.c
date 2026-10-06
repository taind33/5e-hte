/*******************************************************************************
 * @file    vip_helper.c
 * @brief   See vip_helper.h. Kept hardware-agnostic (no BSP includes) so it
 *          can be exercised by the host-side cross-check test in
 *          tools/vip_crosscheck (see README.md) against the Qt-side
 *          implementation, to prove wire-compatibility before ever touching
 *          real UART hardware.
 ******************************************************************************/
#include "vip_helper.h"
#include <string.h>

/* ---- little-endian pack/unpack ---------------------------------------- */
static void PutU16LE(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}
static void PutU32LE(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}
static uint16_t GetU16LE(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t GetU32LE(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- CRC16: CCITT-FALSE (poly 0x1021, init 0xFFFF) --------------------
 * Identical algorithm/result to App/backend/vip_helper.cpp VIP_CRC16(). */
uint16_t VIP_CRC16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    int b;

    if (data == 0) {
        return 0U;
    }
    for (i = 0U; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (b = 0; b < 8; b++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* ---- CRC32: IEEE 802.3 (poly 0xEDB88320 reflected, init/final 0xFFFFFFFF)
 * Bit-loop form - numerically identical to the table-based
 * App/backend/vip_helper.cpp VIP_CRC32() (same reflected recurrence),
 * traded here for zero static RAM table / no lazy-init on a bare-metal
 * single-core app. */
uint32_t VIP_CRC32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t i;
    int b;

    if (data == 0) {
        return 0U;
    }
    for (i = 0U; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (b = 0; b < 8; b++) {
            if ((crc & 1U) != 0U) {
                crc = (crc >> 1) ^ 0xEDB88320U;
            } else {
                crc = crc >> 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

/* ---- encoding ----------------------------------------------------------- */
static uint32_t SerializeHeader(const vip_header_t *h, uint8_t *out)
{
    out[0] = h->frameType;
    out[1] = h->headerSize;
    out[2] = h->control;
    PutU16LE(&out[3], h->frameSeq);
    PutU16LE(&out[5], h->dataLen);
    out[7] = h->desId;
    out[8] = h->srcId;
    return VIP_HEADER_LEN;
}

static uint32_t EncodeFrame(vip_frame_type_t type, uint8_t control,
                             uint8_t des_id, uint8_t src_id, uint16_t frame_seq,
                             const uint8_t *inner_msg, uint16_t inner_len,
                             uint8_t *out, uint32_t out_cap)
{
    vip_header_t h;
    uint16_t     hcrc;
    uint32_t     total;

    if ((out == 0) || ((inner_len > 0U) && (inner_msg == 0))) {
        return 0U;
    }
    if (inner_len > (uint16_t)VIP_MAX_MSG_LEN) {
        return 0U;
    }

    total = VIP_MIN_FRAME_LEN + ((inner_len > 0U) ? ((uint32_t)inner_len + VIP_MSG_CRC_LEN) : 0U);
    if (out_cap < total) {
        return 0U;
    }

    PutU16LE(&out[0], VIP_SYNC_VALUE);

    h.frameType  = (uint8_t)type;
    h.headerSize = VIP_HEADER_LEN;
    h.control    = control;
    h.frameSeq   = frame_seq;
    h.dataLen    = inner_len;
    h.desId      = des_id;
    h.srcId      = src_id;
    (void)SerializeHeader(&h, &out[2]);

    hcrc = VIP_CRC16(&out[2], VIP_HEADER_LEN);
    PutU16LE(&out[2U + VIP_HEADER_LEN], hcrc);

    if (inner_len > 0U) {
        uint32_t msg_off = 2U + VIP_HEADER_LEN + VIP_HEADER_CRC_LEN;
        uint32_t mcrc;

        memcpy(&out[msg_off], inner_msg, inner_len);
        mcrc = VIP_CRC32(&out[msg_off], inner_len);
        PutU32LE(&out[msg_off + inner_len], mcrc);
    }

    return total;
}

uint32_t VIP_Encode_Data(uint8_t des_id, uint8_t src_id, uint16_t frame_seq,
                          const uint8_t *inner_msg, uint16_t inner_len,
                          uint8_t *out, uint32_t out_cap)
{
    return EncodeFrame(VIP_FRAME_DATA_STD, 0U, des_id, src_id, frame_seq,
                        inner_msg, inner_len, out, out_cap);
}

uint32_t VIP_Encode_Ping(uint8_t des_id, uint8_t src_id, uint16_t frame_seq,
                          uint8_t *out, uint32_t out_cap)
{
    return EncodeFrame(VIP_FRAME_PING_STD, VIP_CTRL_PING_REQUEST_MASK,
                        des_id, src_id, frame_seq, 0, 0U, out, out_cap);
}

uint32_t VIP_Encode_Ack(uint8_t des_id, uint8_t src_id, uint16_t frame_seq,
                         uint8_t *out, uint32_t out_cap)
{
    return EncodeFrame(VIP_FRAME_ACK_STD, 0U, des_id, src_id, frame_seq,
                        0, 0U, out, out_cap);
}

uint16_t VIP_NextTxSeq(void)
{
    static uint16_t s_tx_seq = 0U;
    return s_tx_seq++;
}

/* ---- decoding FSM -------------------------------------------------------- */
static void ResetToSync(vip_status_t *status)
{
    status->parse_state = VIP_STATE_SYNC_1;
    status->header_idx  = 0U;
    status->crc_idx      = 0U;
    status->payload_idx  = 0U;
}

void VIP_Decode_Init(vip_status_t *status)
{
    if (status == 0) {
        return;
    }
    status->msg_received = 0U;
    status->parse_state  = VIP_STATE_SYNC_1;
    status->last_error   = VIP_ERR_NONE;
    status->header_idx   = 0U;
    status->crc_idx       = 0U;
    status->payload_idx   = 0U;
    memset(status->header_raw, 0, sizeof(status->header_raw));
    memset(status->crc_buf, 0, sizeof(status->crc_buf));
}

uint8_t VIP_Decode(vip_msg_t *rxmsg, vip_status_t *status, uint8_t byte)
{
    if ((rxmsg == 0) || (status == 0)) {
        return 0U;
    }
    status->msg_received = 0U;

    switch (status->parse_state) {
    case VIP_STATE_SYNC_1:
        if (byte == (uint8_t)(VIP_SYNC_VALUE & 0xFFU)) {
            status->parse_state = VIP_STATE_SYNC_2;
        }
        break;

    case VIP_STATE_SYNC_2:
        if (byte == (uint8_t)((VIP_SYNC_VALUE >> 8) & 0xFFU)) {
            rxmsg->SYNC = VIP_SYNC_VALUE;
            status->header_idx  = 0U;
            status->parse_state = VIP_STATE_HEADER;
        } else if (byte == (uint8_t)(VIP_SYNC_VALUE & 0xFFU)) {
            status->parse_state = VIP_STATE_SYNC_2;
        } else {
            status->parse_state = VIP_STATE_SYNC_1;
        }
        break;

    case VIP_STATE_HEADER:
        status->header_raw[status->header_idx++] = byte;
        if (status->header_idx >= VIP_HEADER_LEN) {
            rxmsg->header.frameType  = status->header_raw[0];
            rxmsg->header.headerSize = status->header_raw[1];
            rxmsg->header.control    = status->header_raw[2];
            rxmsg->header.frameSeq   = GetU16LE(&status->header_raw[3]);
            rxmsg->header.dataLen    = GetU16LE(&status->header_raw[5]);
            rxmsg->header.desId      = status->header_raw[7];
            rxmsg->header.srcId      = status->header_raw[8];

            if (rxmsg->header.headerSize != VIP_HEADER_LEN) {
                status->last_error = VIP_ERR_BAD_HEADER_SIZE;
                ResetToSync(status);
                break;
            }
            if (rxmsg->header.dataLen > (uint16_t)VIP_MAX_MSG_LEN) {
                status->last_error = VIP_ERR_BAD_HEADER_SIZE;
                ResetToSync(status);
                break;
            }
            status->crc_idx     = 0U;
            status->parse_state = VIP_STATE_HEADER_CRC;
        }
        break;

    case VIP_STATE_HEADER_CRC:
        status->crc_buf[status->crc_idx++] = byte;
        if (status->crc_idx >= VIP_HEADER_CRC_LEN) {
            uint16_t received = GetU16LE(status->crc_buf);
            uint16_t expected = VIP_CRC16(status->header_raw, VIP_HEADER_LEN);

            if (received != expected) {
                status->last_error = VIP_ERR_HEADER_CRC;
                ResetToSync(status);
                break;
            }
            rxmsg->headerCRC = received;

            if (rxmsg->header.dataLen == 0U) {
                rxmsg->msg.seq   = 0U;
                rxmsg->msg.msgid = 0U;
                rxmsg->msg.len   = 0U;
                rxmsg->msg_crc   = 0U;
                status->msg_received = 1U;
                ResetToSync(status);
                return 1U;
            }
            if (rxmsg->header.dataLen < 3U) {
                status->last_error = VIP_ERR_BAD_HEADER_SIZE;
                ResetToSync(status);
                break;
            }
            status->payload_idx = 0U;
            status->parse_state = VIP_STATE_MSG_PAYLOAD;
        }
        break;

    case VIP_STATE_MSG_PAYLOAD:
        status->msg_raw[status->payload_idx++] = byte;
        if (status->payload_idx >= rxmsg->header.dataLen) {
            rxmsg->msg.seq   = status->msg_raw[0];
            rxmsg->msg.msgid = status->msg_raw[1];
            rxmsg->msg.len   = status->msg_raw[2];

            if (((uint32_t)3U + (uint32_t)rxmsg->msg.len) == (uint32_t)rxmsg->header.dataLen) {
                memcpy(rxmsg->msg.payload64, &status->msg_raw[3], rxmsg->msg.len);
            }
            status->crc_idx     = 0U;
            status->parse_state = VIP_STATE_MSG_CRC;
        }
        break;

    case VIP_STATE_MSG_CRC:
        status->crc_buf[status->crc_idx++] = byte;
        if (status->crc_idx >= VIP_MSG_CRC_LEN) {
            uint32_t received = GetU32LE(status->crc_buf);
            uint32_t expected = VIP_CRC32(status->msg_raw, rxmsg->header.dataLen);

            if (received != expected) {
                status->last_error = VIP_ERR_MSG_CRC;
                ResetToSync(status);
                break;
            }
            rxmsg->msg_crc       = received;
            status->msg_received = 1U;
            ResetToSync(status);
            return 1U;
        }
        break;

    default:
        ResetToSync(status);
        break;
    }
    return 0U;
}

/* ---- routing / auto-ack --------------------------------------------------- */
typedef struct {
    uint8_t node_id;
    uint8_t is_local;
} vip_route_entry_t;

static const vip_route_entry_t g_vip_route_table[] = {
    { VIP_NODE_ID_SELF, 1U },
    { VIP_NODE_ID_HOST, 0U },
};
#define VIP_ROUTE_TABLE_SIZE  (sizeof(g_vip_route_table) / sizeof(g_vip_route_table[0]))

vip_route_action_t VIP_Route_Lookup(uint8_t des_id)
{
    uint32_t i;

    if (des_id == VIP_NODE_ID_BROADCAST) {
        return VIP_ROUTE_LOCAL;
    }
    for (i = 0U; i < VIP_ROUTE_TABLE_SIZE; i++) {
        if (g_vip_route_table[i].node_id == des_id) {
            return (g_vip_route_table[i].is_local != 0U) ? VIP_ROUTE_LOCAL : VIP_ROUTE_FORWARD;
        }
    }
    return VIP_ROUTE_UNKNOWN;
}

vip_process_result_t VIP_Process_Frame(const vip_msg_t *rxmsg,
                                        uint8_t *reply_buf, uint32_t reply_cap,
                                        uint32_t *reply_len)
{
    vip_route_action_t route;

    if (rxmsg == 0) {
        return VIP_PROCESS_IGNORE;
    }

    route = VIP_Route_Lookup(rxmsg->header.desId);
    if (route == VIP_ROUTE_UNKNOWN) {
        return VIP_PROCESS_IGNORE;
    }
    if (route == VIP_ROUTE_FORWARD) {
        return VIP_PROCESS_FORWARD;
    }

    switch ((vip_frame_type_t)rxmsg->header.frameType) {
    case VIP_FRAME_PING_STD:
        if ((rxmsg->header.control & VIP_CTRL_PING_REQUEST_MASK) != 0U) {
            uint32_t len = VIP_Encode_Ack(rxmsg->header.srcId, VIP_NODE_ID_SELF,
                                           rxmsg->header.frameSeq,
                                           reply_buf, reply_cap);
            if (len == 0U) {
                return VIP_PROCESS_IGNORE;
            }
            if (reply_len != 0) {
                *reply_len = len;
            }
            return VIP_PROCESS_REPLY_READY;
        }
        return VIP_PROCESS_IGNORE;

    case VIP_FRAME_ACK_STD:
        return VIP_PROCESS_IGNORE;

    case VIP_FRAME_DATA_STD:
        return VIP_PROCESS_DATA_READY;

    default:
        return VIP_PROCESS_IGNORE;
    }
}
