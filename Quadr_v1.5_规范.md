# Quadr 压缩协议格式规范（v1.5）

本文件为 Quadr v1.5 的格式与行为说明。Quadr 是一个面向数值型与时序数据的“预处理器”——
在 LZ77/熵编码之前对原始字节流进行变换以提高后级压缩率。此规范兼顾流式解码（one-pass）
与实现可移植性。

关键常量（参见代码定义）：
- 魔数（Magic）: 0x51554452 (ASCII "QUDR")
- 规范版本（Version）: 0x15
- 默认 Block Size: 64 KB (QUADR_BLOCK_SIZE_DEFAULT)
- 支持的 Block Size 范围: 4 KB … 256 KB (QUADR_BLOCK_SIZE_MIN / QUADR_BLOCK_SIZE_MAX)
- Block Header 大小: 12 字节 (QUADR_BLOCK_HEADER_SIZE)

目录
- 全局文件头（Global Header）
- 块（Block）布局与头部字段
- Block Type 与 Payload 格式
- 编码器行为（探针、stride、shuffle）
- 解压器工作流（流式解码约束）
- 实现说明与兼容性

---

1. 全局文件头（Global Header）

全局头位于文件开头，包含：
- Magic (4 bytes LE)
- Version (1 byte)
- Total uncompressed size (8 bytes LE)
- Block count (4 bytes LE)
- Data Hint (1 byte)
- Hash table: N × 8 bytes (XXH3-64 per block)
- Offset table: N × 8 bytes (absolute file offsets to each block)

全局头总大小 = 18 + 16 × block_count 字节。

Data Hint 是可选提示字段，提供给编码器以缩小探针候选集；解码器可忽略该字段。

常见 hint 值（非强制）示例：generic (0x00), image (0x01), audio (0x02), sensor (0x03), float (0x04), force_auto (0xFF)

---

2. 块（Block）布局

每个块在文件中按帧（frame）方式存放：

    [1 byte backend_id][4 bytes comp_size LE][12 bytes Block Header][comp_size bytes payload]

其中前 5 字节是流式后端（compression backend）相关的 frame header，随后是我们定义的 12 字节 Block Header。

Block Header 字段（按字节）：
- [0..3] uncomp_size (uint32 LE)
- [4..7] comp_size   (uint32 LE)  —— 注意：此处的 comp_size 在写入时为变换后（pre-filter）长度，随后 frame 前的 4 字节再次记录后端压缩后的大小
- [8] flags: bits 0-1 = type (2 bits), bit 2 = shuffle_flag, bits 3-6 = word_size
- [9] x_bit (8/16/32/64)
- [10] stride (采样单位)
- [11] reserved (must be 0)

Block Type 的 2-bit 枚举：
- 00 DELTA
- 01 RLE
- 10 PASSTHROUGH
- 11 RAW

---

3. Payload 格式

3.1 DELTA

DELTA 块表示对采样流做残差（delta）变换。根据 shuffle_flag，有两种子路径：

- Path A (shuffle_flag = 0): 纯 Delta。Delta 按 x_bit 宽度以小端序输出，stride 以采样数为单位。

- Path B (shuffle_flag = 1): 先 Byte Shuffle 再做 8-bit、stride=1 的 Delta（即按字节做 delta），适用于 float32/float64 或按字节可获利的数据。此路径要求编码器能缓冲整个块（非严格流式）。

解码顺序为逆变换：后端解压 -> inverse pre-filter -> 输出。

3.2 RLE

RLE 格式使用（count, value）对：1 字节计数 (1..255) + 1 字节值，依次展开至 uncomp_size。
编码器若发现 RLE 后长度 >= 原始长度，则必须退回为 PASSTHROUGH。

3.3 PASSTHROUGH / RAW

PASSTHROUGH：payload 即原始字节，comp_size = uncomp_size 或在经过 pre-filter 后为相同长度（取决于实现）；
RAW：语义上与 PASSTHROUGH 等价，作为逃生类型保留。

---

4. 编码器行为（必遵守）

4.1 Stride 单位

Stride 的单位为采样数（samples），字节步长 = stride × (x_bit / 8)。示例：x_bit=16, stride=2 → 字节步长 = 4。

4.2 探针（Probe）

编码器必须实现一个探针函数，用以为每个块选择最佳编码路径。一个合规的简要流程：

1) 计算原始字节的熵 raw_h 与 RLE 比例。
2) 若 RLE 明显优（RLE 长度远小于原始），选择 RLE。
3) 对候选 stride 集合（默认 {1,2,3,4,6,8}；若存在 hint_stride 则一并考虑）逐一计算 delta 后的熵，记录最佳。
4) 若 x_bit 为 float 类型或 Data Hint 建议，测试 Byte Shuffle + delta（stride=1，8-bit delta）并比较熵。
5) 若 best 为 DELTA，但 (raw_h - best_h) ≤ delta_threshold（实现默认 0.05），则降级为 PASSTHROUGH。

实现注意：不得使用原始熵大于某阈值直接判定为 PASSTHROUGH；必须以比较后的收益决定是否使用 DELTA。

4.3 Shuffle 限制

若选择 Byte Shuffle（路径 B），编码器必须保证能缓冲整个块并在 header 中标注 shuffle_flag=1；解码器会为该块分配临时缓冲以完成逆变换。

4.4 Block Size

推荐默认 64 KB；对低延迟场景可用 16–32 KB，对大型时序数据可增至 128–256 KB。实现应在打开流时对传入 block_size 做范围校验并 clamp 到允许范围。

---

5. 解压器（流式/一遍通过）

解压器总体流程：读取 global header -> 根据 offset_table 直接 seek 至第 0 块的起始偏移 -> 逐块按 offset_table 定位并解析 frame/blk header、读取 payload -> 调用后端解压 -> 逆 pre-filter -> 输出块数据 -> 校验 XXH3-64 哈希。

解码器必须支持：
- 在不加载完整文件的前提下逐块解码（流式），并在遇到 shuffle_flag=1 的块时临时分配缓冲完成逆变换。
- 对 hash 校验失败要返回错误并中止。

注意：实现中应初始化解码上下文中与编码相关的选项结构（例如 block_size）以避免未定义行为；当解码上下文被传给后端 bound 函数时不可使用未初始化值。

---

6. 后端（compression backend）接口

文件 frame 在每块之前包含 1 字节 backend_id 与 4 字节后端压缩后大小（LE）。应用（或 CLI）需注入具体的压缩/解压函数指针（如 zlib-ng、zstd、lz4）；库提供了默认的 passthrough（不压缩）后端。

在流式解码中，解码器会把当前块的 backend_id 暴露到 stream ctx 中，以便应用层的 bk_decompress 回调能据此选择解码实现（例如混合后端场景）。

---

7. 兼容性与实现说明

- 本规范与实现（`libs/quadr-core`）一致：Block Header 为 12 字节，global header 包含 hash_table 与 offset_table。
- 解压实现为 one-pass，但遇到 shuffle_flag=1 的块会进行块级缓冲。
- 解码上下文必须初始化其 encode-options 结构以避免在调用 `quadr_stream_set_backend()` 时读取未初始化的 `block_size`。
- CLI 示例（在项目构建输出中运行）：

```powershell
# 默认压缩解压
cmake-build-release\app\quadr.exe encode input.jpg output.qdr
cmake-build-release\app\quadr.exe decode output.qdr out.jpg

# 指定 block size（KB）
cmake-build-release\app\quadr.exe encode --block=32 input.jpg out_32kb.qdr
```

---

文档版本：v1.5.1（2026-04-01）


---

## 1. 宏观架构：独立分块与单次遍历流式设计

Quadr 文件由一个**全局文件头**和若干**独立数据块（Block）**组成。
解压器采用单次遍历（One-Pass）流式输出，内存占用恒定。

---

## 2. 全局文件头（Global Header）

| 字段名称 | 位宽 (bits) | 说明 |
|---|---|---|
| Magic Number | 32 | 固定标识符 `0x51554452`（ASCII "QUDR"） |
| Spec Version | 8 | 规范版本号，当前为 `0x15`（v1.5） |
| Total Uncompressed Size | 64 | 原始数据总字节数，用于解压终止校验 |
| Block Count | 32 | 文件包含的 Block 总数 |
| Data Hint | 8 | 调用方数据类型提示（见 §2.1） |
| Block Hash Table | 64 x Count | 每个 Block 的 XXH3-64bit 哈希值 |
| Block Offset Table | 64 x Count | 每个 Block 的绝对字节偏移量，供多核并发定位 |

### 2.1 Data Hint 字段定义

| 值 | 含义 | 编码器默认探针行为 |
|---|---|---|
| `0x00` | 未知 / 通用 | 全自动探针决策，枚举全部候选 Stride |
| `0x01` | 图像（RGBA / 灰度） | 优先验证 Stride=4（RGBA）或 Stride=1（灰度） |
| `0x02` | 音频（PCM 整数） | 优先验证 Stride=声道数；不尝试 Byte Shuffle |
| `0x03` | 科学数据 / 传感器时序 | 优先验证 Stride 属于 {1,2,3,4}；对 float 类型尝试 Byte Shuffle |
| `0x04` | 科学浮点（float32/64） | 强制尝试 Byte Shuffle + Delta 变换链 |
| `0xFF` | 强制通用模式 | 忽略 Hint，完全依赖探针 |

Data Hint 是**可选优化字段**，减少探针计算量，不影响解压正确性，解压器忽略该字段。

### 2.2 推荐 Block Size

| 数据类型 | 推荐 Block Size | 说明 |
|---|---|---|
| 通用 | 64 KB（默认） | 探针统计量稳定，Header 开销可忽略 |
| 传感器 / 时序 | 128 KB – 256 KB | 小块下探针误判率显著上升（4KB vs 64KB 差约 10pp） |
| 流式 / 实时 | 16 KB – 32 KB | 优先降低延迟，接受轻微压缩率损失 |

---

## 3. 数据块结构（Block Layout）

```
+-------------------------+
| Block Header            |
+-------------------------+
| Block Payload           |   (内容格式由 Block Type 决定)
+-------------------------+
```

### 3.1 块头部（Block Header）

| 字段名称 | 位宽 (bits) | 说明 |
|---|---|---|
| Block Uncompressed Size | 32 | 当前块原始数据字节数 |
| Block Compressed Size | 32 | 当前块 Payload 的物理字节数 |
| Block Type | 2 | 块类型枚举（见下表） |
| Shuffle Flag | 1 | 1 = Payload 为 Byte Shuffle 后再 Delta；仅 Block Type=DELTA 时有效 |
| x-bit Format | 8 | 原始数据采样位宽（有效值：8 / 16 / 32 / 64） |
| Delta Stride | 8 | 通道步长，单位采样数（见 §5.1）；Block Type != DELTA 时忽略 |
| Word Size | 4 | Byte Shuffle 的字组宽度（字节数）；Shuffle Flag=0 时忽略 |
| Reserved | 1 | 保留，必须置 0 |

**Block Type 枚举：**

| 值 | 枚举名 | Payload 格式 |
|---|---|---|
| `00` | `DELTA` | Delta 变换后的字节流（见 §4.1），可选 Shuffle Flag |
| `01` | `RLE` | RLE 编码字节流（见 §4.2） |
| `10` | `PASSTHROUGH` | 原始字节，不做任何变换 |
| `11` | `RAW` | 原始字节，完全未压缩（逃生舱） |

---

## 4. Block Payload 格式

### 4.1 DELTA 块

DELTA 块 Payload 有两种变换路径，由 **Shuffle Flag** 决定。

#### 路径 A：纯 Delta（Shuffle Flag = 0）

```
初始化 Prev[0..Stride-1] = 0

for i = 0..N-1:
    Delta[i] = (Sample[i] - Prev[i % Stride]) mod 2^x
    Prev[i % Stride] = Sample[i]
    输出 Delta[i]（x-bit 小端序）
```

#### 路径 B：Byte Shuffle + Delta（Shuffle Flag = 1）

```
Step 1  Byte Shuffle：
        输入 N 个采样，每个采样 W = Word Size 字节宽
        输出 = [所有采样的 byte_0] ++ [所有采样的 byte_1] ++ ... ++ [所有采样的 byte_{W-1}]
        即：将 N x W 的字节矩阵做转置，变为 W x N

Step 2  对 Shuffle 后的字节流做 stride=1 的 8-bit Delta：
        Delta[0] = Shuffled[0]
        Delta[i] = (Shuffled[i] - Shuffled[i-1]) mod 256   (i >= 1)
        输出 Delta[i]（1 字节/值）
```

**解压器逆变换（路径 B）：**

```
Step 1  逆 Delta（stride=1，8-bit 域）还原 Shuffled 流
Step 2  逆 Byte Shuffle：将 W x N 矩阵转置回 N x W，还原原始采样流
```

注意：路径 B 需要缓冲整个 Block，不满足逐字节流式输出要求。
编码器**不得**对无法整体缓冲的 Block 设置 Shuffle Flag=1。

> **选择依据（实验数据）：**
> - float32：Shuffle+Delta vs 纯 Delta 多节省 **8.21pp**；vs 裸 Huffman 多节省 **20.66pp**
> - float64：Shuffle+Delta vs 纯 Delta 多节省 **17.97pp**
> - PCM int16：Shuffle 无益，探针自动选择 Shuffle Flag=0

### 4.2 RLE 块

Payload 由若干 RLE 段组成，每段格式：

```
[1 byte: 运行长度 N, 1–255] [1 byte: 重复字节值 V]
```

解压器重复输出 N 个字节 V，直至还原 Block Uncompressed Size 字节。

若 RLE 后体积 >= 原始体积，编码器**必须**改用 PASSTHROUGH。

### 4.3 PASSTHROUGH 块

Payload 即原始字节，解压器原样输出。

### 4.4 RAW 块

Payload 即原始字节，Block Compressed Size = Block Uncompressed Size。

---

## 5. 编码器规范（Encoder Specification）

### 5.1 Stride 的单位约定

Stride 的单位为**采样数（samples）**，而非字节数。

字节步长 = `Stride × (x-bit / 8)`

示例：x-bit=16，Stride=2 → 字节步长=4（立体声 PCM 每声道独立 Delta）。

### 5.2 Stride 自动探测（强制）

选择 DELTA 类型时，**必须**枚举以下候选集合，选使变换后字节流熵值最低的值：

```
默认候选集合：{1, 2, 3, 4, 6, 8}
若 Data Hint 提供 hint_stride，加入候选集合（不替代默认集合）
```

### 5.3 编码器探针算法（强制）

```
function choose_block_encoding(block, x_bit, hint_stride):

    // 第一步：RLE 检测
    if rle_ratio(block) < 0.5:
        return (RLE, stride=0, shuffle=0)

    // 第二步：Delta 候选评估
    raw_h  = entropy(block)
    best   = { type=PASSTHROUGH, stride=0, shuffle=0, score=raw_h }

    candidates = {1, 2, 3, 4, 6, 8}
    if hint_stride > 0: candidates.add(hint_stride)

    for s in candidates:
        h = entropy(delta(block, stride=s))
        if h < best.score:
            best = { type=DELTA, stride=s, shuffle=0, score=h }

    // 第三步：Byte Shuffle + Delta（仅 float 类型或显式 Hint）
    if x_bit in {32, 64} or data_hint in {0x03, 0x04}:
        word_size = x_bit / 8
        sh_d = delta(byte_shuffle(block, word_size), stride=1)
        h    = entropy(sh_d)
        if h < best.score:
            best = { type=DELTA, stride=1, shuffle=1, score=h }

    // 第四步：收益门槛
    if best.type == DELTA and (raw_h - best.score) <= 0.05:
        return (PASSTHROUGH, 0, 0)

    return best

// 严禁：不得以 raw_h > 任意固定阈值 作为跳转 PASSTHROUGH 的条件。
// 原因：传感器/灰度图原始熵接近 8.0，但 Delta 后熵可低至 2.57。
```

### 5.4 16-bit / 32-bit Delta 语义

| x-bit | Delta 运算域 | 输出格式 | 字节步长（Stride=S） |
|---|---|---|---|
| 8 | mod 256 | 1 字节/采样 | S 字节 |
| 16 | mod 65536 | 小端序 2 字节/采样 | 2S 字节 |
| 32 | mod 2^32 | 小端序 4 字节/采样 | 4S 字节 |
| 64 | mod 2^64 | 小端序 8 字节/采样 | 8S 字节 |

float32/float64：按 IEEE 754 解释为 uint32/uint64 后做模整数 Delta。
建议优先走 Byte Shuffle + Delta 路径（见 §5.3 第三步）。

---

## 6. 解压器极限单次遍历工作流（One-Pass Workflow）

```
读取 Global Header → Block Count, Hash Table, Offset Table

for Block i in [0, Block Count):
    读取 Block Header → Type, ShuffleFlag, x_bit, Stride, WordSize,
                        UncompSize, CompSize
    读取 Block Payload（CompSize 字节）

    switch Type:
        DELTA, ShuffleFlag=0: 逐采样逆 Delta（流式）-> 输出
        DELTA, ShuffleFlag=1: 缓冲 -> 逆 Delta -> 逆 Shuffle -> 输出
        RLE:         解 RLE -> 输出
        PASSTHROUGH: 直接输出
        RAW:         直接输出

    XXH3-64 校验输出字节，对比 Hash Table[i]
    不匹配 -> 报错终止

断言 sum(UncompSize) == Total Uncompressed Size
```

### 6.1 DELTA 路径 A 状态机（C 伪代码）

```c
uint8_t prev[MAX_STRIDE] = {0};
uint32_t idx = 0;

while (samples_remaining--) {
    uint64_t d = read_sample(x_bit);            // 读 x-bit 小端序
    uint64_t s = (d + prev[idx % stride]) & mask; // mask = (1<<x_bit)-1
    prev[idx % stride] = (uint8_t)(s & 0xFF);   // 对 8-bit；16/32 需调整
    write_sample(s, x_bit);
    idx++;
}
```

---

## 7. 版本兼容性

| 字段 | v1.4 | v1.5 |
|---|---|---|
| Spec Version | `0x14` | `0x15` |
| Block Type 字段 | Raw Fallback Flag（1-bit）| Block Type 枚举（2-bit）|
| Shuffle Flag | 无 | 新增（1-bit，Block Header）|
| MWTA 控制流区 | 存在（强制）| 移入附录 A（可选独立模式）|
| 基准冻结 | 解压器规则 | 删除（编码器侧策略）|
| 编码器探针 | 未规范 | §5.3 强制规范 |
| Stride 自动探测 | 未规范 | §5.2 强制规范 |
| Data Hint | 无 | Global Header 新增字段 |
| Byte Shuffle | 无 | §4.1 路径 B + §5.3 第三步 |

v1.5 解压器**不向后兼容** v1.4 格式（Block Header 位域定义变化）。

---

## 附录 A：基准测试参考数据（v1.5 设计依据）

### A.1 主测试：综合压缩率（越低越好）

| 数据集 | 裸 zlib | 裸 zstd | Q+zlib | Q+zstd | 新架构 | 最优 |
|---|---|---|---|---|---|---|
| 灰度图（平滑渐变）| 0.64% | 0.21% | 0.12% | **0.02%** | 0.12% | Q+zstd |
| RGBA（带噪点）| **97.55%** | 100.01% | 97.55% | 100.01% | 97.56% | Q+zlib |
| PCM 立体声 16bit | 98.07% | 97.93% | **86.87%** | 86.46% | 86.87% | Q+zstd |
| 传感器时序 | 55.38% | 56.51% | **49.18%** | 51.86% | 51.29% | Q+zlib |
| 科学浮点 float32 | 91.57% | 91.41% | 83.36% | **82.42%** | 83.37% | Q+zstd |
| 文本（重复 HTML）| 0.36% | **0.07%** | 0.36% | 0.07% | 0.43% | Q+zstd |
| 可执行文件 | 100.04% | 100.01% | 100.04% | **100.01%** | 100.04% | Q+zstd |
| 稀疏数组（1% 非零）| 2.98% | 2.55% | 2.98% | 2.55% | **2.32%** | 新架构 |
| 协议填充 | 0.11% | 0.02% | 0.11% | 0.02% | **0.02%** | 新架构 |
| 已压缩数据 | 100.04% | 100.01% | 100.04% | **100.01%** | 100.04% | Q+zstd |
| **综合平均** | 54.67% | 54.87% | **52.06%** | 52.34% | 52.21% | **Q+zlib** |

### A.2 Byte Shuffle 变换链对比（Huffman 近似）

| 数据 | 纯 Huffman | Delta（最优）| Shuffle | Shuffle+Delta |
|---|---|---|---|---|
| float32 | 91.57% | 83.36% | 75.26% | **70.90%** (+20.66pp)|
| float64 | 95.21% | 86.38% | 75.55% | **68.38%** (+26.83pp)|
| PCM int16 | 98.07% | **86.85%** | 91.13% | 91.62% |
| 传感器 4ch int16 | 44.77% | 32.42% | 42.67% | **29.59%** (+15.18pp)|

### A.3 Block Size 敏感性（新架构，数值型数据）

| 数据集 | 4KB | 16KB | 64KB | 128KB | 256KB |
|---|---|---|---|---|---|
| 灰度图 | 0.51% | 0.20% | **0.12%** | 0.11% | 0.11% |
| PCM 立体声 | 88.15% | **86.88%** | 86.87% | 86.88% | 86.88% |
| 传感器时序 | 58.84% | 55.11% | 51.29% | 49.88% | **49.18%** |
| 科学浮点 | 84.50% | 83.30% | **83.37%** | 83.36% | 83.37% |

---

## 附录 B：MWTA 独立模式（可选，Standalone Mode）

> 仅适用于 Quadr 不接后级 LZ77 和熵编码的嵌入式/低资源场景。
> 标准预处理器部署形态应使用主规范，不使用本附录。

独立模式通过 `Data Hint & 0x80 == 0x80` 激活。激活后 DELTA 块 Payload 改用
MWTA 分仓 + 控制流格式（参见 v1.4 规范 §3.2–§3.3）。基准冻结规则在独立模式下
同样删除，A 象限无条件更新 Prev。

---

*文档版本：v1.5.1 | 基准测试日期：2026-03-28*
