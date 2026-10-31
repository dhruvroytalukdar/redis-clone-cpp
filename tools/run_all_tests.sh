#!/usr/bin/env bash
# The one command that builds everything and runs every test suite.
# Later modules add integration and stress tests here.
set -euo pipefail

cd "$(dirname "$0")/.."

cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
