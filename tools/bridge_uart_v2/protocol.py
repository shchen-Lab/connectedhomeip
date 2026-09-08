"""Independent Python implementation of the Matter Bridge UART v2 wire codec."""

from __future__ import annotations

import struct
from dataclasses import dataclass

SOF = b"\xA5\x5A"
EOF = b"\x0D\x0A"
VERSION = 2
HEADER_SIZE = 31
MIN_FRAME_SIZE = 35
MAX_PAYLOAD = 1024

ACK_REQUIRED = 1 << 2
RESPONSE = 1 << 0
ERROR = 1 << 1


def crc16(data: bytes) -> int:
    value = 0xFFFF
    for byte in data:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ 0x1021) & 0xFFFF if value & 0x8000 else (value << 1) & 0xFFFF
    return value


def crc32(data: bytes) -> int:
    value = 0xFFFFFFFF
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ 0xEDB88320 if value & 1 else value >> 1
    return value ^ 0xFFFFFFFF


@dataclass(frozen=True)
class Frame:
    message_type: int
    flags: int
    sequence: int
    session: int
    device: int
    endpoint: int
    binding: int
    cluster: int
    ident: int
    payload: bytes = b""

    def encode(self) -> bytes:
        if not 0 <= len(self.payload) <= MAX_PAYLOAD:
            raise ValueError("payload length")
        header = SOF + bytes((VERSION, self.message_type, self.flags)) + struct.pack(
            "<HIIHIIIH",
            self.sequence,
            self.session,
            self.device,
            self.endpoint,
            self.binding,
            self.cluster,
            self.ident,
            len(self.payload),
        )
        value = header + self.payload
        return value + struct.pack("<H", crc16(value[2:])) + EOF

    @staticmethod
    def decode(data: bytes) -> "Frame":
        if len(data) < MIN_FRAME_SIZE or data[:2] != SOF or data[2] != VERSION:
            raise ValueError("SOF/version")
        payload_len = struct.unpack_from("<H", data, 29)[0]
        if payload_len > MAX_PAYLOAD or len(data) != MIN_FRAME_SIZE + payload_len:
            raise ValueError("length")
        if data[-2:] != EOF:
            raise ValueError("EOF")
        if crc16(data[2 : 31 + payload_len]) != struct.unpack_from("<H", data, 31 + payload_len)[0]:
            raise ValueError("CRC")
        sequence, session, device, endpoint, binding, cluster, ident, _ = struct.unpack_from(
            "<HIIHIIIH", data, 5
        )
        return Frame(
            data[3],
            data[4],
            sequence,
            session,
            device,
            endpoint,
            binding,
            cluster,
            ident,
            data[31 : 31 + payload_len],
        )


def u8(value: int) -> bytes:
    return struct.pack("<B", value)


def u16(value: int) -> bytes:
    return struct.pack("<H", value)


def u32(value: int) -> bytes:
    return struct.pack("<I", value)
