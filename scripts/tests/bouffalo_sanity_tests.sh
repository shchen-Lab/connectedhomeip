#!/usr/bin/env bash

CHIP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec bash "$CHIP_ROOT/tools/matter_dut/bouffalo_sanity_tests.sh" "$@"
