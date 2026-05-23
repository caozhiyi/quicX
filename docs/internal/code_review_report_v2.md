# quicX 第二轮代码审查报告

> **审查日期**: 2026-03-19（第二轮）
> **审查范围**: 全库 src/ + test/ + example/
> **审查方法**: 多子代理并行深度审查（Common 层、QUIC 层、HTTP/3 层）
> **前提**: 第一轮报告中的 P0（6 个）、P1（12 个）、P2（15 个）问题已全部修复或处理

---

## 一、总评

### 综合评分：82/100（较第一轮 +3）

| 维度 | 评分 | 变化 | 要点 |
|---|---|---|---|
| **架构设计** | 85/100 | → | 分层清晰、接口/实现分离好 |
| **RFC 合规性** | 78/100 | ↓4 | 发现传输参数处理多个 RFC 违规 |
| **代码安全性** | 78/100 | ↑8 | P0 已修，但 Common 层仍有缓冲区溢出 |
| **测试体系** | 76/100 | ↓2 | 发现更多测试质量问题 |
| **代码质量** | 82/100 | ↑6 | 拼写/注释已统一，但存在新的逻辑问题 |
| **线程安全** | 77/100 | ↑2 | inet_ntoa 已修，但 HTTP/3 层有生命周期问题 |

---

## 二、P0 级致命问题（3 个）

### P0-1. `PoolAlloter::Free` 当 `len=0` 时数组越界访问

**文件**: `src/common/alloter/pool_alloter.cpp:56-73`

```cpp
void PoolAlloter::Free(void* &data, uint32_t len) {
    if (!data) { return; }
    if (len > kDefaultMaxBytes) { ... }
    MemNode** my_free = &(free_list_[FreeListIndex(len)]);  // len=0 → 越界
}
```

**问题**: `FreeListIndex(0)` = `(0 + kAlign - 1) / kAlign - 1` = `7/8 - 1` = `0 - 1` = **UINT32_MAX**（无符号下溢），访问 `free_list_[4294967295]`。

**触发条件**: `IAlloter::Free` 接口声明 `len` 默认值为 0（`virtual void Free(void* &data, uint32_t len = 0)`），调用者很容易省略 `len`。

**修复**:
```cpp
if (len == 0 || len > kDefaultMaxBytes) {
    alloter_->Free(data);
    data = nullptr;
    return;
}
```

---

### P0-2. `FixedEncodeUint16` 缓冲区溢出（只检查 1 字节但写入 2 字节）

**文件**: `src/common/decode/decode.cpp:162-167`

```cpp
uint8_t* FixedEncodeUint16(uint8_t *start, uint8_t *end, uint16_t value) {
    if (start == nullptr || end == nullptr || start >= end) {  // 只保证 ≥1 字节
        return nullptr;
    }
    *(uint16_t*)start = htons(value);  // 写入 2 字节 → 越界
    return start + sizeof(uint16_t);
}
```

**问题**: 空间检查 `start >= end` 只保证至少 1 字节空间，但实际写 2 字节。对比 `FixedEncodeUint32/64` 正确使用了 `start + sizeof(T) > end` 检查。

**修复**:
```cpp
if (start == nullptr || end == nullptr || start + sizeof(uint16_t) > end) {
    return nullptr;
}
```

---

### P0-3. `TransportParam::EncodeSize()` 返回 `sizeof(TransportParam)` 与实际编码大小无关

**文件**: `src/quic/connection/transport_param.cpp:283-285`

```cpp
uint32_t TransportParam::EncodeSize() {
    return sizeof(TransportParam);  // C++ 对象内存布局大小，≠ 序列化大小
}
```

**问题**: `sizeof(TransportParam)` 包含 vtable 指针、`std::string`/`std::vector` 内部结构等，与 QUIC wire format 编码大小完全无关。当字符串很长时可能允许缓冲区溢出；当短参数时又会不必要地拒绝足够的缓冲区。

**修复**: 计算实际 varint 编码大小，遍历所有参数累加 type + length + value 的 varint 编码字节数。

---

## 三、P1 级严重问题（18 个）

### P1-1. `TransportParam::Merge()` 对所有参数取 `min`，初始值为 0 导致限制全为 0

**文件**: `src/quic/connection/transport_param.cpp:53-76`

```cpp
bool TransportParam::Merge(const TransportParam& tp) {
    initial_max_data_ = std::min(tp.initial_max_data_, initial_max_data_);
    // 如果 initial_max_data_ 初始为 0 → std::min(remote_value, 0) = 0
    // 所有流控限制被清零 → 连接无法传输任何数据
}
```

**问题**: RFC 9000 规定 `initial_max_data` 等参数是对端设置的限制，应直接采用对端值，不是取最小值。初始值为 0 导致 `min` 永远返回 0。

**修复**: 对大多数参数直接赋值 `initial_max_data_ = tp.initial_max_data_`，仅 `max_idle_timeout` 按 RFC 取最小值。

---

### P1-2. `TransportParam::Decode` 不跳过未知传输参数（违反 RFC 9000 §18）

**文件**: `src/quic/connection/transport_param.cpp:274-278`

```cpp
default:
    common::LOG_ERROR("unsupport transport param. type:%d", type);
    return false;  // RFC 9000: "MUST ignore transport parameters that it does not support"
```

**问题**: 收到未知参数 ID 时返回 false 导致整个解码失败。与使用 GREASE 或扩展参数（如 QUIC v2 `version_information`）的对端无法完成握手。

**修复**: 解码并跳过未知参数的 length + value 字段：
```cpp
default: {
    uint64_t param_len = 0;
    pos = common::DecodeVarint(pos, end, param_len);
    if (pos == nullptr || pos + param_len > end) return false;
    pos += param_len;  // skip unknown param
    break;
}
```

---

### P1-3. `TransportParam::EncodeBool` 编码 `disable_active_migration` 不符合 RFC

**文件**: `src/quic/connection/transport_param.cpp:301-306`

```cpp
uint8_t* TransportParam::EncodeBool(uint8_t* start, uint8_t* end, bool value, uint32_t type) {
    start = common::EncodeVarint(start, end, type);
    start = common::EncodeVarint(start, end, 1);    // length = 1
    start = common::EncodeVarint(start, end, value ? 1 : 0);  // value = 0/1
    return start;
}
```

**问题**: RFC 9000 §18 规定 `disable_active_migration` 是**零长度参数**——存在即表示 true。正确编码为 `type + length(0)`，无 value 字段。当前编码会导致对端解码失败。

**修复**:
```cpp
start = common::EncodeVarint(start, end, type);
start = common::EncodeVarint(start, end, 0);  // zero-length
return start;
```

---

### P1-4. 服务端接收 HANDSHAKE_DONE 帧未拒绝（违反 RFC 9000 §19.20）

**文件**: `src/quic/connection/connection_server.cpp:30-31, 72-83`

```cpp
// 构造函数中注册了处理回调
frame_processor_->SetHandshakeDoneCallback(
    std::bind(&ServerConnection::HandleHandshakeDoneFrame, this, ...));

// 处理函数正常接受
bool ServerConnection::HandleHandshakeDoneFrame(std::shared_ptr<IFrame> frame) {
    state_machine_.OnHandshakeDone();  // 错误地接受了
    return true;
}
```

**问题**: RFC 9000 §19.20 明确规定："A server MUST treat receipt of a HANDSHAKE_DONE frame as a connection error of type PROTOCOL_VIOLATION."

**修复**: 服务端收到 HANDSHAKE_DONE 应关闭连接并报告 `PROTOCOL_VIOLATION`。

---

### P1-5. `NEW_CONNECTION_ID` 缺少 `retire_prior_to` 验证 + DoS 风险

**文件**: `src/quic/connection/connection_frame_processor.cpp:359-377`

```cpp
uint64_t retire_prior_to = new_cid_frame->GetRetirePriorTo();
if (retire_prior_to > 0) {
    for (uint64_t seq = 0; seq < retire_prior_to; ++seq) {
        // 创建 retire_prior_to 个 RetireConnectionIDFrame 对象
    }
}
```

**问题**:
1. RFC 9000 §19.15: Retire Prior To **必须** ≤ Sequence Number，缺少此验证
2. **DoS**: 恶意对端发送 `retire_prior_to = 2^62`，for 循环创建海量帧对象导致 OOM
3. 每次收到 NEW_CONNECTION_ID 都从 seq=0 开始，重复退役已退役的 CID

**修复**: 验证 `retire_prior_to <= sequence_number`，限制循环范围，记录已退役的最大序号。

---

### P1-6. `frame_type_bit_ |= (1 << frame->GetType())` 有符号移位溢出 UB

**文件**: `src/quic/packet/init_packet.cpp:230`, `rtt_1_packet.cpp:172`, `handshake_packet.cpp:194`, `rtt_0_packet.cpp:204`

```cpp
for (const auto& frame : frames_list_) {
    frame_type_bit_ |= (1 << frame->GetType());  // 1 是 signed int
}
```

**问题**: 字面量 `1` 是 `int` 类型（有符号 32 位）。当 `GetType()` 返回 30（`kHandshakeDone`）或 31 时，`1 << 30` / `1 << 31` 导致**有符号整数溢出 = UB**。`type.h` 中的 `FrameTypeBit` 枚举也存在同样问题。

**修复**: 统一使用 `1u <<` 或 `uint32_t(1) <<`。

---

### P1-7. `DecodeBytesCopy` / `DecodeBytesNoCopy` 失败时返回 `start` 而非 `nullptr`

**文件**: `src/common/decode/decode.cpp:225-243`

```cpp
uint8_t* DecodeBytesCopy(uint8_t *start, uint8_t *end, uint8_t*& out, uint32_t out_len) {
    if (start == nullptr || end == nullptr || end - start < out_len) {
        LOG_ERROR("too small to decode bytes");
        return start;  // 其他 decode 函数返回 nullptr
    }
}
```

**问题**: 所有其他 decode 函数（`DecodeVarint`, `FixedDecodeUint*`）失败时返回 `nullptr`，调用者用 `if (!new_pos)` 检查。但 `DecodeBytesCopy` / `DecodeBytesNoCopy` / `EncodeBytes` 失败时返回 `start`（非空），导致**错误不会被检测到**。

**修复**: 统一返回 `nullptr`。

---

### P1-8. `FixedEncode/DecodeUint16/32/64` 未对齐内存访问 (Strict Aliasing UB)

**文件**: `src/common/decode/decode.cpp` 多处

```cpp
*(uint16_t*)start = htons(value);   // start 是 uint8_t*，无对齐保证
uint32_t raw = *(uint32_t*)start;    // ARM 上可能 SIGBUS
```

**问题**: 将 `uint8_t*` 强转为 `uint16_t*/uint32_t*/uint64_t*` 并解引用，违反 strict aliasing 规则和对齐要求，是**未定义行为**。x86 上通常工作，ARM/RISC-V 上可能崩溃。

**修复**: 使用 `memcpy` 进行 type punning：
```cpp
uint32_t network_value = htonl(value);
memcpy(start, &network_value, sizeof(uint32_t));
```

---

### P1-9. `BlockMemoryPool::Expansion` 不检查 `malloc` 返回值

**文件**: `src/common/alloter/pool_block.cpp:109-122`

```cpp
void* mem = malloc(large_size_);
free_mem_vec_.push_back(mem);  // nullptr 入列 → 后续分配返回 nullptr
```

**修复**: 检查 malloc 返回值，失败时 break 或抛异常。

---

### P1-10. `SendStream::TrySendData` 无符号下溢

**文件**: `src/quic/stream/send_stream.cpp:185, 237`

```cpp
if (peer_data_limit_ - send_data_offset_ < kStreamDataBlockedThreshold) {
    // 当 send_data_offset_ > peer_data_limit_ 时 → uint64_t 下溢 → ~0
```

以及：
```cpp
uint32_t stream_send_size = peer_data_limit_ - send_data_offset_;
// uint64_t → uint32_t 截断
```

**修复**: 先判断 `send_data_offset_ > peer_data_limit_`，使用 `uint64_t` 中间变量并用 `std::min` 限制。

---

### P1-11. `BaseLogger::GetStreamParam` lambda 捕获裸 `this` 指针

**文件**: `src/common/log/base_logger.cpp:147-183`

```cpp
cb = [this](std::shared_ptr<Log> l) { logger_->Fatal(l); };
```

**问题**: lambda 捕获 `this`（`BaseLogger*`），在静态销毁阶段或跨作用域使用时可能 use-after-free。

**修复**: 捕获 `logger_` 的 shared_ptr 而非 `this`。

---

### P1-12. HTTP/3 `ServerConnection::HandleTimer` 定时器回调捕获裸 `this`

**文件**: `src/http3/connection/connection_server.cpp:177, 422`

```cpp
quic_server_->AddTimer(kServerPushWaitTimeMs,
    std::bind(&ServerConnection::HandleTimer, this));
```

**问题**: 异步定时器捕获裸 `this`，连接关闭后定时器触发 → use-after-free。

**修复**: 使用 `weak_from_this()` 模式。

---

### P1-13. HTTP/3 `ClientConnection` 中 shared_ptr 伪装为 weak_ptr 导致循环引用

**文件**: `src/http3/connection/connection_client.cpp:73-78`

```cpp
auto weak_enc = encoder_sender;  // 实际是 shared_ptr 拷贝！
qpack_encoder_->SetInstructionSender([weak_enc](...) {
    if (!weak_enc) { ... }  // 永远不为 null
});
```

**问题**: 变量名为 `weak_enc` 但实际是 `shared_ptr` 拷贝，导致循环引用和空检查永远不触发。`ServerConnection` 同样存在。

**修复**: 使用 `std::weak_ptr<...> weak_enc = encoder_sender;`。

---

### P1-14. QPACK `DynamicTable::DuplicateEntry` 引用失效（悬空引用）

**文件**: `src/http3/qpack/dynamic_table.cpp:104-117`

```cpp
const HeaderItem& original = headeritem_deque_[absolute_index];
return AddHeaderItem(original.name_, original.value_);
// AddHeaderItem → EvictEntries → pop_back → original 可能悬空
```

**修复**: 在 `AddHeaderItem` 前拷贝 name 和 value：
```cpp
std::string name = headeritem_deque_[absolute_index].name_;
std::string value = headeritem_deque_[absolute_index].value_;
return AddHeaderItem(name, value);
```

---

### P1-15. HTTP/3 `IConnection::HandleSettings` 用 `std::min` 合并设置值

**文件**: `src/http3/connection/if_connection.cpp:102`

```cpp
settings_[iter->first] = std::min(settings_[iter->first], iter->second);
// settings_ 无该 key → 默认 0 → min(0, remote_value) = 0
```

**问题**: RFC 9114 §7.2.4 规定 SETTINGS 值应直接存储。`std::min` 加上默认值 0 导致所有对端设置被清零。

**修复**: `settings_[iter->first] = iter->second;`

---

### P1-16. `std::stoul` 对恶意 `content-length` 未做异常处理

**文件**: `src/http3/stream/request_stream.cpp:93`, `response_stream.cpp:140`, `push_receiver_stream.cpp:132`

```cpp
body_length_ = std::stoul(headers_["content-length"]);
// "abc" → std::invalid_argument → 进程崩溃
```

**修复**: try-catch 包裹或使用安全解析，失败时发送 `H3_MESSAGE_ERROR`。

---

### P1-17. `ControlReceiverStream::HandleFrame` 将合法帧类型误报为错误

**文件**: `src/http3/stream/control_receiver_stream.cpp:81-103`

```cpp
switch (frame->GetType()) {
    case FrameType::kGoAway: { ... }
    case FrameType::kSettings: { ... }
    default:
        error_handler_(stream_->GetStreamID(), Http3ErrorCode::kFrameUnexpected);
        // CANCEL_PUSH 等合法帧 + 未知扩展帧都会导致连接关闭
}
```

**问题**: RFC 9114 规定未知帧类型应被忽略，且 `CANCEL_PUSH` 在控制流上是合法的。

**修复**: 未知帧类型用 LOG_WARN 并忽略，添加 CANCEL_PUSH 处理。

---

### P1-18. QPACK 编码器在编码期间插入动态表但从不引用

**文件**: `src/http3/qpack/qpack_encoder.cpp:23-87`

```cpp
// Header prefix 的 Required Insert Count 在编码前就写入
// 编码过程中向动态表添加条目（行 84）
dynamic_table_.AddHeaderItem(name, value);
instruction_sender_({{name, value}});
// 但紧接着 fallthrough 到 LiteralNoNameRef 编码（不引用新条目）
```

**问题**: 每次编码都无谓地膨胀动态表，但实际编码从不引用新插入的条目。且 RIC 在编码开始前就已写入，与后续插入不一致。

**修复**: 要么使用 indexed dynamic 引用，要么不在 Encode 路径中添加条目。

---

## 四、P2 级中等问题（15 个）

| # | 问题 | 位置 | 修复建议 |
|---|---|---|---|
| 1 | `EventLoop::RegisterFd` 先存 handler 再验证，失败时残留 | `event_loop.cpp:125` | 验证前置，AddFd 失败时清理 |
| 2 | `AsyncWriter::WriterLoop` 退出后队列中的任务被丢弃 | `async_writer.cpp:81` | 退出循环后 drain 队列 |
| 3 | `BufferDecodeWrapper::DecodeBytes` 慢路径 `new[]` 所有权不明确 | `buffer_decode_wrapper.cpp:127` | 返回 RAII 对象或文档明确所有权 |
| 4 | `MultiBlockBuffer::Write(IBuffer)` 不移动源 buffer 读指针 | `multi_block_buffer.cpp:364` | 添加 `buffer->MoveReadPt(written)` |
| 5 | `Address::operator==` 的 `port_ != 0` 条件违反自反性 | `address.cpp:61` | 移除 `port_ != 0` 条件 |
| 6 | `SingleBlockBuffer::MoveReadPt` 中 `int32_t` 窄化比较 | `single_block_buffer.cpp:67` | 直接比较 `size_t` 和 `uint32_t` |
| 7 | `FormatLog` 中 `vsnprintf` 返回值未做截断检查 | `base_logger.cpp:18` | `curlen += std::min(ret, len - curlen)` |
| 8 | `BufferSpan::GetLength()` 在无效状态下未检查 `Valid()` | `buffer_span.cpp:34` | 添加 `Valid()` 检查 |
| 9 | `KqueueEventDriver` 错误路径将 wakeup_fd 设为 0 而非 -1 | `kqueue_event_driver.cpp:57` | 统一使用 -1 表示无效 fd |
| 10 | `SysCallResult::errno_` 与 POSIX 宏 `errno` 命名冲突 | `os_return.h:11` | 改名为 `error_code_` |
| 11 | `RecvStream::OnStreamFrame` offset+length 可能整数溢出 | `recv_stream.cpp:116` | 添加溢出检查 |
| 12 | BBR v3 round 边界处 ACK 数据归属不正确 | `bbr_v3_congestion_control.cpp:84` | 先累加再检查 round 边界 |
| 13 | CUBIC `OnPacketLost` 缺少重复降窗防护 | `cubic_congestion_control.cpp:118` | 添加 `in_recovery_` 检查 |
| 14 | `ServerConnection::CanPush()` off-by-one（MAX_PUSH_ID=0 时不能 push） | `http3/connection_server.cpp:447` | `>=` 改为 `>`，区分未收到/收到 0 |
| 15 | `PushReceiverStream` frame decode 失败被误当做 need-more-data | `push_receiver_stream.cpp:77` | `!decode_ok` 时无条件报错 |

---

## 五、测试质量问题

| # | 文件 | 问题 |
|---|---|---|
| T1 | `http_connection_test.cpp:42` | `MockClient::error_code_` 未初始化，`ErrorHandler` 为空实现 |
| T2 | `frame_decoder_test.cpp:125-134` | `DecodeInvalidFrameType` 测试意图与实际行为不匹配 |
| T3 | `request_response_stream_test.cpp:109` | `MockServerConnection::error_code_` 未初始化 |

---

## 六、修复优先级矩阵

### Phase 0: 紧急修复（P0，半天）

| 任务 | 预估 | 文件数 |
|---|---|---|
| P0-1: PoolAlloter::Free len=0 | 5 min | 1 |
| P0-2: FixedEncodeUint16 边界检查 | 5 min | 1 |
| P0-3: TransportParam::EncodeSize | 1 h | 1 |
| **小计** | **~1.2 h** | **3** |

### Phase 1a: RFC 合规性修复（3 天）

| 任务 | 预估 |
|---|---|
| P1-1: TransportParam::Merge 直接赋值 | 1 h |
| P1-2: 未知传输参数跳过 | 30 min |
| P1-3: EncodeBool 零长度 | 15 min |
| P1-4: 服务端拒绝 HANDSHAKE_DONE | 15 min |
| P1-5: NEW_CONNECTION_ID 验证 | 2 h |
| P1-15: HandleSettings 直接赋值 | 15 min |
| P1-17: ControlReceiver 未知帧忽略 | 1 h |
| **小计** | **~5.5 h** |

### Phase 1b: 内存安全修复（2 天）

| 任务 | 预估 |
|---|---|
| P1-6: 有符号移位 → 1u << | 30 min |
| P1-7: Decode 返回值统一 | 30 min |
| P1-8: memcpy 替代类型双关 | 1 h |
| P1-9: malloc 返回值检查 | 15 min |
| P1-10: 无符号下溢保护 | 30 min |
| P1-14: DuplicateEntry 先拷贝 | 15 min |
| P1-16: stoul 异常处理 | 30 min |
| **小计** | **~3.5 h** |

### Phase 1c: 生命周期修复（1 天）

| 任务 | 预估 |
|---|---|
| P1-11: Logger lambda 捕获 | 30 min |
| P1-12: Timer 回调 weak_ptr | 30 min |
| P1-13: weak_ptr 替代 shared_ptr | 30 min |
| P1-18: QPACK 编码器逻辑 | 2 h |
| **小计** | **~3.5 h** |

### Phase 2: 代码质量（1 周）

| 批次 | 预估 |
|---|---|
| P2-1~P2-10: Common 层修复 | 4 h |
| P2-11~P2-15: QUIC/HTTP3 层修复 | 4 h |
| T1~T3: 测试质量修复 | 2 h |
| **小计** | **~10 h** |

---

## 七、与第一轮报告的对比

| 维度 | 第一轮问题数 | 本轮新增 | 状态 |
|---|---|---|---|
| P0 | 6 | 3 | 第一轮已修 ✅ |
| P1 | 12 | 18 | 第一轮已修 ✅ |
| P2 | 15 | 15 | 第一轮已修 ✅ |
| **新增总计** | — | **36** | 待修复 |

### 新增问题分布

| 层 | P0 | P1 | P2 | 合计 |
|---|---|---|---|---|
| Common | 2 | 4 | 8 | 14 |
| QUIC | 1 | 6 | 3 | 10 |
| HTTP/3 | 0 | 6 | 2 | 8 |
| 测试 | 0 | 0 | 3 | 3 |
| **合计** | **3** | **16** | **16** | **35** |

---

## 八、建议新增的测试用例

| 测试 | 覆盖问题 |
|---|---|
| `pool_alloter_free_zero_len_test` | P0-1 |
| `fixed_encode_uint16_boundary_test` | P0-2 |
| `transport_param_encode_size_test` | P0-3 |
| `transport_param_merge_nonzero_test` | P1-1 |
| `transport_param_unknown_param_test` | P1-2 |
| `transport_param_encode_bool_zero_len_test` | P1-3 |
| `server_reject_handshake_done_test` | P1-4 |
| `new_cid_retire_prior_to_validation_test` | P1-5 |
| `decode_bytes_failure_returns_nullptr_test` | P1-7 |
| `unaligned_encode_decode_test` | P1-8 |
| `send_stream_flow_control_underflow_test` | P1-10 |
| `dynamic_table_duplicate_with_eviction_test` | P1-14 |
| `settings_merge_preserves_remote_values_test` | P1-15 |
| `malformed_content_length_test` | P1-16 |
| `control_stream_unknown_frame_type_test` | P1-17 |

---

## 九、长期建议（补充）

1. **`-Wshift-overflow -Wsign-conversion`**: 可发现所有有符号移位和窄化转换问题
2. **Transport Parameters fuzzing**: 添加 `TransportParam::Decode` 的 fuzz target
3. **QUIC 互操作测试**: 与 quiche/ngtcp2 进行 QUIC v2 互操作，验证传输参数兼容性
4. **HTTP/3 互操作测试**: 验证 SETTINGS/PUSH/CANCEL_PUSH 帧的正确处理
5. **Lifetime analysis**: 使用 Clang `-Wdangling` 检测回调中的 `this` 捕获问题
6. **UBSan CI 集成**: 可一次性发现 strict aliasing、移位溢出、整数溢出等 UB

---

> **结论**: 第一轮 P0/P1 问题已全部修复，代码安全性显著提升。但本轮深度审查发现了更多**RFC 合规性问题**（传输参数处理 4 个违规）、**内存安全问题**（Common 层编解码器 3 个）和 **HTTP/3 层生命周期问题**（回调中裸 this 指针 3 个）。其中最紧急的是 P0-1（PoolAlloter 越界访问）和 P0-2（FixedEncodeUint16 缓冲区溢出），建议立即修复。修复全部 P0+P1 后，代码整体评分预计可达 **88+**。
