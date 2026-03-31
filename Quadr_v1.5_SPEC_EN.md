Quadr v1.5 Specification
=========================

This document is an English translation and condensed README-style specification of the Quadr v1.5 protocol implemented in this repository.

Overview
--------
Quadr is a pre-filter layer that transforms numeric/time-series data before LZ77 + entropy coding to improve compressibility. It operates on independent blocks and supports streaming decode (one-pass), with an optional block-level byte-shuffle optimization for floating-point data.

Key constants (see code for exact definitions):
- Magic: 0x51554452 (ASCII "QUDR")
- Version: 0x15
- Default block size: 64 KB (configurable, clamp to 4 KB - 256 KB)
- Block header size: 12 bytes

File layout
-----------
Global header (at file start):
- Magic (4 bytes LE)
- Version (1 byte)
- Total uncompressed size (8 bytes LE)
- Block count (4 bytes LE)
- Data hint (1 byte)
- Hash table: N x 8 bytes (XXH3-64)
- Offset table: N x 8 bytes (absolute offsets to blocks)

Per-block frame (stream framing):
- 1 byte: backend_id
- 4 bytes LE: backend compressed size
- 12 bytes: Block Header
- comp_size bytes: payload (the pre-filter output possibly compressed by backend)

Block Header (12 bytes):
- [0..3] uncomp_size (uint32 LE)
- [4..7] comp_size (uint32 LE) -- pre-filter output size
- [8] flags: bits 0-1 = type, bit 2 = shuffle_flag, bits 3-6 = word_size
- [9] x_bit (8/16/32/64)
- [10] stride (in samples)
- [11] reserved (0)

Block types (2-bit):
- 00 DELTA
- 01 RLE
- 10 PASSTHROUGH
- 11 RAW

Payload formats
---------------
- DELTA
  - Path A (shuffle_flag=0): delta applied on samples with stride in samples; output sample-size depends on x_bit.
  - Path B (shuffle_flag=1): byte-shuffle by word_size followed by 8-bit delta (stride=1). Requires buffering the full block.
- RLE: (count, value) pairs: 1 byte count (1..255), 1 byte value. If RLE expands, encoder must use PASSTHROUGH.
- PASSTHROUGH/RAW: raw bytes.

Encoder requirements
--------------------
- Implement a probe that chooses between RLE, DELTA (candidate strides), and PASSTHROUGH using entropy estimates.
- Required candidate stride set: {1,2,3,4,6,8}. If hint_stride is provided, include it as a candidate.
- Byte shuffle + delta should be considered for float32/float64 or when data_hint suggests it.
- If delta's improvement (raw_h - best_h) <= 0.05, fall back to PASSTHROUGH.
- Clamp user-specified block sizes into [4 KB, 256 KB].

Decoder behavior
----------------
- Read the global header, then use offset_table to seek to blocks and read frame headers + blocks.
- For DELTA with shuffle_flag=0: inverse delta can be streamed.
- For DELTA with shuffle_flag=1: must buffer the block, inverse delta, then inverse shuffle.
- Verify output for each block using XXH3-64 hash; on mismatch, return an error.

Streaming backend
-----------------
- The stream frame stores backend_id and per-frame compressed size. Application must provide backend compress/decompress/bound callbacks. A default passthrough backend exists.
- Implementations should initialize decode context options (e.g., block_size) before using them in callbacks to avoid undefined behaviour.

CLI examples
------------
```
# build (Ninja)
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# compress
build/app/quadr encode input.jpg output.qdr
# decompress
build/app/quadr decode output.qdr out.jpg
```

Compatibility
-------------
- Quadr v1.5 changes block header bitfields vs v1.4 and is not backward compatible.

Contact
-------
See repository for code and tests. This spec mirrors the implementation in `libs/quadr-core` and the CLI in `app`.

