#!/usr/bin/env python3
"""Interactive phone/UART session. JSON commands on stdin; one serial owner."""
import argparse
from collections import deque
from dataclasses import asdict, replace
import fcntl
import json
import os
from pathlib import Path
import select
import struct
import sys
import termios
import time
import tty

from mcu_simulator import (
    Simulator, Device, HELLO_REQUEST, LIST_REQUEST, BIND_REQUEST,
    ADD_NOTIFY, ADD_RESPONSE, REMOVE_NOTIFY, REMOVE_RESPONSE,
    ONLINE_NOTIFY, ONLINE_RESPONSE, OFFLINE_NOTIFY, OFFLINE_RESPONSE,
    STATE_SNAPSHOT, SNAPSHOT_RESPONSE, COMMAND, BUSY, OK, CAP_CW, CAP_HS_CT,
)
from protocol import Frame, ACK_REQUIRED, RESPONSE, ERROR, u8, u16, u32

REPLIES = {ADD_RESPONSE: 7, REMOVE_RESPONSE: 1, ONLINE_RESPONSE: 1,
           OFFLINE_RESPONSE: 1, SNAPSHOT_RESPONSE: 5}


class PhoneSimulator(Simulator):
    def _log(self, *args, **kwargs):
        # Base Simulator logs construction, not transport. The owner logs actual IO.
        pass


def describe(frame):
    result = {"cluster": hex(frame.cluster), "id": hex(frame.ident),
              "payload_hex": frame.payload.hex(" ")}
    p = frame.payload
    if frame.message_type == COMMAND:
        names = {6: {0: "Off", 1: "On", 2: "Toggle"},
                 8: {0: "MoveToLevel", 1: "Move", 2: "Step", 3: "Stop",
                     4: "MoveToLevelWithOnOff", 5: "MoveWithOnOff",
                     6: "StepWithOnOff", 7: "StopWithOnOff"},
                 0x300: {0: "MoveToHue", 1: "MoveHue", 2: "StepHue",
                         3: "MoveToSaturation", 4: "MoveSaturation",
                         5: "StepSaturation", 6: "MoveToHueAndSaturation",
                         7: "UNSUPPORTED_XY", 8: "UNSUPPORTED_XY", 9: "UNSUPPORTED_XY",
                         10: "MoveToColorTemperature", 0x47: "StopMoveStep",
                         0x4B: "MoveColorTemperature", 0x4C: "StepColorTemperature"}}
        result["command"] = names.get(frame.cluster, {}).get(frame.ident, "Unknown")
        if frame.cluster == 8 and frame.ident in (0, 4) and len(p) == 6:
            result.update(nullable=p[0], level=p[1], transition=struct.unpack_from("<H", p, 2)[0])
        if frame.cluster == 0x300 and frame.ident == 6 and len(p) == 6:
            result.update(hue=p[0], saturation=p[1], transition=struct.unpack_from("<H", p, 2)[0])
        if frame.cluster == 0x300 and frame.ident == 10 and len(p) == 6:
            result.update(ct=struct.unpack_from("<H", p)[0], transition=struct.unpack_from("<H", p, 2)[0])
        if frame.cluster in (8, 0x300) and len(p) >= 2:
            result.update(options_mask=p[-2], options_override=p[-1])
    if frame.message_type == STATE_SNAPSHOT and len(p) == 10:
        result.update(state_version=struct.unpack_from("<I", p)[0], mask=hex(struct.unpack_from("<H", p, 4)[0]))
    if frame.flags & RESPONSE and p:
        result["status"] = p[0]
    return result


class Session:
    def __init__(self, simulator, send, event):
        self.sim = simulator
        self.send = send
        self.event = event
        self.queue = deque()
        self.pending = None
        self.list_ready = False

    def enqueue(self, frame):
        # Keep the newest unsent snapshot per device; never alter an in-flight request.
        if frame.message_type == STATE_SNAPSHOT:
            self.queue = deque(q for q in self.queue
                               if not (q.message_type == STATE_SNAPSHOT and q.device == frame.device))
        self.queue.append(frame)

    def tick(self, now=None):
        now = time.monotonic() if now is None else now
        if self.pending:
            frame, sent, retries = self.pending
            if now - sent >= 0.5:
                if retries == 3:
                    self.event("request_timeout", type=hex(frame.message_type), device=frame.device,
                               sequence=frame.sequence)
                    self.pending = None
                else:
                    frame = replace(frame, flags=frame.flags | 0x20)
                    self.send(frame)
                    self.pending = (frame, now, retries + 1)
        elif self.queue:
            frame = self.queue.popleft()
            self.send(frame)
            self.pending = (frame, now, 0)

    def receive(self, frame):
        if frame.message_type in REPLIES:
            if not self.pending:
                return
            request = self.pending[0]
            if (frame.message_type, frame.sequence, frame.session, frame.device,
                frame.endpoint, frame.binding, frame.cluster, frame.ident) != (
                    request.message_type + 1, request.sequence, request.session, request.device,
                    request.endpoint, request.binding, 0, 0):
                return
            if not frame.flags & RESPONSE or not frame.payload:
                return
            status = frame.payload[0]
            if bool(frame.flags & ERROR) != bool(status) or len(frame.payload) != (
                    1 if status else REPLIES[frame.message_type]):
                return
            if status == BUSY:
                return  # Preserve the original deadline and bounded retries.
            self.pending = None
            self.event("ack", type=hex(frame.message_type), device=frame.device, status=status,
                       sequence=frame.sequence, payload=frame.payload.hex(" "))
            if status:
                return
            if frame.message_type == REMOVE_RESPONSE:
                self.sim.devices.pop(frame.device, None)
                self.sim.binding.pop(frame.device, None)
                self.sim._save()
            elif frame.message_type == ONLINE_RESPONSE:
                d = self.sim.devices[frame.device]
                ep, binding = self.sim.binding[d.uart_device_id]
                self.enqueue(self.sim._snapshot(d, ep, binding))
            return
        if frame.message_type == HELLO_REQUEST and not frame.flags & 0x20:
            self.pending = None
            self.queue.clear()
            self.list_ready = False
            self.sim.binding.clear()
            self.sim.cache.clear()
            self.sim.list_cache = None
        if frame.message_type == BIND_REQUEST and frame.flags & 0x20:
            key = (frame.message_type, frame.sequence, frame.device)
            cached = self.sim.cache.get(key)
            if cached and cached[0] == replace(frame, flags=frame.flags & 0x1F):
                replies = self.sim._handle(frame)  # Bind retry returns current state.
            else:
                replies = self.sim.handle(frame)
        else:
            replies = self.sim.handle(frame)
        for response in replies:
            if response.message_type == STATE_SNAPSHOT:
                self.enqueue(response)
            else:
                self.send(response)
        if frame.message_type == LIST_REQUEST and replies:
            self.list_ready = True
        if frame.message_type in (BIND_REQUEST, COMMAND):
            self.event("devices", devices=self.devices())

    def devices(self):
        return [dict(asdict(d), endpoint=self.sim.binding.get(d.uart_device_id, (None, None))[0],
                     binding=self.sim.binding.get(d.uart_device_id, (None, None))[1])
                for d in self.sim.devices.values()]

    def action(self, action):
        op = action["op"]
        if op == "list":
            self.event("devices", devices=self.devices(), list_ready=self.list_ready)
            return
        if not self.list_ready or self.pending or self.queue:
            raise ValueError("wait for HELLO/list and outstanding notification/snapshot ACKs")
        device_id = int(action["id"])
        if not 0 < device_id <= 0xFFFFFFFF:
            raise ValueError("id must be a nonzero uint32")
        if op == "add":
            if device_id in self.sim.devices:
                raise ValueError("device already exists; use a distinct id")
            kind = action["kind"]
            if kind not in ("cw", "hsct"):
                raise ValueError("kind must be cw or hsct")
            d = Device(device_id, 0x10 if kind == "cw" else 0x11,
                       CAP_CW if kind == "cw" else CAP_HS_CT,
                       color_mode=2 if kind == "cw" else 0)
            self.sim.devices[device_id] = d
            self.sim.list_version = self.sim.list_version % 0xFFFFFFFF + 1
            self.sim._save()
            payload = u8(d.device_type) + u32(d.capability_flags) + u32(d.state_version) + u8(d.state_flags) + bytes([2, 0, 0])
            frame = Frame(ADD_NOTIFY, ACK_REQUIRED, self.sim._next_sequence(), self.sim.session,
                          device_id, 0, 0, 0, 0, payload)
        else:
            d = self.sim.devices[device_id]
            ep, binding = self.sim.binding[device_id]
            if d.pending_remove:
                raise ValueError("device pending removal")
            updated = replace(d, state_version=d.state_version % 0xFFFFFFFF + 1)
            if op == "state":
                if not d.online:
                    raise ValueError("online + snapshot required first")
                values = action["values"]
                allowed = {"on", "level", "hue", "saturation", "color_temperature"}
                if not values or set(values) - allowed:
                    raise ValueError("unknown or empty state fields")
                if "color_temperature" in values and ({"hue", "saturation"} & values.keys()):
                    raise ValueError("cannot mix HS and CT")
                for key, value in values.items():
                    low, high = ((0, 1) if key == "on" else (1, 254) if key == "level"
                                 else (154, 454) if key == "color_temperature" else (0, 254))
                    if type(value) is not int or not low <= value <= high:
                        raise ValueError("invalid " + key)
                    if key in ("hue", "saturation") and d.device_type != 0x11:
                        raise ValueError("CW has no HS")
                    setattr(updated, key, value)
                if "color_temperature" in values:
                    updated.color_mode = 2
                elif {"hue", "saturation"} & values.keys():
                    updated.color_mode = 0
                frame = self.sim._snapshot(updated, ep, binding)
            elif op in ("online", "offline", "remove"):
                if op == "remove":
                    updated.pending_remove = True
                    self.sim.list_version = self.sim.list_version % 0xFFFFFFFF + 1
                    payload = u8(1) + u32(updated.state_version) + bytes(3)
                    message_type = REMOVE_NOTIFY
                else:
                    updated.online = op == "online"
                    payload = u8(0) + u32(updated.state_version) + u8(updated.state_flags) + bytes(2)
                    message_type = ONLINE_NOTIFY if updated.online else OFFLINE_NOTIFY
                frame = Frame(message_type, ACK_REQUIRED, self.sim._next_sequence(), self.sim.session,
                              device_id, ep, binding, 0, 0, payload)
            else:
                raise ValueError("unknown op")
            self.sim.devices[device_id] = updated
            self.sim._save()
        self.enqueue(frame)
        self.event("action", action=action)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--console", default="/dev/ttyUSB2")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    wire = (args.output / "uart.jsonl").open("a", buffering=1)
    actions = (args.output / "actions.jsonl").open("a", buffering=1)
    console = (args.output / "bridge.log").open("ab", buffering=0)
    ports = {}
    def event(kind, **data):
        record = dict(time=time.time(), monotonic=time.monotonic(), event=kind, **data)
        line = json.dumps(record, sort_keys=True)
        actions.write(line + "\n")
        print(line, flush=True)
    def open_port(path, speed):
        fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
        previous = termios.tcgetattr(fd)
        ports[fd] = previous
        fcntl.ioctl(fd, termios.TIOCEXCL)
        tty.setraw(fd)
        attrs = termios.tcgetattr(fd)
        attrs[4] = attrs[5] = speed
        attrs[2] &= ~termios.CRTSCTS
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        return fd
    def log_frame(direction, frame):
        record = dict(time=time.time(), monotonic=time.monotonic(), direction=direction,
                      type=hex(frame.message_type), sequence=frame.sequence,
                      session=frame.session, device=frame.device, endpoint=frame.endpoint,
                      binding=frame.binding, flags=frame.flags, raw_hex=frame.encode().hex(" "),
                      **describe(frame))
        line = json.dumps(record, sort_keys=True)
        wire.write(line + "\n")
        if frame.message_type not in (0x42, 0x43):
            print(line, flush=True)
    try:
        uart = open_port(args.port, termios.B921600)
        log_fd = open_port(args.console, termios.B2000000)
        def send(frame):
            data = memoryview(frame.encode())
            while data:
                count = os.write(uart, data)
                if count <= 0:
                    raise OSError("short UART write")
                data = data[count:]
            log_frame("tx", frame)
        sim = PhoneSimulator(Path(__file__).parent / "fixtures/empty.json",
                             args.output / "mcu-state.json", None)
        session = Session(sim, send, event)
        event("started", port=args.port, console=args.console, app="user-developed",
              session=sim.session, note="Existing phone commissioning preserved; empty MCU list.")
        rx = bytearray()
        last_rx = time.monotonic()
        stdin_open = True
        while True:
            watched = [uart, log_fd] + ([sys.stdin] if stdin_open else [])
            readable = select.select(watched, [], [], 0.05)[0]
            if log_fd in readable:
                console.write(os.read(log_fd, 8192))
            if sys.stdin in readable:
                line = sys.stdin.readline()
                if not line:
                    stdin_open = False  # Keep responding to heartbeat after frontend detaches.
                else:
                    try:
                        action = json.loads(line)
                        if action.get("op") == "quit":
                            break
                        session.action(action)
                    except (ValueError, KeyError, TypeError) as error:
                        event("action_error", error=str(error))
            if uart in readable:
                rx.extend(os.read(uart, 4096))
                last_rx = time.monotonic()
            elif rx and time.monotonic() - last_rx >= 0.1:
                event("partial_frame_timeout", raw_hex=rx.hex(" "))
                rx.clear()
            while len(rx) >= 35:
                if rx[:2] != b"\xA5\x5A":
                    del rx[0]
                    continue
                length = struct.unpack_from("<H", rx, 29)[0]
                if length > 1024:
                    del rx[0]
                    continue
                if len(rx) < 35 + length:
                    break
                try:
                    frame = Frame.decode(bytes(rx[:35 + length]))
                except ValueError as error:
                    event("wire_error", error=str(error))
                    del rx[0]
                    continue
                del rx[:35 + length]
                log_frame("rx", frame)
                session.receive(frame)
            session.tick()
    except KeyboardInterrupt:
        event("stopped", reason="keyboard")
    finally:
        for fd, attrs in ports.items():
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            fcntl.ioctl(fd, termios.TIOCNXCL)
            os.close(fd)
        wire.close()
        actions.close()
        console.close()


if __name__ == "__main__":
    main()
