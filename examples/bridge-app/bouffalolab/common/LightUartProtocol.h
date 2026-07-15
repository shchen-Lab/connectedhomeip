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
    LU_FLAG_NULL_VALUE       = 1u << 3,
} lu_flags_t;

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
} lu_payload_writer_t;

typedef struct
{
    const uint8_t * buf;
    size_t len;
    size_t offset;
} lu_payload_reader_t;

uint16_t lu_crc16_ccitt_false(const uint8_t * data, size_t len);

lu_status_t lu_pack_frame(uint8_t type, uint8_t flags, uint16_t seq, uint16_t endpoint, uint32_t cluster, uint32_t id,
                          const uint8_t * payload, uint16_t payload_len, uint8_t * out, size_t out_cap, size_t * out_len);

lu_status_t lu_unpack_frame(const uint8_t * frame, size_t frame_len, lu_frame_t * out);

void lu_payload_writer_init(lu_payload_writer_t * writer, uint8_t * buf, size_t cap);
lu_status_t lu_payload_add_u8(lu_payload_writer_t * writer, uint8_t value);
lu_status_t lu_payload_add_u16(lu_payload_writer_t * writer, uint16_t value);
lu_status_t lu_payload_add_u32(lu_payload_writer_t * writer, uint32_t value);
lu_status_t lu_payload_add_i16(lu_payload_writer_t * writer, int16_t value);

void lu_payload_reader_init(lu_payload_reader_t * reader, const uint8_t * payload, size_t payload_len);
bool lu_payload_get_u8(lu_payload_reader_t * reader, uint8_t * out);
bool lu_payload_get_u16(lu_payload_reader_t * reader, uint16_t * out);
bool lu_payload_get_u32(lu_payload_reader_t * reader, uint32_t * out);
bool lu_payload_get_i16(lu_payload_reader_t * reader, int16_t * out);
bool lu_payload_reader_done(const lu_payload_reader_t * reader);

#ifdef __cplusplus
}
#endif

#endif
