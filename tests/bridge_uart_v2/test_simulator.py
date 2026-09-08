import contextlib
import dataclasses
import io
import pathlib
import struct
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/bridge_uart_v2"))
from protocol import Frame, ACK_REQUIRED, crc32
from mcu_simulator import Simulator, HELLO_REQUEST, LIST_REQUEST, BIND_REQUEST, COMMAND, STATE_SNAPSHOT


class SimulatorContractTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.sim = Simulator(ROOT / "tools/bridge_uart_v2/fixtures/cw_and_hs.json",
                             pathlib.Path(self.tmp.name) / "state.json", None)
        self.seq = 10
        self.call(Frame(HELLO_REQUEST, ACK_REQUIRED, 1, 0, 0, 0, 0, 0, 0,
                        struct.pack("<BBIIHH", 2, 0, 42, 0, 1024, 1059)))
        self.bind(2, 4, 9, 0x11, 0x17)

    def call(self, frame):
        with contextlib.redirect_stdout(io.StringIO()):
            return self.sim.handle(frame)

    def bind(self, device, endpoint, binding, kind, cap):
        return self.call(Frame(BIND_REQUEST, ACK_REQUIRED, 2, self.sim.session, device, endpoint, binding, 0, 0,
                               struct.pack("<BIBI", kind, cap, 1, 0)))

    def command(self, cluster, ident, payload=b""):
        self.seq += 1
        return Frame(COMMAND, ACK_REQUIRED, self.seq, self.sim.session, 2, 4, 9, cluster, ident, payload)

    def test_hs_snapshot_has_ack_and_complete_mask(self):
        frames = self.bind(2, 4, 9, 0x11, 0x17)
        snapshot = frames[1]
        self.assertEqual(snapshot.message_type, STATE_SNAPSHOT)
        self.assertEqual(snapshot.flags, ACK_REQUIRED)
        self.assertEqual(len(snapshot.payload), 10)
        self.assertEqual(struct.unpack_from("<H", snapshot.payload, 4)[0], 0x0F)

    def test_hs_and_ct_update_actual_state(self):
        frames = self.call(self.command(0x300, 6, bytes([80, 140, 0, 0, 0, 0])))
        self.assertEqual(frames[0].payload[0], 0)
        self.assertEqual(frames[1].payload[8:], bytes([80, 140]))
        frames = self.call(self.command(0x300, 0x0A, struct.pack("<HHBB", 300, 0, 0, 0)))
        self.assertEqual(struct.unpack_from("<H", frames[1].payload, 4)[0], 0x43)
        self.assertEqual(struct.unpack_from("<H", frames[1].payload, 8)[0], 300)

    def test_duplicate_toggle_executes_once(self):
        request = self.command(6, 2)
        first = self.call(request)
        state = dataclasses.asdict(self.sim.devices[2])
        second = self.call(dataclasses.replace(request, flags=ACK_REQUIRED | 0x20))
        self.assertEqual(first, second)
        self.assertEqual(dataclasses.asdict(self.sim.devices[2]), state)

    def test_old_binding_rejected_without_mutation(self):
        request = dataclasses.replace(self.command(6, 2), binding=8)
        state = dataclasses.asdict(self.sim.devices[2])
        frames = self.call(request)
        self.assertEqual(frames[0].payload, bytes([5]))
        self.assertEqual(dataclasses.asdict(self.sim.devices[2]), state)

    def test_xy_and_unimplemented_transition_are_rejected(self):
        state = dataclasses.asdict(self.sim.devices[2])
        for ident, payload in [(7, bytes(8)), (6, bytes([50, 80, 10, 0, 0, 0]))]:
            frames = self.call(self.command(0x300, ident, payload))
            self.assertEqual(frames[0].payload, bytes([0x0C]))
        self.assertEqual(dataclasses.asdict(self.sim.devices[2]), state)

    def test_invalid_hs_and_ct_are_rejected(self):
        for ident, payload in [(6, bytes([255, 80, 0, 0, 0, 0])),
                               (0x0A, struct.pack("<HHBB", 500, 0, 0, 0))]:
            frames = self.call(self.command(0x300, ident, payload))
            self.assertEqual(frames[0].payload, bytes([1]))

    def test_list_checksum_covers_entries(self):
        frames = self.call(Frame(LIST_REQUEST, ACK_REQUIRED, 3, self.sim.session, 0, 0, 0, 0, 0,
                                 struct.pack("<IIB3x", 7, 0, 0)))
        expected = crc32(b"".join(frame.payload for frame in frames[1:-1]))
        self.assertEqual(struct.unpack_from("<I", frames[0].payload, 12)[0], expected)
        self.assertEqual(struct.unpack_from("<I", frames[-1].payload, 6)[0], expected)

    def test_new_bridge_boot_invalidates_bindings(self):
        self.call(Frame(HELLO_REQUEST, ACK_REQUIRED, 4, 0, 0, 0, 0, 0, 0,
                        struct.pack("<BBIIHH", 2, 0, 43, 0, 1024, 1059)))
        self.assertEqual(self.sim.binding, {})
        frames = self.call(self.command(6, 1))
        self.assertEqual(frames[0].payload, bytes([5]))


if __name__ == "__main__":
    unittest.main()
