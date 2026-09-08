import pathlib
import struct
import sys
import tempfile
import unittest
from dataclasses import replace

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/bridge_uart_v2"))
from phone_session import Session, PhoneSimulator
from mcu_simulator import HELLO_REQUEST, LIST_REQUEST, BIND_REQUEST, STATE_SNAPSHOT
from protocol import Frame, ACK_REQUIRED, RESPONSE, ERROR


class PhoneSessionTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = pathlib.Path(self.tmp.name) / "state.json"
        self.sim = PhoneSimulator(ROOT / "tools/bridge_uart_v2/fixtures/empty.json", self.path, None)
        self.sent, self.events = [], []
        self.live = Session(self.sim, self.sent.append, lambda name, **kw: self.events.append((name, kw)))
        self.live.receive(Frame(HELLO_REQUEST, ACK_REQUIRED, 1, 0, 0, 0, 0, 0, 0,
                                struct.pack("<BBIIHH", 2, 0, 42, 0, 1024, 1059)))
        self.live.receive(Frame(LIST_REQUEST, ACK_REQUIRED, 2, self.sim.session, 0, 0, 0, 0, 0,
                                struct.pack("<III", 1, 0, 0)))

    def ack(self, payload, **changes):
        q = self.live.pending[0]
        response = replace(q, message_type=q.message_type + 1,
                           flags=RESPONSE | (ERROR if payload[0] else 0), payload=payload, **changes)
        self.live.receive(response)

    def bound(self):
        self.live.action({"op": "add", "id": 1, "kind": "cw"})
        self.live.tick(1)
        self.ack(struct.pack("<BHI", 0, 3, 10))
        self.live.receive(Frame(BIND_REQUEST, ACK_REQUIRED, 3, self.sim.session, 1, 3, 10, 0, 0,
                                struct.pack("<BIBI", 0x10, 0x13, 1, 0)))
        self.live.tick(2)
        self.ack(struct.pack("<BI", 0, 1))

    def test_online_add_waits_for_bind_before_snapshot(self):
        self.live.action({"op": "add", "id": 1, "kind": "cw"})
        self.live.tick(1)
        self.assertEqual(self.sent[-1].message_type, 0x36)
        self.assertEqual(len(self.sent[-1].payload), 13)
        self.ack(struct.pack("<BHI", 0, 3, 10))
        self.assertFalse(self.live.queue)
        self.assertFalse(self.sim.binding)
        self.live.receive(Frame(BIND_REQUEST, ACK_REQUIRED, 3, self.sim.session, 1, 3, 10, 0, 0,
                                struct.pack("<BIBI", 0x10, 0x13, 1, 0)))
        self.assertEqual(self.sent[-1].message_type, 0x3B)
        self.live.tick(2)
        self.assertEqual(self.sent[-1].message_type, STATE_SNAPSHOT)
        self.assertEqual((self.sent[-1].endpoint, self.sent[-1].binding), (3, 10))

    def test_mismatched_reply_does_not_complete_pending(self):
        self.live.action({"op": "add", "id": 1, "kind": "cw"})
        self.live.tick(1)
        self.ack(struct.pack("<BHI", 0, 3, 10), binding=99)
        self.assertIsNotNone(self.live.pending)

    def test_busy_cannot_extend_deadline_forever(self):
        self.live.action({"op": "add", "id": 1, "kind": "cw"})
        self.live.tick(1)
        for now in (1.5, 2, 2.5, 3):
            self.ack(bytes([7]))
            self.live.tick(now)
        self.assertIsNone(self.live.pending)
        self.assertEqual(len([f for f in self.sent if f.message_type == 0x36]), 4)
        self.assertEqual(self.events[-1][0], "request_timeout")

    def test_online_sends_snapshot_only_after_ack(self):
        self.bound()
        self.live.action({"op": "offline", "id": 1})
        self.live.tick(3)
        self.ack(bytes([0]))
        self.live.action({"op": "online", "id": 1})
        self.live.tick(4)
        self.assertFalse(self.live.queue)
        self.ack(bytes([0]))
        self.assertEqual(self.live.queue[0].message_type, STATE_SNAPSHOT)

    def test_remove_persists_until_success(self):
        self.bound()
        self.live.action({"op": "remove", "id": 1})
        self.live.tick(3)
        restored = PhoneSimulator(ROOT / "tools/bridge_uart_v2/fixtures/empty.json", self.path, None)
        self.assertTrue(restored.devices[1].pending_remove)
        self.ack(bytes([0]))
        self.assertNotIn(1, self.sim.devices)
        restored = PhoneSimulator(ROOT / "tools/bridge_uart_v2/fixtures/empty.json", self.path, None)
        self.assertNotIn(1, restored.devices)

    def test_invalid_state_is_not_partially_applied(self):
        self.bound()
        before = replace(self.sim.devices[1])
        with self.assertRaises(ValueError):
            self.live.action({"op": "state", "id": 1, "values": {"level": 100, "hue": 80}})
        self.assertEqual(before, self.sim.devices[1])
        self.assertFalse(self.live.queue)


if __name__ == "__main__":
    unittest.main()
