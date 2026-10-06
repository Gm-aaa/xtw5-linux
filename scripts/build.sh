#!/usr/bin/env bash
set -euo pipefail
xtw_source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$xtw_source_dir" -B "$xtw_source_dir/build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON
cmake --build "$xtw_source_dir/build" --parallel 6
ctest --test-dir "$xtw_source_dir/build" --output-on-failure
