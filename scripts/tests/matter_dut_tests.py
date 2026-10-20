#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys


REPO_ROOT = Path(__file__).resolve().parents[2]
RUNNER = REPO_ROOT / "tools/matter_dut/matter_dut_tests.py"
raise SystemExit(subprocess.call([sys.executable, str(RUNNER), *sys.argv[1:]]))
