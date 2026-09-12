#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cargo build
cargo build --release
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 2
