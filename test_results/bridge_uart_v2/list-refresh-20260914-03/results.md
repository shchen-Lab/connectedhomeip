# UART list refresh and re-sync hardware result

Date: 2026-09-14. The Bridge had already been commissioned. This test did not reset or modify Wi-Fi commissioning.

## Scope

The MCU simulator used UART1 (`/dev/ttyUSB0`) at 921600. Bridge logs were captured from `/dev/ttyUSB2`.

## Results

| Check | Result | Evidence |
| --- | --- | --- |
| Same-session list refresh | PASS | Heartbeat version changes caused `DEVICE_LIST_REQUEST` at UART lines 22 and 38, with no HELLO or global disconnect after the first successful session. |
| Existing binding retained | PASS | ID 1 stayed endpoint 3/binding 10 and ID 3 stayed endpoint 4/binding 11. The full list refresh at lines 38-42 has no subsequent `DEVICE_BIND_REQUEST`. |
| No unrelated Reachable flap | PASS | After ID 3 became online, the only later offline logs are ID 1 at `bridge.log:56` and `bridge.log:61`; ID 3 is not taken offline by list refresh or ID 1 recovery. |
| Re-sync no-snapshot bound | PASS | ID 1 sent exactly three `DEVICE_STATE_REQUEST` frames (UART lines 51, 55, 57), each received OK and no snapshot. Bridge logged one exhaustion at `bridge.log:77`, then no further requests in the captured interval. |
| Exhaustion log throttling | PASS | One `UART re-sync exhausted` record appears after the third timeout; the previous per-second logging did not recur. |

## Notes

- The test began while a previous test session's HELLO retransmission was still outstanding. The fresh simulator correctly returned BUSY until Bridge issued a new HELLO; this did not affect the completed test sequence.
- A newly added device receives a bind snapshot and an immediate state request, producing a duplicate same-version snapshot. This is a pre-existing startup-flow inefficiency observed in the UART trace, not a regression from list refresh. It was not changed in this fix.

Raw evidence: `uart.jsonl`, `actions.jsonl`, and `bridge.log` in this directory.
