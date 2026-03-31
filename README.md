# Quadr

Quadr is a lightweight pre-filter compression layer designed to improve compression of numeric and time-series data before LZ77 / entropy coding. This repository contains a C implementation (libs/quadr-core) and a CLI tool (app/quadr) for encoding/decoding files using the Quadr v1.5 protocol.

Features
- Block-based pre-filter transforms (Delta, RLE, optional Byte Shuffle)
- Streaming encode/decode API
- Pluggable backends (zlib-ng, zstd, lz4) via CLI adapters

Quick build (Linux / Windows using CMake + Ninja)

```bash
# Configure & build
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run tests
cd build
ctest --output-on-failure -C Release
```

CLI usage (after build)

```powershell
# compress
build\app\quadr.exe encode input.jpg output.qdr
# decompress
build\app\quadr.exe decode output.qdr out.jpg
# show file info
build\app\quadr.exe info output.qdr
# verify integrity
build\app\quadr.exe verify output.qdr
```

More information: see `Quadr_v1.5_规范.md` for the full protocol specification (English and Chinese).
