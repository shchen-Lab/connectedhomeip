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

#ifndef LIGHT_UART_PROTO_H
#define LIGHT_UART_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LU_SOF0 0xA5u
#define LU_SOF1 0x5Au
#define LU_EOF0 0x0Du
#define LU_EOF1 0x0Au
#define LU_VERSION 0x01u

#define LU_HEADER_SIZE 19u
#define LU_CRC_HEADER_SIZE 17u
#define LU_TRAILER_SIZE 4u
#define LU_MIN_FRAME_SIZE (LU_HEADER_SIZE + LU_TRAILER_SIZE)
#define LU_MAX_PAYLOAD_SIZE 1024u

typedef enum
{
    LU_MSG_DOWN_INVOKE_COMMAND = 0x10,
    LU_MSG_DOWN_READ_ATTRIBUTE = 0x11,
    LU_MSG_UP_ATTRIBUTE_REPORT = 0x20,
} lu_msg_type_t;

typedef enum
{
    LU_FLAG_RESPONSE_TO_READ = 1u << 1,
    LU_FLAG_ERROR            = 1u << 2,
} lu_flags_t;

typedef enum
{
    LU_VT_BOOL     = 0x01,
    LU_VT_U8       = 0x02,
    LU_VT_U16      = 0x03,
    LU_VT_U32      = 0x04,
    LU_VT_I8       = 0x05,
    LU_VT_I16      = 0x06,
    LU_VT_ENUM8    = 0x10,
    LU_VT_BITMAP8  = 0x20,
    LU_VT_BITMAP16 = 0x21,
    LU_VT_BITMAP32 = 0x22,
} lu_value_type_t;

typedef enum
{
    LU_VALUE_FLAG_NULL = 1u << 0,
} lu_value_flags_t;

typedef enum
{
    LU_OK = 0,
    LU_ERR_ARG,
    LU_ERR_NO_SPACE,
    LU_ERR_BAD_SOF,
    LU_ERR_BAD_EOF,
    LU_ERR_VERSION,
    LU_ERR_BAD_LENGTH,
    LU_ERR_CRC,
    LU_ERR_BAD_PAYLOAD,
} lu_status_t;

typedef struct
{
    uint8_t type;
    uint8_t flags;
    uint16_t seq;
    uint16_t endpoint;
    uint32_t cluster;
    uint32_t id;
    const uint8_t * payload;
    uint16_t payload_len;
} lu_frame_t;

typedef struct
{
    uint8_t * buf;
    size_t cap;
    size_t len;
    uint8_t count;
} lu_field_writer_t;

typedef struct
{
    const uint8_t * buf;
    size_t len;
    size_t offset;
    uint8_t count;
    uint8_t index;
} lu_field_reader_t;

typedef struct
{
    uint8_t field_id;
    uint8_t value_type;
    uint8_t value_flags;
    const uint8_t * value;
    uint16_t value_len;
} lu_field_t;

typedef struct
{
    uint8_t value_type;
    uint8_t value_flags;
    const uint8_t * value;
    uint16_t value_len;
} lu_attr_value_t;

uint16_t lu_crc16_ccitt_false(const uint8_t * data, size_t len);

lu_status_t lu_pack_frame(uint8_t type, uint8_t flags, uint16_t seq, uint16_t endpoint, uint32_t cluster, uint32_t id,
                          const uint8_t * payload, uint16_t payload_len, uint8_t * out, size_t out_cap, size_t * out_len);

lu_status_t lu_unpack_frame(const uint8_t * frame, size_t frame_len, lu_frame_t * out);

void lu_field_writer_init(lu_field_writer_t * writer, uint8_t * buf, size_t cap);
lu_status_t lu_field_add_raw(lu_field_writer_t * writer, uint8_t field_id, uint8_t value_type, uint8_t value_flags,
                             const uint8_t * value, uint16_t value_len);
lu_status_t lu_field_add_null(lu_field_writer_t * writer, uint8_t field_id, uint8_t value_type);
lu_status_t lu_field_add_bool(lu_field_writer_t * writer, uint8_t field_id, bool value);
lu_status_t lu_field_add_u8(lu_field_writer_t * writer, uint8_t field_id, uint8_t value);
lu_status_t lu_field_add_u16(lu_field_writer_t * writer, uint8_t field_id, uint16_t value);
lu_status_t lu_field_add_u32(lu_field_writer_t * writer, uint8_t field_id, uint32_t value);
lu_status_t lu_field_add_i8(lu_field_writer_t * writer, uint8_t field_id, int8_t value);
lu_status_t lu_field_add_i16(lu_field_writer_t * writer, uint8_t field_id, int16_t value);
lu_status_t lu_field_add_enum8(lu_field_writer_t * writer, uint8_t field_id, uint8_t value);
lu_status_t lu_field_add_bitmap8(lu_field_writer_t * writer, uint8_t field_id, uint8_t value);
lu_status_t lu_field_add_bitmap16(lu_field_writer_t * writer, uint8_t field_id, uint16_t value);
lu_status_t lu_field_add_bitmap32(lu_field_writer_t * writer, uint8_t field_id, uint32_t value);

lu_status_t lu_field_reader_init(lu_field_reader_t * reader, const uint8_t * payload, size_t payload_len);
lu_status_t lu_field_next(lu_field_reader_t * reader, lu_field_t * field);
bool lu_field_reader_done(const lu_field_reader_t * reader);

lu_status_t lu_pack_attr_bool(bool value, uint8_t * out, size_t out_cap, size_t * out_len);
lu_status_t lu_pack_attr_u8(uint8_t value_type, uint8_t value, uint8_t * out, size_t out_cap, size_t * out_len);
lu_status_t lu_pack_attr_u16(uint8_t value_type, uint16_t value, uint8_t * out, size_t out_cap, size_t * out_len);
lu_status_t lu_pack_attr_null(uint8_t value_type, uint8_t * out, size_t out_cap, size_t * out_len);
lu_status_t lu_unpack_attr(const uint8_t * payload, size_t payload_len, lu_attr_value_t * out);

bool lu_field_get_u8(const lu_field_t * field, uint8_t expected_type, uint8_t * out);
bool lu_field_get_u16(const lu_field_t * field, uint8_t expected_type, uint16_t * out);
bool lu_attr_get_bool(const lu_attr_value_t * attr, bool * out);
bool lu_attr_get_u8(const lu_attr_value_t * attr, uint8_t expected_type, uint8_t * out);
bool lu_attr_get_u16(const lu_attr_value_t * attr, uint8_t expected_type, uint16_t * out);

#ifdef __cplusplus
}
#endif

#endif
