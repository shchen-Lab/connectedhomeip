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

lu_status_t lu_pack_frame(uint8_t type, uint8_t flags, uint16_t seq, uint16_t endpoint, uint32_t cluster, uint32_t id,
                          const uint8_t * payload, uint16_t payload_len, uint8_t * out, size_t out_cap, size_t * out_len)
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
    put_u16_le(&out[7], endpoint);
    put_u32_le(&out[9], cluster);
    put_u32_le(&out[13], id);
    put_u16_le(&out[17], payload_len);
    if (payload_len != 0u)
    {
        memcpy(&out[19], payload, payload_len);
    }

    crc = lu_crc16_ccitt_false(&out[2], LU_CRC_HEADER_SIZE + payload_len);
    put_u16_le(&out[19u + payload_len], crc);
    out[21u + payload_len] = LU_EOF0;
    out[22u + payload_len] = LU_EOF1;
    *out_len              = total_len;

    return LU_OK;
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

    payload_len  = get_u16_le(&frame[17]);
    expected_len = LU_MIN_FRAME_SIZE + payload_len;
    if (payload_len > LU_MAX_PAYLOAD_SIZE || frame_len != expected_len)
    {
        return LU_ERR_BAD_LENGTH;
    }
    if (frame[21u + payload_len] != LU_EOF0 || frame[22u + payload_len] != LU_EOF1)
    {
        return LU_ERR_BAD_EOF;
    }

    expected_crc = get_u16_le(&frame[19u + payload_len]);
    actual_crc   = lu_crc16_ccitt_false(&frame[2], LU_CRC_HEADER_SIZE + payload_len);
    if (actual_crc != expected_crc)
    {
        return LU_ERR_CRC;
    }

    out->type        = frame[3];
    out->flags       = frame[4];
    out->seq         = get_u16_le(&frame[5]);
    out->endpoint    = get_u16_le(&frame[7]);
    out->cluster     = get_u32_le(&frame[9]);
    out->id          = get_u32_le(&frame[13]);
    out->payload_len = payload_len;
    out->payload     = payload_len == 0u ? NULL : &frame[19];

    return LU_OK;
}

void lu_field_writer_init(lu_field_writer_t * writer, uint8_t * buf, size_t cap)
{
    if (writer == NULL)
    {
        return;
    }

    writer->buf   = buf;
    writer->cap   = cap;
    writer->len   = 0u;
    writer->count = 0u;

    if (buf != NULL && cap != 0u)
    {
        buf[0]       = 0u;
        writer->len  = 1u;
        writer->count = 0u;
    }
}

lu_status_t lu_field_add_raw(lu_field_writer_t * writer, uint8_t field_id, uint8_t value_type, uint8_t value_flags,
                             const uint8_t * value, uint16_t value_len)
{
    if (writer == NULL || writer->buf == NULL || (value == NULL && value_len != 0u))
    {
        return LU_ERR_ARG;
    }
    if ((value_flags & LU_VALUE_FLAG_NULL) != 0u && value_len != 0u)
    {
        return LU_ERR_ARG;
    }
    if (writer->count == 0xFFu)
    {
        return LU_ERR_NO_SPACE;
    }
    if (writer->len > writer->cap)
    {
        return LU_ERR_BAD_PAYLOAD;
    }
    if (writer->cap - writer->len < (size_t) (5u + value_len))
    {
        return LU_ERR_NO_SPACE;
    }

    writer->buf[writer->len++] = field_id;
    writer->buf[writer->len++] = value_type;
    writer->buf[writer->len++] = value_flags;
    put_u16_le(&writer->buf[writer->len], value_len);
    writer->len += 2u;
    if (value_len != 0u)
    {
        memcpy(&writer->buf[writer->len], value, value_len);
        writer->len += value_len;
    }

    writer->buf[0] = ++writer->count;
    return LU_OK;
}

lu_status_t lu_field_add_null(lu_field_writer_t * writer, uint8_t field_id, uint8_t value_type)
{
    return lu_field_add_raw(writer, field_id, value_type, LU_VALUE_FLAG_NULL, NULL, 0u);
}

lu_status_t lu_field_add_bool(lu_field_writer_t * writer, uint8_t field_id, bool value)
{
    const uint8_t v = value ? 1u : 0u;
    return lu_field_add_raw(writer, field_id, LU_VT_BOOL, 0u, &v, sizeof(v));
}

lu_status_t lu_field_add_u8(lu_field_writer_t * writer, uint8_t field_id, uint8_t value)
{
    return lu_field_add_raw(writer, field_id, LU_VT_U8, 0u, &value, sizeof(value));
}

lu_status_t lu_field_add_u16(lu_field_writer_t * writer, uint8_t field_id, uint16_t value)
{
    uint8_t v[2];
    put_u16_le(v, value);
    return lu_field_add_raw(writer, field_id, LU_VT_U16, 0u, v, sizeof(v));
}

lu_status_t lu_field_add_u32(lu_field_writer_t * writer, uint8_t field_id, uint32_t value)
{
    uint8_t v[4];
    put_u32_le(v, value);
    return lu_field_add_raw(writer, field_id, LU_VT_U32, 0u, v, sizeof(v));
}

lu_status_t lu_field_add_i8(lu_field_writer_t * writer, uint8_t field_id, int8_t value)
{
    const uint8_t v = (uint8_t) value;
    return lu_field_add_raw(writer, field_id, LU_VT_I8, 0u, &v, sizeof(v));
}

lu_status_t lu_field_add_i16(lu_field_writer_t * writer, uint8_t field_id, int16_t value)
{
    uint8_t v[2];
    put_u16_le(v, (uint16_t) value);
    return lu_field_add_raw(writer, field_id, LU_VT_I16, 0u, v, sizeof(v));
}

lu_status_t lu_field_add_enum8(lu_field_writer_t * writer, uint8_t field_id, uint8_t value)
{
    return lu_field_add_raw(writer, field_id, LU_VT_ENUM8, 0u, &value, sizeof(value));
}

lu_status_t lu_field_add_bitmap8(lu_field_writer_t * writer, uint8_t field_id, uint8_t value)
{
    return lu_field_add_raw(writer, field_id, LU_VT_BITMAP8, 0u, &value, sizeof(value));
}

lu_status_t lu_field_add_bitmap16(lu_field_writer_t * writer, uint8_t field_id, uint16_t value)
{
    uint8_t v[2];
    put_u16_le(v, value);
    return lu_field_add_raw(writer, field_id, LU_VT_BITMAP16, 0u, v, sizeof(v));
}

lu_status_t lu_field_add_bitmap32(lu_field_writer_t * writer, uint8_t field_id, uint32_t value)
{
    uint8_t v[4];
    put_u32_le(v, value);
    return lu_field_add_raw(writer, field_id, LU_VT_BITMAP32, 0u, v, sizeof(v));
}

lu_status_t lu_field_reader_init(lu_field_reader_t * reader, const uint8_t * payload, size_t payload_len)
{
    if (reader == NULL || payload == NULL || payload_len == 0u)
    {
        return LU_ERR_ARG;
    }
    if (payload[0] == 0u && payload_len != 1u)
    {
        return LU_ERR_BAD_PAYLOAD;
    }

    reader->buf    = payload;
    reader->len    = payload_len;
    reader->offset = 1u;
    reader->count  = payload[0];
    reader->index  = 0u;

    return LU_OK;
}

lu_status_t lu_field_next(lu_field_reader_t * reader, lu_field_t * field)
{
    uint16_t value_len;
    size_t field_offset;
    size_t value_offset;

    if (reader == NULL || field == NULL)
    {
        return LU_ERR_ARG;
    }
    if (reader->index >= reader->count)
    {
        return LU_ERR_BAD_PAYLOAD;
    }
    if (reader->offset > reader->len)
    {
        return LU_ERR_BAD_PAYLOAD;
    }
    if (reader->len - reader->offset < 5u)
    {
        return LU_ERR_BAD_PAYLOAD;
    }

    field_offset        = reader->offset;
    field->field_id    = reader->buf[reader->offset++];
    field->value_type  = reader->buf[reader->offset++];
    field->value_flags = reader->buf[reader->offset++];
    value_len          = get_u16_le(&reader->buf[reader->offset]);
    reader->offset += 2u;
    value_offset = reader->offset;

    if ((field->value_flags & LU_VALUE_FLAG_NULL) != 0u && value_len != 0u)
    {
        reader->offset = field_offset;
        return LU_ERR_BAD_PAYLOAD;
    }
    if (reader->len - reader->offset < value_len)
    {
        reader->offset = field_offset;
        return LU_ERR_BAD_PAYLOAD;
    }

    field->value_len = value_len;
    field->value     = value_len == 0u ? NULL : &reader->buf[value_offset];
    reader->offset += value_len;
    reader->index++;

    return LU_OK;
}

bool lu_field_reader_done(const lu_field_reader_t * reader)
{
    return reader != NULL && reader->index == reader->count && reader->offset == reader->len;
}

static lu_status_t pack_attr_raw(uint8_t value_type, uint8_t value_flags, const uint8_t * value, uint16_t value_len,
                                 uint8_t * out, size_t out_cap, size_t * out_len)
{
    if (out == NULL || out_len == NULL || (value == NULL && value_len != 0u))
    {
        return LU_ERR_ARG;
    }
    if ((value_flags & LU_VALUE_FLAG_NULL) != 0u && value_len != 0u)
    {
        return LU_ERR_ARG;
    }
    if (out_cap < (size_t) (4u + value_len))
    {
        return LU_ERR_NO_SPACE;
    }

    out[0] = value_type;
    out[1] = value_flags;
    put_u16_le(&out[2], value_len);
    if (value_len != 0u)
    {
        memcpy(&out[4], value, value_len);
    }
    *out_len = 4u + value_len;
    return LU_OK;
}

lu_status_t lu_pack_attr_bool(bool value, uint8_t * out, size_t out_cap, size_t * out_len)
{
    const uint8_t v = value ? 1u : 0u;
    return pack_attr_raw(LU_VT_BOOL, 0u, &v, sizeof(v), out, out_cap, out_len);
}

lu_status_t lu_pack_attr_u8(uint8_t value_type, uint8_t value, uint8_t * out, size_t out_cap, size_t * out_len)
{
    return pack_attr_raw(value_type, 0u, &value, sizeof(value), out, out_cap, out_len);
}

lu_status_t lu_pack_attr_u16(uint8_t value_type, uint16_t value, uint8_t * out, size_t out_cap, size_t * out_len)
{
    uint8_t v[2];
    put_u16_le(v, value);
    return pack_attr_raw(value_type, 0u, v, sizeof(v), out, out_cap, out_len);
}

lu_status_t lu_pack_attr_null(uint8_t value_type, uint8_t * out, size_t out_cap, size_t * out_len)
{
    return pack_attr_raw(value_type, LU_VALUE_FLAG_NULL, NULL, 0u, out, out_cap, out_len);
}

lu_status_t lu_unpack_attr(const uint8_t * payload, size_t payload_len, lu_attr_value_t * out)
{
    uint16_t value_len;

    if (payload == NULL || out == NULL)
    {
        return LU_ERR_ARG;
    }
    if (payload_len < 4u)
    {
        return LU_ERR_BAD_LENGTH;
    }

    value_len = get_u16_le(&payload[2]);
    if ((payload[1] & LU_VALUE_FLAG_NULL) != 0u && value_len != 0u)
    {
        return LU_ERR_BAD_PAYLOAD;
    }
    if (payload_len != (size_t) (4u + value_len))
    {
        return LU_ERR_BAD_LENGTH;
    }

    out->value_type  = payload[0];
    out->value_flags = payload[1];
    out->value_len   = value_len;
    out->value       = value_len == 0u ? NULL : &payload[4];

    return LU_OK;
}

bool lu_field_get_u8(const lu_field_t * field, uint8_t expected_type, uint8_t * out)
{
    if (field == NULL || out == NULL || field->value_type != expected_type || field->value_len != 1u ||
        (field->value_flags & LU_VALUE_FLAG_NULL) != 0u)
    {
        return false;
    }
    *out = field->value[0];
    return true;
}

bool lu_field_get_u16(const lu_field_t * field, uint8_t expected_type, uint16_t * out)
{
    if (field == NULL || out == NULL || field->value_type != expected_type || field->value_len != 2u ||
        (field->value_flags & LU_VALUE_FLAG_NULL) != 0u)
    {
        return false;
    }
    *out = get_u16_le(field->value);
    return true;
}

bool lu_attr_get_bool(const lu_attr_value_t * attr, bool * out)
{
    if (attr == NULL || out == NULL || attr->value_type != LU_VT_BOOL || attr->value_len != 1u ||
        (attr->value_flags & LU_VALUE_FLAG_NULL) != 0u)
    {
        return false;
    }
    *out = attr->value[0] != 0u;
    return true;
}

bool lu_attr_get_u8(const lu_attr_value_t * attr, uint8_t expected_type, uint8_t * out)
{
    if (attr == NULL || out == NULL || attr->value_type != expected_type || attr->value_len != 1u ||
        (attr->value_flags & LU_VALUE_FLAG_NULL) != 0u)
    {
        return false;
    }
    *out = attr->value[0];
    return true;
}

bool lu_attr_get_u16(const lu_attr_value_t * attr, uint8_t expected_type, uint16_t * out)
{
    if (attr == NULL || out == NULL || attr->value_type != expected_type || attr->value_len != 2u ||
        (attr->value_flags & LU_VALUE_FLAG_NULL) != 0u)
    {
        return false;
    }
    *out = get_u16_le(attr->value);
    return true;
}
