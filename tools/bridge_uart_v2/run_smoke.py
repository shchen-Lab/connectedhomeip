#!/usr/bin/env python3
"""Real UART + chip-tool smoke test. Run only after flashing the matching firmware."""
import argparse
import fcntl
import json
import os
import pathlib
import re
import subprocess
import termios
import threading
import time
from mcu_simulator import Simulator

ANSI = re.compile(r"\x1b\[[0-9;]*m")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyUSB0")
    parser.add_argument("--chip-tool", type=pathlib.Path, required=True)
    parser.add_argument("--node", default="1234")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--devices", type=pathlib.Path,
                        default=pathlib.Path(__file__).parent / "fixtures/cw_and_hs.json")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    sim = Simulator(args.devices, args.output / "mcu-state.json", args.output / "uart.jsonl")
    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY)
    fcntl.ioctl(fd, termios.TIOCEXCL)
    failures = []
    results = []
    thread_errors = []
    def serve():
        try:
            sim.run(fd)
        except Exception as error:
            thread_errors.append(str(error))
    thread = threading.Thread(target=serve, daemon=True)
    thread.start()

    def chip(label, words, expected=None):
        if thread_errors:
            raise RuntimeError("MCU simulator failed: " + thread_errors[0])
        command = [str(args.chip_tool.resolve()), *words, args.node, str(endpoint)]
        started = time.time()
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=25)
        text = ANSI.sub("", result.stdout)
        with (args.output / "matter.log").open("a") as log:
            log.write("\n$ " + " ".join(command) + "\n" + text)
        passed = result.returncode == 0 and "Run command failure" not in text
        if expected:
            passed = passed and re.search(expected, text) is not None
        record = {"test": label, "time": started, "command": command, "exit": result.returncode, "passed": passed}
        results.append(record)
        if not passed:
            failures.append(label)
            raise RuntimeError(label + " failed; see matter.log")
        return text

    try:
        # The Bridge probes HELLO after timeout; no reset or re-pairing is needed.
        deadline = time.monotonic() + 40
        while len(sim.binding) < len(sim.devices) and time.monotonic() < deadline:
            if thread_errors:
                raise RuntimeError(thread_errors[0])
            time.sleep(0.1)
        if len(sim.binding) != len(sim.devices):
            raise RuntimeError("No complete UART binding within 40 seconds")
        endpoint = 0
        chip("root-parts", ["descriptor", "read", "parts-list"])
        for device_id, device in sim.devices.items():
            endpoint = sim.binding[device_id][0]
            # Wait for the initial snapshot acknowledgement before commands.
            deadline = time.monotonic() + 5
            while (sim.pending_snapshot or sim.snapshot_queue) and time.monotonic() < deadline:
                time.sleep(0.1)
            chip(f"{device_id}-descriptor", ["descriptor", "read", "device-type-list"])
            chip(f"{device_id}-reachable", ["bridgeddevicebasicinformation", "read", "reachable"], r"Reachable:\s*(?:TRUE|true|1)")
            chip(f"{device_id}-off", ["onoff", "off"])
            chip(f"{device_id}-off-read", ["onoff", "read", "on-off"], r"OnOff:\s*(?:FALSE|false|0)")
            chip(f"{device_id}-on", ["onoff", "on"])
            chip(f"{device_id}-on-read", ["onoff", "read", "on-off"], r"OnOff:\s*(?:TRUE|true|1)")
            chip(f"{device_id}-level", ["levelcontrol", "move-to-level-with-on-off", "123", "0"])
            chip(f"{device_id}-level-read", ["levelcontrol", "read", "current-level"], r"CurrentLevel:\s*123\b")
            if device.device_type == 0x11:
                chip(f"{device_id}-feature", ["colorcontrol", "read", "feature-map"], r"FeatureMap:\s*(?:17|0x0*11)\b")
                chip(f"{device_id}-hs", ["colorcontrol", "move-to-hue-and-saturation", "80", "140", "0"])
                chip(f"{device_id}-hue-read", ["colorcontrol", "read", "current-hue"], r"CurrentHue:\s*80\b")
                chip(f"{device_id}-sat-read", ["colorcontrol", "read", "current-saturation"], r"CurrentSaturation:\s*140\b")
                chip(f"{device_id}-hs-mode", ["colorcontrol", "read", "color-mode"], r"ColorMode:\s*0\b")
            chip(f"{device_id}-ct", ["colorcontrol", "move-to-color-temperature", "300", "0"])
            chip(f"{device_id}-ct-read", ["colorcontrol", "read", "color-temperature-mireds"], r"ColorTemperatureMireds:\s*300\b")
            chip(f"{device_id}-ct-mode", ["colorcontrol", "read", "color-mode"], r"ColorMode:\s*2\b")
    except Exception as error:
        failures.append(str(error))
    finally:
        sim.running = False
        thread.join(timeout=2)
        os.close(fd)
        report = {"passed": not failures, "failures": failures, "tests": results,
                  "bindings": sim.binding, "note": "Immediate HS/CT smoke only; no timing, power-loss or capacity sign-off."}
        (args.output / "summary.json").write_text(json.dumps(report, indent=2))
    print(json.dumps({"passed": not failures, "failures": failures}, indent=2))
    return 1 if failures else 0

if __name__ == "__main__":
    raise SystemExit(main())
