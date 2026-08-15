#!/usr/bin/env sh

set -eu

cmake -S . -B build
cmake --build build
exec ./build/redis "$@"
