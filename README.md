# Quadr

> **Quadr Compression Protocol v1.5** — 高性能无损数据压缩工具

Quadr 不是简单的压缩库封装，而是实现了**自定义预处理变换层**，在数据送入压缩后端之前先进行智能变换，从而获得比传统压缩更好的压缩率。

---

## 特性

| 特性 | 说明 |
|:---|:---|
| **预处理变换** | Delta / XOR / Byte Shuffle / RLE 四种变换，降低数据熵 |
| **压缩后端** | zlib-ng / zstd / lz4 / lz4hc / 7z(LZMA) / passthrough |
| **SIMD 加速** | AVX2 / SSSE3 / SSE2 / NEON 四级运行时检测与加速 |
| **流式 API** | 支持任意大小文件，不会将整个文件加载到内存 |
| **归档格式** | `.qar` 多文件归档，支持并行编解码 |
| **智能探测** | `probe` 机制自动分析数据并推荐最优编码参数 |
| **完整性校验** | XXH3-64 哈希，每个 block 独立校验 |
| **跨平台** | Windows (MinGW)、Linux |

---

## 架构

```
┌─────────────────────────────────────────────────────────┐
│                     quadr (CLI)                         │
│                                                         │
│   main.c ──┬── cmd/cmd_encode.c    ──┐                  │
│            ├── cmd/cmd_decode.c      │                  │
│            ├── cmd/cmd_info.c        │   quadr_console  │
│            ├── cmd/cmd_bench.c       ├──► ANSI 终端输出  │
│            ├── cmd/cmd_verify.c      │                  │
│            ├── cmd/cmd_probe.c       │                  │
│            ├── cmd/cmd_pack.c        │                  │
│            ├── cmd/cmd_unpack.c      │                  │
│            └── cmd/cmd_list.c      ──┘                  │
├─────────────────────────────────────────────────────────┤
│                  libs/quadr-logic                       │
│                                                         │
│  logic_encode    logic_decode    logic_info             │
│  logic_bench     logic_verify    logic_probe            │
│  logic_pack      logic_unpack    logic_list             │
│  logic_common                                           │
├─────────────────────────────────────────────────────────┤
│                   libs/quadr-core                       │
│                                                         │
│  ┌──────────────┐  ┌─────────────────┐  ┌────────────┐  │
│  │ quadr_core   │  │ quadr_stream    │  │ quadr_     │  │
│  │ quadr_delta_ │  │ quadr_backend   │  │ archive    │  │
│  │ simd         │  │ quadr_xxh3      │  │            │  │
│  │ quadr_entropy│  │                 │  │            │  │
│  │ _simd        │  │                 │  │            │  │
│  └──────────────┘  └─────────────────┘  └────────────┘  │
│                                                         │
│  tests/  (128 个单元测试)    fuzz/                       │
└─────────────────────────────────────────────────────────┘
```

---

## 变换类型

| 变换 | 适用场景 | 原理 |
|:---|:---|:---|
| **Delta** | 数值序列（传感器数据、PCM 音频） | `out[i] = in[i] - in[i-stride]` |
| **XOR** | 浮点数、指针、哈希类数据 | `out[i] = in[i] ^ in[i-stride]` |
| **Byte Shuffle** | 多字节数值（16/32/64 位） | 按字节重新排列，将相同字节位置聚集 |
| **RLE** | 存在连续重复的数据 | 游程编码 |
| **Passthrough** | 已压缩/高熵数据 | 不变换，直接传递 |

---

## 快速开始

### 构建

```bash
# 依赖: vcpkg (zlib-ng, zstd, lz4, liblzma)
cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Release

# 运行测试
ctest --test-dir build
```

### 发布打包

```bash
# Windows → quadr-1.5.0-windows-x64.zip (含 exe + DLL + 静态库 + 头文件)
cmake --build build --config Release --target package-release

# Linux  → quadr-1.5.0-linux-x64.tar.gz (含 binary + .a + 头文件)
cmake --build cmake-build-linux-release --config Release --target package-release
```

### 基本用法

```bash
quadr encode input.bin output.qdr       # 压缩文件（默认 zstd L3）
quadr decode output.qdr restored.bin    # 解压文件
quadr info output.qdr                   # 查看文件元信息
quadr bench input.bin                   # 基准测试
quadr verify output.qdr                 # 验证文件完整性
quadr probe input.bin                   # 分析数据并推荐编码参数
quadr pack -o archive.qar f1.bin f2.bin # 打包多文件归档
quadr unpack archive.qar ./output/      # 解包归档
quadr list archive.qar                  # 列出归档内容
```

---

## 压缩选项

| 选项 | 说明 | 默认值 |
|:---|:---|:---|
| `--backend=<name>` | 压缩后端: `none`, `zlib-ng`, `zstd`, `lz4`, `lz4hc`, `7z` | `zstd` |
| `--level=<N>` | 后端压缩级别 | 每后端默认 |
| `--block=<KB>` | Block 大小 (KB)，范围 4–256 | `64` |
| `--hint=<type>` | 数据类型提示: `generic`, `image`, `audio`, `sensor`, `float` | `generic` |
| `--xbit=<N>` | 采样位宽: `8`, `16`, `32`, `64` | `8` |
| `--stride=<N>` | 步长提示 (1–8) | 自动 |
| `--fast` / `--no-fast` | 快速 / 完整探测 | `fast` |
| `--mixed-backend` | Delta 块用默认后端，Passthrough 块用 lz4 | 关闭 |
| `--adaptive-block` | 自动选择 block size | 关闭 |
| `--parallel` | 并行编码 | 关闭 |
| `--threads=<N>` | 线程数 | 自动 |

---

## 示例

```bash
# 最大压缩比（文本文件可优于 zip）
quadr encode --backend=zstd --level=9 input.html output.qdr

# 最快速度
quadr encode --backend=lz4 input.bin output.qdr

# 浮点传感器数据
quadr encode --hint=float --xbit=32 sensor.raw output.qdr

# PCM 音频（Delta 变换效果显著）
quadr encode --hint=audio --xbit=16 audio.pcm output.qdr

# 并行编码大文件
quadr encode --parallel --threads=4 bigfile.bin output.qdr

# 混合后端（Delta 块用 zstd，Passthrough 块用 lz4）
quadr encode --mixed-backend input.bin output.qdr

# 指定 block 大小
quadr encode --block=32 input.bin output.qdr
```

---

## 后端对比

| ID | 后端 | 依赖库 | 特点 | 默认 Level |
|:--:|:---|:---|:---|:--:|
| 0 | Passthrough | — | 不压缩，仅预处理变换 | 0 |
| 1 | Zlib | zlib-ng | 兼容性好 | 6 |
| 2 | Zstd | zstd | 压缩比 / 速度平衡最佳 | 3 |
| 3 | LZ4 | lz4 | 极速解压 | 0 |
| 4 | LZ4HC | lz4 | 高压缩比 LZ4 | 9 |
| 5 | 7Z | liblzma | 最高压缩比 | 6 |

---

## 文件格式

### .qdr 单文件格式

```
┌──────────────────────────────────────────┐
│                File Header               │
│  ┌────────────────────────────────────┐  │
│  │ Magic       0x51554452 ("QUDR")    │  │
│  │ Version     0x15                   │  │
│  │ Total Size  uint64 LE              │  │
│  │ Block Count uint32 LE              │  │
│  │ Data Hint   uint8                  │  │
│  │ Hash Table  uint64[] (per block)   │  │
│  │ Offset Table uint64[] (per block)  │  │
│  └────────────────────────────────────┘  │
├──────────────────────────────────────────┤
│            Block 0 Frame                 │
│  ┌────────────────────────────────────┐  │
│  │ Backend ID      1 byte             │  │
│  │ Comp Size       4 bytes (LE)       │  │
│  │ Block Header    12 bytes           │  │
│  │ Compressed Data ...                │  │
│  └────────────────────────────────────┘  │
├──────────────────────────────────────────┤
│            Block 1 Frame                 │
│                 ...                      │
└──────────────────────────────────────────┘
```

### .qar 归档格式

```
Magic:      0x51415243 ("QARC")
Version:    0x01
Max files:  65535
Max path:   260 字符
```

---

## 使用 C API

```c
#include "quadr.h"

/* ── 流式编码 ─────────────────────────────────────────── */

QuadrEncodeOpts opts;
quadr_encode_opts_default(&opts);
opts.block_size = 64 * 1024;

QuadrStreamCtx *ctx = quadr_stream_encode_open(
    "output.qdr", &opts, QUADR_BACKEND_ZSTD, 3, total_bytes);

while (have_data) {
    quadr_stream_feed(ctx, buf, len);
}
quadr_stream_encode_close(ctx);

/* ── 流式解码 ─────────────────────────────────────────── */

QuadrStreamCtx *dctx = quadr_stream_decode_open("output.qdr", 0);
uint8_t out_buf[65536];
size_t written;

while (quadr_stream_pull(dctx, out_buf, sizeof(out_buf), &written) == QUADR_OK) {
    /* 处理 out_buf[0..written-1] */
}
quadr_stream_close(dctx);
```

---

## 依赖

| 依赖 | 用途 |
|:---|:---|
| zlib-ng | zlib 后端 |
| zstd | zstd 后端 |
| lz4 | lz4 / lz4hc 后端 |
| liblzma | 7z 后端 |

通过 vcpkg 管理：

```bash
vcpkg install zlib-ng zstd lz4 liblzma
```

---

## 许可证

GPL-2.0 — 详见 [LICENSE](LICENSE)

