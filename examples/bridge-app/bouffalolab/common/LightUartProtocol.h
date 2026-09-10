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
#define LU_VERSION 0x02u

#define LU_HEADER_SIZE 31u
#define LU_CRC_HEADER_SIZE 29u
#define LU_TRAILER_SIZE 4u
#define LU_MIN_FRAME_SIZE (LU_HEADER_SIZE + LU_TRAILER_SIZE)
#define LU_FRAME_OVERHEAD LU_MIN_FRAME_SIZE
#define LU_MAX_PAYLOAD_SIZE 1024u

typedef enum
{
    LU_MSG_DOWN_INVOKE_COMMAND          = 0x10,
    LU_MSG_DOWN_READ_ATTRIBUTE          = 0x11,
    LU_MSG_DOWN_COMMAND_RESPONSE        = 0x12,
    LU_MSG_DOWN_READ_ATTRIBUTE_RESPONSE = 0x13,
    LU_MSG_UP_ATTRIBUTE_REPORT          = 0x20,
    LU_MSG_HELLO_REQUEST                = 0x30,
    LU_MSG_HELLO_RESPONSE               = 0x31,
    LU_MSG_DEVICE_LIST_REQUEST          = 0x32,
    LU_MSG_DEVICE_LIST_BEGIN            = 0x33,
    LU_MSG_DEVICE_LIST_ENTRY            = 0x34,
    LU_MSG_DEVICE_LIST_END              = 0x35,
    LU_MSG_DEVICE_ADD_NOTIFY            = 0x36,
    LU_MSG_DEVICE_ADD_RESPONSE          = 0x37,
    LU_MSG_DEVICE_REMOVE_NOTIFY         = 0x38,
    LU_MSG_DEVICE_REMOVE_RESPONSE       = 0x39,
    LU_MSG_DEVICE_BIND_REQUEST          = 0x3A,
    LU_MSG_DEVICE_BIND_RESPONSE         = 0x3B,
    LU_MSG_DEVICE_ONLINE_NOTIFY         = 0x3C,
    LU_MSG_DEVICE_ONLINE_RESPONSE       = 0x3D,
    LU_MSG_DEVICE_OFFLINE_NOTIFY        = 0x3E,
    LU_MSG_DEVICE_OFFLINE_RESPONSE      = 0x3F,
    LU_MSG_UP_STATE_SNAPSHOT            = 0x40,
    LU_MSG_STATE_SNAPSHOT_RESPONSE      = 0x41,
    LU_MSG_HEARTBEAT                    = 0x42,
    LU_MSG_HEARTBEAT_RESPONSE           = 0x43,
    LU_MSG_DEVICE_STATE_REQUEST         = 0x44,
    LU_MSG_DEVICE_STATE_RESPONSE        = 0x45,
} lu_msg_type_t;

typedef enum
{
    LU_FLAG_RESPONSE       = 1u << 0,
    LU_FLAG_ERROR          = 1u << 1,
    LU_FLAG_ACK_REQUIRED   = 1u << 2,
    LU_FLAG_NULL_VALUE     = 1u << 3,
    LU_FLAG_MORE           = 1u << 4,
    LU_FLAG_RETRANSMISSION = 1u << 5,
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
    LU_ERR_RESERVED_BITS,
} lu_status_t;

typedef enum
{
    LU_STATUS_OK                      = 0x00,
    LU_STATUS_INVALID_ARGUMENT        = 0x01,
    LU_STATUS_UNKNOWN_DEVICE          = 0x02,
    LU_STATUS_ENDPOINT_EXHAUSTED      = 0x03,
    LU_STATUS_DEVICE_ID_CONFLICT      = 0x04,
    LU_STATUS_BINDING_MISMATCH        = 0x05,
    LU_STATUS_UNSUPPORTED_DEVICE_TYPE = 0x06,
    LU_STATUS_BUSY                    = 0x07,
    LU_STATUS_BAD_STATE_VERSION       = 0x08,
    LU_STATUS_PROTOCOL_MISMATCH       = 0x09,
    LU_STATUS_CRC_OR_FORMAT_ERROR     = 0x0A,
    LU_STATUS_INTERNAL_ERROR          = 0x0B,
    LU_STATUS_UNSUPPORTED_COMMAND     = 0x0C,
    LU_STATUS_UNSUPPORTED_ATTRIBUTE   = 0x0D,
    LU_STATUS_DEVICE_OFFLINE          = 0x0E,
    LU_STATUS_STATE_UNAVAILABLE       = 0x0F,
} lu_business_status_t;

typedef struct
{
    uint8_t type;
    uint8_t flags;
    uint16_t seq;
    uint32_t session_id;
    uint32_t uart_device_id;
    uint16_t endpoint;
    uint32_t binding_version;
    uint32_t cluster;
    uint32_t id;
    const uint8_t * payload;
    uint16_t payload_len;
} lu_frame_t;

/* Validate message-specific flags after the wire CRC has been checked. */
lu_status_t lu_validate_frame_semantics(const lu_frame_t * frame);

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
uint32_t lu_crc32_iso_hdlc(const uint8_t * data, size_t len);

lu_status_t lu_pack_frame(uint8_t type, uint8_t flags, uint16_t seq, uint32_t session_id, uint32_t uart_device_id,
                          uint16_t endpoint, uint32_t binding_version, uint32_t cluster, uint32_t id, const uint8_t * payload,
                          uint16_t payload_len, uint8_t * out, size_t out_cap, size_t * out_len);

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
