#include "LightUartProtocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t kHelloFrame[] = {
    0xA5, 0x5A, 0x02, 0x30, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x02, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x23, 0x04, 0xFA, 0x89, 0x0D, 0x0A,
};

static void TestCrc(void)
{
    static const uint8_t text[] = "123456789";
    assert(lu_crc16_ccitt_false(text, 9) == 0x29B1);
    assert(lu_crc32_iso_hdlc(text, 9) == 0xCBF43926u);
    assert(lu_crc32_iso_hdlc(NULL, 0) == 0u);
}

static void TestHelloVector(void)
{
    lu_frame_t frame;
    assert(sizeof(kHelloFrame) == 49);
    assert(lu_unpack_frame(kHelloFrame, sizeof(kHelloFrame), &frame) == LU_OK);
    assert(frame.type == LU_MSG_HELLO_REQUEST);
    assert(frame.flags == LU_FLAG_ACK_REQUIRED);
    assert(frame.seq == 1);
    assert(frame.session_id == 0);
    assert(frame.uart_device_id == 0);
    assert(frame.endpoint == 0);
    assert(frame.binding_version == 0);
    assert(frame.payload_len == 14);
    assert(frame.payload[0] == 2 && frame.payload[1] == 0);

    uint8_t corrupted[sizeof(kHelloFrame)];
    memcpy(corrupted, kHelloFrame, sizeof(corrupted));
    corrupted[10] ^= 0x01;
    assert(lu_unpack_frame(corrupted, sizeof(corrupted), &frame) == LU_ERR_CRC);
    assert(lu_unpack_frame(kHelloFrame, sizeof(kHelloFrame) - 1, &frame) == LU_ERR_BAD_LENGTH);
}

static void TestPackRoundTrip(void)
{
    uint8_t payload[1024];
    uint8_t frame[LU_MIN_FRAME_SIZE + LU_MAX_PAYLOAD_SIZE];
    size_t frame_len = 0;
    lu_frame_t decoded;
    memset(payload, 0xA5, sizeof(payload));
    assert(lu_pack_frame(LU_MSG_UP_STATE_SNAPSHOT, LU_FLAG_ACK_REQUIRED, 7, 0x10203040, 0x01020304, 3, 9, 0, 0, payload,
                         sizeof(payload), frame, sizeof(frame), &frame_len) == LU_OK);
    assert(frame_len == 1059);
    assert(lu_unpack_frame(frame, frame_len, &decoded) == LU_OK);
    assert(decoded.session_id == 0x10203040);
    assert(decoded.uart_device_id == 0x01020304);
    assert(decoded.endpoint == 3 && decoded.binding_version == 9);
    assert(decoded.payload_len == sizeof(payload));
    assert(decoded.payload[1023] == 0xA5);
}

static void TestSemanticFlags(void)
{
    uint8_t frame[LU_MIN_FRAME_SIZE];
    size_t frame_len = 0;
    lu_frame_t decoded;
    assert(lu_pack_frame(LU_MSG_UP_ATTRIBUTE_REPORT, LU_FLAG_RESPONSE, 1, 1, 1, 3, 1, 6, 0, NULL, 0, frame, sizeof(frame), &frame_len) == LU_OK);
    assert(lu_unpack_frame(frame, frame_len, &decoded) == LU_ERR_BAD_PAYLOAD);
    assert(lu_pack_frame(LU_MSG_HELLO_REQUEST, LU_FLAG_ACK_REQUIRED | 0x80u, 1, 0, 0, 0, 0, 0, 0, NULL, 0, frame, sizeof(frame), &frame_len) == LU_OK);
    assert(lu_unpack_frame(frame, frame_len, &decoded) == LU_OK); // Rev.3 ignores reserved bits 6-7.
    assert(lu_pack_frame(LU_MSG_UP_STATE_SNAPSHOT, 0, 1, 1, 1, 3, 1, 0, 0, NULL, 0, frame, sizeof(frame), &frame_len) == LU_OK);
    assert(lu_unpack_frame(frame, frame_len, &decoded) == LU_ERR_BAD_PAYLOAD);
}

int main(void)
{
    TestCrc();
    TestHelloVector();
    TestPackRoundTrip();
    TestSemanticFlags();
    puts("bridge_uart_v2 protocol C tests: PASS");
    return 0;
}
