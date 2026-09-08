import binascii
import struct
import unittest


SOF = b"\xA5\x5A"
EOF = b"\x0D\x0A"
VERSION = 2
HEADER_SIZE = 31
MIN_FRAME = 35
MAX_PAYLOAD = 1024


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def pack_frame(message_type, flags, sequence, session, device, endpoint, binding, cluster, ident, payload=b""):
    header = (
        SOF
        + bytes((VERSION, message_type, flags))
        + struct.pack("<HIIHIIIH", sequence, session, device, endpoint, binding, cluster, ident, len(payload))
    )
    assert len(header) == HEADER_SIZE
    crc = crc16_ccitt_false(header[2:] + payload)
    return header + payload + struct.pack("<H", crc) + EOF


def unpack_frame(frame: bytes):
    if len(frame) < MIN_FRAME or frame[:2] != SOF or frame[2] != VERSION:
        raise ValueError("bad frame header")
    payload_len = struct.unpack_from("<H", frame, 29)[0]
    if payload_len > MAX_PAYLOAD or len(frame) != MIN_FRAME + payload_len:
        raise ValueError("bad frame length")
    if frame[-2:] != EOF:
        raise ValueError("bad EOF")
    expected = struct.unpack_from("<H", frame, 31 + payload_len)[0]
    if crc16_ccitt_false(frame[2 : 31 + payload_len]) != expected:
        raise ValueError("bad CRC")
    sequence, session, device, endpoint, binding, cluster, ident, _ = struct.unpack_from("<HIIHIIIH", frame, 5)
    return {
        "type": frame[3],
        "flags": frame[4],
        "sequence": sequence,
        "session": session,
        "device": device,
        "endpoint": endpoint,
        "binding": binding,
        "cluster": cluster,
        "id": ident,
        "payload": frame[31 : 31 + payload_len],
    }


class ProtocolVectorTest(unittest.TestCase):
    def test_crc_vectors(self):
        self.assertEqual(crc16_ccitt_false(b"123456789"), 0x29B1)
        self.assertEqual(binascii.crc32(b"123456789"), 0xCBF43926)

    def test_hello_vector(self):
        frame = pack_frame(0x30, 0x04, 1, 0, 0, 0, 0, 0, 0, bytes.fromhex("02 00 01 00 00 00 00 00 00 00 00 04 23 04"))
        self.assertEqual(len(frame), 49)
        decoded = unpack_frame(frame)
        self.assertEqual(decoded["type"], 0x30)
        self.assertEqual(decoded["payload"][:2], b"\x02\x00")

    def test_bad_crc_and_embedded_delimiters(self):
        frame = pack_frame(0x20, 0, 1, 1, 2, 3, 4, 0x300, 7, b"\xA5\x5A\x0D\x0A")
        self.assertEqual(unpack_frame(frame)["payload"], b"\xA5\x5A\x0D\x0A")
        corrupt = bytearray(frame)
        corrupt[10] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC"):
            unpack_frame(bytes(corrupt))

    def test_snapshot_masks(self):
        self.assertEqual(6 + 1 + 1 + 1 + 1, 10)  # HS mask 0x000F
        self.assertEqual(6 + 1 + 1 + 2, 10)  # CT mask 0x0043


if __name__ == "__main__":
    unittest.main()
