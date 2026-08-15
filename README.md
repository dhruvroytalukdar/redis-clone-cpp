# Redis clone (C++)

A Redis-compatible server built in C++.

For the feature-by-feature implementation plan and the concepts behind it, see
[the build walkthrough](docs/BUILD_GUIDE.md).

## Build

Requirements: CMake 3.20+ and a C++23-capable compiler.

```sh
cmake -S . -B build
cmake --build build
```

## Run

```sh
./build/redis
```

The server entry point is [src/main.cpp](src/main.cpp).
