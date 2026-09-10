/*
 *
 *    Copyright (c) 2026 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "LightUartProtocol.h"

#include <string.h>

static void put_u16_le(uint8_t * dst, uint16_t value)
{
    dst[0] = (uint8_t) (value & 0xFFu);
    dst[1] = (uint8_t) ((value >> 8) & 0xFFu);
}

static void put_u32_le(uint8_t * dst, uint32_t value)
{
    dst[0] = (uint8_t) (value & 0xFFu);
    dst[1] = (uint8_t) ((value >> 8) & 0xFFu);
    dst[2] = (uint8_t) ((value >> 16) & 0xFFu);
    dst[3] = (uint8_t) ((value >> 24) & 0xFFu);
}

static uint16_t get_u16_le(const uint8_t * src)
{
    return (uint16_t) (((uint16_t) src[1] << 8) | src[0]);
}

static uint32_t get_u32_le(const uint8_t * src)
{
    return ((uint32_t) src[0]) | ((uint32_t) src[1] << 8) | ((uint32_t) src[2] << 16) | ((uint32_t) src[3] << 24);
}

uint16_t lu_crc16_ccitt_false(const uint8_t * data, size_t len)
{
    uint16_t crc = 0xFFFFu;

    if (data == NULL && len != 0u)
    {
        return 0u;
    }

    for (size_t i = 0; i < len; ++i)
    {
        crc ^= (uint16_t) data[i] << 8;
        for (uint8_t bit = 0; bit < 8u; ++bit)
        {
            if ((crc & 0x8000u) != 0u)
            {
                crc = (uint16_t) ((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc = (uint16_t) (crc << 1);
            }
        }
    }

    return crc;
}

uint32_t lu_crc32_iso_hdlc(const uint8_t * data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;

    if (data == NULL && len != 0u)
    {
        return 0u;
    }

    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8u; ++bit)
        {
            crc = (crc & 1u) != 0u ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }

    return crc ^ 0xFFFFFFFFu;
}

lu_status_t lu_pack_frame(uint8_t type, uint8_t flags, uint16_t seq, uint32_t session_id, uint32_t uart_device_id,
                          uint16_t endpoint, uint32_t binding_version, uint32_t cluster, uint32_t id, const uint8_t * payload,
                          uint16_t payload_len, uint8_t * out, size_t out_cap, size_t * out_len)
{
    const size_t total_len = LU_MIN_FRAME_SIZE + payload_len;
    uint16_t crc;

    if (out == NULL || out_len == NULL || (payload == NULL && payload_len != 0u))
    {
        return LU_ERR_ARG;
    }
    if (payload_len > LU_MAX_PAYLOAD_SIZE)
    {
        return LU_ERR_BAD_LENGTH;
    }
    if (out_cap < total_len)
    {
        return LU_ERR_NO_SPACE;
    }

    out[0] = LU_SOF0;
    out[1] = LU_SOF1;
    out[2] = LU_VERSION;
    out[3] = type;
    out[4] = flags;
    put_u16_le(&out[5], seq);
    put_u32_le(&out[7], session_id);
    put_u32_le(&out[11], uart_device_id);
    put_u16_le(&out[15], endpoint);
    put_u32_le(&out[17], binding_version);
    put_u32_le(&out[21], cluster);
    put_u32_le(&out[25], id);
    put_u16_le(&out[29], payload_len);
    if (payload_len != 0u)
    {
        memcpy(&out[31], payload, payload_len);
    }

    crc = lu_crc16_ccitt_false(&out[2], LU_CRC_HEADER_SIZE + payload_len);
    put_u16_le(&out[31u + payload_len], crc);
    out[33u + payload_len] = LU_EOF0;
    out[34u + payload_len] = LU_EOF1;
    *out_len               = total_len;

    return LU_OK;
}

static bool lu_is_response_type(uint8_t type)
{
    switch (type)
    {
    case LU_MSG_DOWN_COMMAND_RESPONSE:
    case LU_MSG_DOWN_READ_ATTRIBUTE_RESPONSE:
    case LU_MSG_HELLO_RESPONSE:
    case LU_MSG_DEVICE_ADD_RESPONSE:
    case LU_MSG_DEVICE_REMOVE_RESPONSE:
    case LU_MSG_DEVICE_BIND_RESPONSE:
    case LU_MSG_DEVICE_ONLINE_RESPONSE:
    case LU_MSG_DEVICE_OFFLINE_RESPONSE:
    case LU_MSG_STATE_SNAPSHOT_RESPONSE:
    case LU_MSG_HEARTBEAT_RESPONSE:
    case LU_MSG_DEVICE_STATE_RESPONSE:
        return true;
    default:
        return false;
    }
}

lu_status_t lu_validate_frame_semantics(const lu_frame_t * frame)
{
    if (frame == NULL)
    {
        return LU_ERR_RESERVED_BITS;
    }
    if (frame->seq == 0u)
    {
        return LU_ERR_BAD_PAYLOAD;
    }

    switch (frame->type)
    {
    case LU_MSG_UP_ATTRIBUTE_REPORT:
    case LU_MSG_DEVICE_LIST_BEGIN:
    case LU_MSG_DEVICE_LIST_ENTRY:
    case LU_MSG_DEVICE_LIST_END:
        if (frame->type == LU_MSG_DEVICE_LIST_END)
        {
            return (frame->flags & 0x3Fu & ~(uint8_t) LU_FLAG_ERROR) == 0u ? LU_OK : LU_ERR_BAD_PAYLOAD;
        }
        return (frame->flags & 0x3Fu) == 0u ? LU_OK : LU_ERR_BAD_PAYLOAD;
    default:
        break;
    }

    if (lu_is_response_type(frame->type))
    {
        return (frame->flags & LU_FLAG_RESPONSE) != 0u &&
                (frame->flags & (LU_FLAG_ACK_REQUIRED | LU_FLAG_NULL_VALUE | LU_FLAG_MORE)) == 0u
            ? LU_OK
            : LU_ERR_BAD_PAYLOAD;
    }

    switch (frame->type)
    {
    case LU_MSG_DOWN_INVOKE_COMMAND:
    case LU_MSG_DOWN_READ_ATTRIBUTE:
    case LU_MSG_HELLO_REQUEST:
    case LU_MSG_DEVICE_LIST_REQUEST:
    case LU_MSG_DEVICE_ADD_NOTIFY:
    case LU_MSG_DEVICE_REMOVE_NOTIFY:
    case LU_MSG_DEVICE_BIND_REQUEST:
    case LU_MSG_DEVICE_ONLINE_NOTIFY:
    case LU_MSG_DEVICE_OFFLINE_NOTIFY:
    case LU_MSG_HEARTBEAT:
    case LU_MSG_DEVICE_STATE_REQUEST:
    case LU_MSG_UP_STATE_SNAPSHOT:
        return (frame->flags & LU_FLAG_ACK_REQUIRED) != 0u &&
                (frame->flags & (LU_FLAG_RESPONSE | LU_FLAG_ERROR | LU_FLAG_NULL_VALUE | LU_FLAG_MORE)) == 0u
            ? LU_OK
            : LU_ERR_BAD_PAYLOAD;
    default:
        return LU_ERR_BAD_PAYLOAD;
    }
}

lu_status_t lu_unpack_frame(const uint8_t * frame, size_t frame_len, lu_frame_t * out)
{
    uint16_t payload_len;
    uint16_t expected_crc;
    uint16_t actual_crc;
    size_t expected_len;

    if (frame == NULL || out == NULL)
    {
        return LU_ERR_ARG;
    }
    if (frame_len < LU_MIN_FRAME_SIZE)
    {
        return LU_ERR_BAD_LENGTH;
    }
    if (frame[0] != LU_SOF0 || frame[1] != LU_SOF1)
    {
        return LU_ERR_BAD_SOF;
    }
    if (frame[2] != LU_VERSION)
    {
        return LU_ERR_VERSION;
    }

    payload_len  = get_u16_le(&frame[29]);
    expected_len = LU_MIN_FRAME_SIZE + payload_len;
    if (payload_len > LU_MAX_PAYLOAD_SIZE || frame_len != expected_len)
    {
        return LU_ERR_BAD_LENGTH;
    }
    if (frame[33u + payload_len] != LU_EOF0 || frame[34u + payload_len] != LU_EOF1)
    {
        return LU_ERR_BAD_EOF;
    }

    expected_crc = get_u16_le(&frame[31u + payload_len]);
    actual_crc   = lu_crc16_ccitt_false(&frame[2], LU_CRC_HEADER_SIZE + payload_len);
    if (actual_crc != expected_crc)
    {
        return LU_ERR_CRC;
    }

    out->type            = frame[3];
    out->flags           = frame[4];
    out->seq             = get_u16_le(&frame[5]);
    out->session_id      = get_u32_le(&frame[7]);
    out->uart_device_id  = get_u32_le(&frame[11]);
    out->endpoint        = get_u16_le(&frame[15]);
    out->binding_version = get_u32_le(&frame[17]);
    out->cluster         = get_u32_le(&frame[21]);
    out->id              = get_u32_le(&frame[25]);
    out->payload_len     = payload_len;
    out->payload         = payload_len == 0u ? NULL : &frame[31];

    lu_status_t semantic_status = lu_validate_frame_semantics(out);
    if (semantic_status != LU_OK)
    {
        return semantic_status;
    }

    return LU_OK;
}

void lu_payload_writer_init(lu_payload_writer_t * writer, uint8_t * buf, size_t cap)
{
    if (writer == NULL)
    {
        return;
    }

    writer->buf = buf;
    writer->cap = cap;
    writer->len = 0u;
}

static lu_status_t payload_add_raw(lu_payload_writer_t * writer, const uint8_t * value, size_t value_len)
{
    if (writer == NULL || writer->buf == NULL || (value == NULL && value_len != 0u))
    {
        return LU_ERR_ARG;
    }
    if (writer->len > writer->cap || writer->cap - writer->len < value_len)
    {
        return LU_ERR_NO_SPACE;
    }

    if (value_len != 0u)
    {
        memcpy(&writer->buf[writer->len], value, value_len);
        writer->len += value_len;
    }
    return LU_OK;
}

lu_status_t lu_payload_add_u8(lu_payload_writer_t * writer, uint8_t value)
{
    return payload_add_raw(writer, &value, sizeof(value));
}

lu_status_t lu_payload_add_u16(lu_payload_writer_t * writer, uint16_t value)
{
    uint8_t v[2];
    put_u16_le(v, value);
    return payload_add_raw(writer, v, sizeof(v));
}

lu_status_t lu_payload_add_u32(lu_payload_writer_t * writer, uint32_t value)
{
    uint8_t v[4];
    put_u32_le(v, value);
    return payload_add_raw(writer, v, sizeof(v));
}

lu_status_t lu_payload_add_i16(lu_payload_writer_t * writer, int16_t value)
{
    return lu_payload_add_u16(writer, (uint16_t) value);
}

void lu_payload_reader_init(lu_payload_reader_t * reader, const uint8_t * payload, size_t payload_len)
{
    if (reader == NULL)
    {
        return;
    }

    reader->buf    = payload;
    reader->len    = payload_len;
    reader->offset = 0u;
}

static bool payload_get_raw(lu_payload_reader_t * reader, uint8_t * out, size_t out_len)
{
    if (reader == NULL || out == NULL || (reader->buf == NULL && out_len != 0u))
    {
        return false;
    }
    if (reader->offset > reader->len || reader->len - reader->offset < out_len)
    {
        return false;
    }

    if (out_len != 0u)
    {
        memcpy(out, &reader->buf[reader->offset], out_len);
        reader->offset += out_len;
    }
    return true;
}

bool lu_payload_get_u8(lu_payload_reader_t * reader, uint8_t * out)
{
    return payload_get_raw(reader, out, sizeof(*out));
}

bool lu_payload_get_u16(lu_payload_reader_t * reader, uint16_t * out)
{
    uint8_t v[2];
    if (out == NULL || !payload_get_raw(reader, v, sizeof(v)))
    {
        return false;
    }

    *out = get_u16_le(v);
    return true;
}

bool lu_payload_get_u32(lu_payload_reader_t * reader, uint32_t * out)
{
    uint8_t v[4];
    if (out == NULL || !payload_get_raw(reader, v, sizeof(v)))
    {
        return false;
    }

    *out = get_u32_le(v);
    return true;
}

bool lu_payload_get_i16(lu_payload_reader_t * reader, int16_t * out)
{
    uint16_t v;
    if (out == NULL || !lu_payload_get_u16(reader, &v))
    {
        return false;
    }

    *out = (int16_t) v;
    return true;
}

bool lu_payload_reader_done(const lu_payload_reader_t * reader)
{
    return reader != NULL && reader->offset == reader->len;
}
