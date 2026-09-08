#!/usr/bin/env bash
set -eu

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${TMPDIR:-/tmp}/bridge-uart-v2-tests
mkdir -p "$build_dir"
gcc -std=c11 -Wall -Wextra -Werror \
  -I"$root_dir/examples/bridge-app/bouffalolab/common" \
  "$root_dir/examples/bridge-app/bouffalolab/common/LightUartProtocol.c" \
  "$root_dir/tests/bridge_uart_v2/test_protocol.c" \
  -o "$build_dir/test_protocol"
"$build_dir/test_protocol"

g++ -std=c++17 -Wall -Wextra -Werror \
  -I"$root_dir/examples/bridge-app/bouffalolab/common" \
  "$root_dir/examples/bridge-app/bouffalolab/common/BridgeUartLifecycle.cpp" \
  "$root_dir/tests/bridge_uart_v2/test_lifecycle.cpp" \
  -o "$build_dir/test_lifecycle"
"$build_dir/test_lifecycle"
