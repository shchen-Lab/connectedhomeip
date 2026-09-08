#!/usr/bin/env python3
"""Small deterministic UART v2 MCU simulator for Bridge integration tests."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import struct
import sys
import termios
import time
import tty
import secrets
import select
from collections import deque
from dataclasses import dataclass, asdict, replace

from protocol import ACK_REQUIRED, ERROR, MIN_FRAME_SIZE, RESPONSE, Frame, crc32, u16, u32, u8

HELLO_REQUEST = 0x30
HELLO_RESPONSE = 0x31
LIST_REQUEST = 0x32
LIST_BEGIN = 0x33
LIST_ENTRY = 0x34
LIST_END = 0x35
ADD_NOTIFY = 0x36
ADD_RESPONSE = 0x37
REMOVE_NOTIFY = 0x38
REMOVE_RESPONSE = 0x39
BIND_REQUEST = 0x3A
BIND_RESPONSE = 0x3B
ONLINE_NOTIFY = 0x3C
ONLINE_RESPONSE = 0x3D
OFFLINE_NOTIFY = 0x3E
OFFLINE_RESPONSE = 0x3F
STATE_SNAPSHOT = 0x40
SNAPSHOT_RESPONSE = 0x41
HEARTBEAT = 0x42
HEARTBEAT_RESPONSE = 0x43
STATE_REQUEST = 0x44
STATE_RESPONSE = 0x45
COMMAND = 0x10
READ_ATTRIBUTE = 0x11
COMMAND_RESPONSE = 0x12
READ_RESPONSE = 0x13

OK = 0x00
INVALID_ARGUMENT = 0x01
UNKNOWN_DEVICE = 0x02
ENDPOINT_EXHAUSTED = 0x03
CONFLICT = 0x04
MISMATCH = 0x05
UNSUPPORTED = 0x06
BUSY = 0x07
OFFLINE = 0x0E
UNSUPPORTED_ATTRIBUTE = 0x0D
STATE_UNAVAILABLE = 0x0F

CAP_CW = 0x13
CAP_HS_CT = 0x17
MASK_CT = 0x0043
MASK_HS = 0x000F


@dataclass
class Device:
    uart_device_id: int
    device_type: int
    capability_flags: int
    online: bool = True
    state_valid: bool = True
    state_version: int = 1
    on: int = 1
    level: int = 128
    hue: int = 0
    saturation: int = 0
    color_temperature: int = 250
    pending_remove: bool = False
    color_mode: int = 0

    @property
    def state_flags(self) -> int:
        return 0 if self.pending_remove else 1 | (2 if self.online else 0) | (4 if self.state_valid else 0)

    @property
    def mode_mask(self) -> int:
        return MASK_HS if self.capability_flags == CAP_HS_CT and self.color_mode == 0 else MASK_CT

    def snapshot_payload(self) -> bytes:
        values = u8(self.on) + u8(self.level)
        if self.mode_mask == MASK_HS:
            values += u8(self.hue) + u8(self.saturation)
        else:
            values += u16(self.color_temperature)
        return u32(self.state_version) + u16(self.mode_mask) + values


class Simulator:
    def __init__(self, devices_path: pathlib.Path, state_path: pathlib.Path, log_path: pathlib.Path | None):
        self.devices_path = devices_path
        self.state_path = state_path
        self.log_path = log_path
        self.session = secrets.randbits(32) or 1
        self.sequence = 1
        self.binding: dict[int, tuple[int, int]] = {}
        self.list_version = 1
        self.bridge_boot_id = 0
        self.devices = self._load_devices()
        self.rx = bytearray()
        self.cache = {}
        self.list_cache = None
        self.pending_snapshot = None
        self.snapshot_queue = deque()
        self.running = True

    def _load_devices(self) -> dict[int, Device]:
        if self.state_path.exists():
            raw = json.loads(self.state_path.read_text())
        else:
            raw = json.loads(self.devices_path.read_text())
        return {int(item["uart_device_id"]): Device(**item) for item in raw}

    def _save(self) -> None:
        self.state_path.parent.mkdir(parents=True, exist_ok=True)
        self.state_path.write_text(json.dumps([asdict(v) for v in self.devices.values()], indent=2, sort_keys=True))

    def _log(self, direction: str, frame: Frame, status: int | None = None) -> None:
        record = {
            "time": time.time(),
            "direction": direction,
            "type": f"0x{frame.message_type:02X}",
            "sequence": frame.sequence,
            "session": frame.session,
            "device": frame.device,
            "endpoint": frame.endpoint,
            "binding": frame.binding,
            "status": status,
            "raw_hex": frame.encode().hex(" ").upper(),
        }
        line = json.dumps(record, sort_keys=True)
        print(line, flush=True)
        if self.log_path:
            with self.log_path.open("a") as stream:
                stream.write(line + "\n")

    def _response(self, request: Frame, message_type: int, payload: bytes = b"", status: int = OK) -> Frame:
        if status:
            payload = u8(status) if message_type != LIST_END else payload
        flags = RESPONSE | (ERROR if status else 0)
        if message_type == LIST_END:
            flags = ERROR if status else 0
        frame = Frame(message_type, flags, request.sequence, self.session, request.device, request.endpoint,
                      request.binding, request.cluster, request.ident, payload)
        self._log("tx", frame, status)
        return frame

    def _snapshot(self, device: Device, endpoint: int, binding: int) -> Frame:
        frame = Frame(STATE_SNAPSHOT, ACK_REQUIRED, self._next_sequence(), self.session, device.uart_device_id,
                      endpoint, binding, 0, 0, device.snapshot_payload())
        self._log("tx", frame)
        return frame

    def _next_sequence(self) -> int:
        value = self.sequence
        self.sequence = self.sequence + 1 if self.sequence < 0xFFFF else 1
        return value

    def handle(self, request: Frame) -> list[Frame]:
        if request.message_type != HELLO_REQUEST and (request.session != self.session or not self.bridge_boot_id):
            return []
        if request.message_type == SNAPSHOT_RESPONSE:
            self._log("rx", request)
            pending = self.pending_snapshot
            if pending:
                sent = pending[0]
                if (request.sequence, request.session, request.device, request.endpoint, request.binding) == (
                    sent.sequence, sent.session, sent.device, sent.endpoint, sent.binding):
                    if request.payload and request.payload[0] == BUSY:
                        self.pending_snapshot = (sent, time.monotonic(), pending[2])
                    elif request.payload and len(request.payload) == (5 if request.payload[0] == OK else 1):
                        self.pending_snapshot = None
            return []
        key = (request.message_type, request.sequence, request.device)
        normalized = replace(request, flags=request.flags & 0x1F)
        if request.flags & 0x20 and request.message_type != LIST_REQUEST:
            cached = self.cache.get(key)
            if not cached or cached[0] != normalized:
                return [self._response(request, request.message_type + (2 if request.message_type in (COMMAND, READ_ATTRIBUTE) else 1),
                                       status=BUSY if not cached else INVALID_ARGUMENT)]
            for frame in cached[1]:
                self._log("tx", frame)
            return cached[1]
        out = self._handle(request)
        if out and request.message_type not in (LIST_REQUEST, HEARTBEAT):
            self.cache[key] = (normalized, out)
            while len(self.cache) > 8:
                del self.cache[next(iter(self.cache))]
        return out

    def _handle(self, request: Frame) -> list[Frame]:
        self._log("rx", request)
        if request.message_type == HELLO_REQUEST:
            if len(request.payload) != 14 or request.payload[:2] != b"\x02\x00" or not struct.unpack_from("<I", request.payload, 2)[0]:
                return [self._response(request, HELLO_RESPONSE, status=INVALID_ARGUMENT)]
            new_boot = struct.unpack_from("<I", request.payload, 2)[0]
            if new_boot != self.bridge_boot_id:
                self.binding.clear()
                self.cache.clear()
                self.list_cache = None
                self.pending_snapshot = None
                self.snapshot_queue.clear()
            self.bridge_boot_id = new_boot
            payload = u8(OK) + u8(2) + u8(0) + u32(0) + u16(1024) + u32(self.list_version) + u32(0x00010000)
            return [self._response(request, HELLO_RESPONSE, payload)]
        if request.session != self.session or not self.bridge_boot_id:
            return []
        if request.message_type == LIST_REQUEST:
            if len(request.payload) != 12:
                return [self._response(request, LIST_END, b"\x00" * 10 + u8(INVALID_ARGUMENT), INVALID_ARGUMENT)]
            transaction = struct.unpack_from("<I", request.payload)[0]
            if self.list_cache and self.list_cache[0] == transaction:
                return self.list_cache[1]
            entries = sorted(self.devices.values(), key=lambda d: d.uart_device_id)
            encoded = [u32(transaction) + u32(d.uart_device_id) + u8(d.device_type) + u32(d.capability_flags) + u8(d.state_flags) + u32(d.state_version) for d in entries]
            checksum = crc32(b"".join(encoded))
            begin = u32(transaction) + u32(self.list_version) + u16(len(encoded)) + u16(18) + u32(checksum)
            out = [Frame(LIST_BEGIN, 0, self._next_sequence(), self.session, 0, 0, 0, 0, 0, begin)]
            for device, entry in zip(entries, encoded):
                out.append(Frame(LIST_ENTRY, 0, self._next_sequence(), self.session, 0, 0, 0, 0, 0, entry))
            end = u32(transaction) + u16(len(encoded)) + u32(checksum) + u8(OK)
            out.append(Frame(LIST_END, 0, self._next_sequence(), self.session, 0, 0, 0, 0, 0, end))
            for frame in out:
                self._log("tx", frame)
            self.list_cache = (transaction, out)
            return out
        if request.message_type == BIND_REQUEST:
            if request.device not in self.devices or len(request.payload) != 10:
                return [self._response(request, BIND_RESPONSE, u8(UNKNOWN_DEVICE), UNKNOWN_DEVICE)]
            device = self.devices[request.device]
            if not request.endpoint or not request.binding or request.payload[5] & ~3 or device.pending_remove:
                return [self._response(request, BIND_RESPONSE, status=INVALID_ARGUMENT)]
            if request.payload[0] != device.device_type or struct.unpack_from("<I", request.payload, 1)[0] != device.capability_flags:
                return [self._response(request, BIND_RESPONSE, u8(CONFLICT), CONFLICT)]
            self.binding[device.uart_device_id] = (request.endpoint, request.binding)
            response = self._response(request, BIND_RESPONSE, u8(OK) + u16(request.endpoint) + u32(request.binding))
            return [response, self._snapshot(device, request.endpoint, request.binding)] if device.online and device.state_valid else [response]
        if request.message_type == HEARTBEAT:
            return [self._response(request, HEARTBEAT_RESPONSE, u8(OK) + u32(int(time.monotonic())) + u32(self.list_version))]
        if request.message_type == COMMAND:
            binding = self.binding.get(request.device)
            if binding != (request.endpoint, request.binding):
                return [self._response(request, COMMAND_RESPONSE, u8(MISMATCH), MISMATCH)]
            if not self.devices[request.device].online:
                return [self._response(request, COMMAND_RESPONSE, u8(OFFLINE), OFFLINE)]
            device = self.devices[request.device]
            before = asdict(device)
            payload = request.payload
            cluster, ident = request.cluster, request.ident
            sizes = {0: 6, 3: 5, 6: 6, 0x0A: 6, 0x47: 2}
            if cluster == 6 and (ident not in (0, 1, 2) or payload):
                return [self._response(request, COMMAND_RESPONSE, status=INVALID_ARGUMENT)]
            if cluster == 8 and (ident not in (0, 4) or len(payload) != 6):
                return [self._response(request, COMMAND_RESPONSE, status=0x0C)]
            if cluster == 0x300 and (ident not in sizes or len(payload) != sizes[ident]):
                return [self._response(request, COMMAND_RESPONSE, status=0x0C)]
            if cluster not in (6, 8, 0x300):
                return [self._response(request, COMMAND_RESPONSE, status=0x0C)]
            if cluster in (8, 0x300):
                if payload[-2] & ~1 or payload[-1] & ~1:
                    return [self._response(request, COMMAND_RESPONSE, status=INVALID_ARGUMENT)]
                # This smoke simulator implements immediate targets, never fakes a gradual transition.
                if cluster == 8:
                    duration = struct.unpack_from("<H", payload, 2)[0]
                    valid = payload[0] <= 1 and payload[1] <= 254 and (payload[1] > 0 or ident == 4)
                    valid = valid and not (payload[0] and duration)
                elif ident == 0x47:
                    duration, valid = 0, True
                else:
                    offset = 1 if ident == 3 else 2
                    duration = struct.unpack_from("<H", payload, offset)[0]
                    valid = (154 <= struct.unpack_from("<H", payload)[0] <= 454) if ident == 0x0A else (
                        device.capability_flags == CAP_HS_CT and payload[0] <= 254 and
                        (ident != 6 or payload[1] <= 254) and (ident != 0 or payload[1] <= 3))
                if not valid or duration == 0xFFFF:
                    return [self._response(request, COMMAND_RESPONSE, status=INVALID_ARGUMENT)]
                if duration:
                    return [self._response(request, COMMAND_RESPONSE, status=0x0C)]
                if not device.on and not (cluster == 8 and ident == 4) and not (payload[-2] & payload[-1] & 1):
                    response = self._response(request, COMMAND_RESPONSE, u8(OK) + u32(device.state_version) + u8(0))
                    return [response, self._snapshot(device, request.endpoint, request.binding)]
            if request.cluster == 0x06 and request.ident in (0, 1, 2):
                device.on = 0 if request.ident == 0 else 1 if request.ident == 1 else 1 - device.on
            elif cluster == 8:
                device.level = max(1, payload[1])
                if ident == 4:
                    device.on = int(payload[1] != 0)
            elif ident == 0x0A:
                device.color_temperature = struct.unpack_from("<H", payload)[0]
                device.color_mode = 2
            elif ident in (0, 3, 6):
                if ident in (0, 6):
                    device.hue = payload[0]
                if ident in (3, 6):
                    device.saturation = payload[0] if ident == 3 else payload[1]
                device.color_mode = 0
            if asdict(device) != before:
                device.state_version += 1
            self._save()
            response = self._response(request, COMMAND_RESPONSE, u8(OK) + u32(before["state_version"]) + u8(0))
            return [response, self._snapshot(device, request.endpoint, request.binding)]
        if request.message_type == READ_ATTRIBUTE:
            binding = self.binding.get(request.device)
            if binding != (request.endpoint, request.binding):
                return [self._response(request, READ_RESPONSE, u8(MISMATCH), MISMATCH)]
            device = self.devices.get(request.device)
            if not device:
                return [self._response(request, READ_RESPONSE, u8(UNKNOWN_DEVICE), UNKNOWN_DEVICE)]
            if request.cluster == 0x06 and request.ident == 0:
                value = u8(device.on)
            elif request.cluster == 0x08 and request.ident == 0:
                value = u8(device.level)
            elif request.cluster == 0x300 and request.ident == 0 and device.capability_flags == CAP_HS_CT:
                value = u8(device.hue)
            elif request.cluster == 0x300 and request.ident == 1 and device.capability_flags == CAP_HS_CT:
                value = u8(device.saturation)
            elif request.cluster == 0x300 and request.ident == 7:
                value = u16(device.color_temperature)
            else:
                return [self._response(request, READ_RESPONSE, u8(UNSUPPORTED_ATTRIBUTE), UNSUPPORTED_ATTRIBUTE)]
            return [self._response(request, READ_RESPONSE, u8(OK) + u32(device.state_version) + value)]
        return []

    def run(self, fd: int) -> None:
        old = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            attrs = termios.tcgetattr(fd)
            attrs[4] = attrs[5] = termios.B921600
            attrs[2] &= ~termios.CRTSCTS
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            while self.running:
                if not self.pending_snapshot and self.snapshot_queue:
                    frame = self.snapshot_queue.popleft()
                    os.write(fd, frame.encode())
                    self.pending_snapshot = (frame, time.monotonic(), 0)
                if self.pending_snapshot and time.monotonic() - self.pending_snapshot[1] >= 0.5:
                    frame, _, retries = self.pending_snapshot
                    if retries >= 3:
                        self.pending_snapshot = None
                    else:
                        frame = replace(frame, flags=frame.flags | 0x20)
                        os.write(fd, frame.encode())
                        self._log("tx", frame)
                        self.pending_snapshot = (frame, time.monotonic(), retries + 1)
                if not select.select([fd], [], [], 0.05)[0]:
                    continue
                data = os.read(fd, 4096)
                if not data:
                    break
                self.rx.extend(data)
                while len(self.rx) >= MIN_FRAME_SIZE:
                    if self.rx[:2] != b"\xA5\x5A":
                        del self.rx[0]
                        continue
                    payload_len = struct.unpack_from("<H", self.rx, 29)[0]
                    total = 35 + payload_len
                    if payload_len > 1024:
                        del self.rx[:2]
                        continue
                    if len(self.rx) < total:
                        break
                    raw = bytes(self.rx[:total])
                    del self.rx[:total]
                    try:
                        request = Frame.decode(raw)
                    except ValueError:
                        continue
                    for response in self.handle(request):
                        if response.message_type == STATE_SNAPSHOT:
                            self.snapshot_queue.append(response)
                        else:
                            os.write(fd, response.encode())
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--devices", type=pathlib.Path, required=True)
    parser.add_argument("--state-dir", type=pathlib.Path, required=True)
    parser.add_argument("--log", type=pathlib.Path)
    args = parser.parse_args()
    if args.baud != 921600:
        parser.error("the Rev.3 protocol requires 921600 baud")
    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY)
    simulator = Simulator(args.devices, args.state_dir / "devices.json", args.log)
    simulator.run(fd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
