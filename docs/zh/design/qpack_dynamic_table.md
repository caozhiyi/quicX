# QPACK 动态表

本文档梳理 quicX 中 RFC 9204 *QPACK: Field Compression for HTTP/3* 的实现：一个 HTTP 头部从被 `Encode()` 接受、决定放进动态表、通过 encoder stream 推给对端，到对端 decoder stream 反馈 Section Ack / Insert Count Increment 的全部链路；以及在反方向上，一个收到的 HEADERS 帧如何在动态表插入还没追上时被"挂起"，等到 IIC 抵达后再被重新 decode。读完后你应当能够：

- 知道 quicX 把 QPACK 的"压缩状态"和"传输状态"分别放在哪两个文件里；
- 理解 **Required Insert Count（RIC）** 这个 RFC 概念在仓库中是如何流转的——谁产生、谁验证、谁阻塞、谁释放；
- 在排查"对端报 `QPACK_DECOMPRESSION_FAILED`"或"HEADERS 永远不上交应用层"问题时，能直接定位到代码片段；
- 区分 quicX 里两种 QPACK 索引——deque 位置 vs 绝对索引（absolute index）——分别在解决什么问题。

本文只覆盖**动态表 + 阻塞协调**这条链路。静态表（QPACK Appendix A，99 项）是只读单例，不参与本文讨论；huffman 编/解码是另一条并行链路，见 `huffman_encoder.cpp`。

---

## 1. 总体路径

下面这张图把一个头部从"用户调用 `SendHeaders` / 收到 `HEADERS` 帧"开始的两条对偶链路画在同一张图里，每个节点都标注**实际函数名**，方便和源码对照：

```text
═══════════════════════════════════════ ① 编码侧（发送 HEADERS） ═══════════════════════════════════════

  ReqRespBaseStream::SendHeaders(headers)
        │
        ▼
  QpackEncoder::Encode(headers, buffer)                                【§3 两遍编码】
        │
        ├── Pass 1：对每个字段决策 EncodeAction
        │     ├── StaticTable::FindHeaderItemIndex   → kStaticIndexed / kStaticNameRef
        │     ├── DynamicTable::FindAbsoluteIndex    → kDynamicIndexed / kDynamicPostBaseIndexed
        │     ├── 否则尝试插入 dynamic_table_         ──► instruction_sender_  (encoder stream)
        │     │     │
        │     │     └── 同步触发：DynamicTable::AddHeaderItem
        │     │                                       deque.push_front
        │     │                                       total_insert_count_++
        │     │                                       index_map +1 偏移
        │     └── 全部失败：kLiteralNoNameRef
        │
        ├── 跟踪 max_required_insert_count                              【§4 RIC 计算】
        │
        └── Pass 2：写 Header Block Prefix + 各字段
              ├── WriteHeaderPrefix(RIC, Base)        编码 RIC = (ric % 2·MaxEntries) + 1
              └── 各字段按 EncodeAction 编码到 HEADERS 帧 payload


═══════════════════════════════════════ ② 解码侧（收到 HEADERS） ═══════════════════════════════════════

  ReqRespBaseStream::OnData ──► HandleHeaders(headers_frame)
        │
        ▼
  QpackEncoder::Decode(buffer, headers)              ◄── 同一类既做 encoder 又做 decoder
        │
        ├── ReadHeaderPrefix(RIC, Base)
        │
        ├── if (RIC > dynamic_table_.GetInsertCount()) → 返回 false   【§5 阻塞】
        │     │
        │     ▼
        │   HandleHeaders 检测到 false
        │     ├── is_currently_blocked_ = true
        │     ├── 拍 encoded_fields 快照（CloneReadable, !move_write_pt）
        │     └── blocked_registry_->Add(key, retry)
        │
        └── 解码各字段 → headers，记录 last_decoded_ric_                 【§6 字段解码】
              │
              ▼
        if (last_decoded_ric_ > 0) EmitDecoderFeedback(SectionAck, key)


═══════════════════════════════════════ ③ encoder stream 接收路径 ═══════════════════════════════════════

  对端发来 encoder stream 字节
        │
        ▼
  QpackEncoderReceiverStream::OnData
        │
        ▼
  QpackEncoder::DecodeEncoderInstructions(buffer)                      【§7 encoder 指令】
        │
        ├── Set Dynamic Table Capacity      → DynamicTable::UpdateMaxTableSize
        ├── Insert With Name Reference      → DynamicTable::AddHeaderItem
        ├── Insert Without Name Reference   → DynamicTable::AddHeaderItem
        └── Duplicate                       → DynamicTable::DuplicateEntry
        │
        ▼
  delta = insert_count_after − insert_count_before
        │
        ├── if (delta > 0)  EmitDecoderFeedback(InsertCountInc, delta)  【§8 反馈】
        │
        └── blocked_registry_->NotifyAll()                              【§5 释放】
              │
              ▼
        遍历 pending_，依次调 retry()
              ├── 若 Decode 成功：clear is_currently_blocked_
              │                   HandleHeaders() → DrainPendingFrames
              └── 若仍阻塞     ：blocked_registry_->Add(self, retry)


═══════════════════════════════════════ ④ decoder stream 接收路径 ═══════════════════════════════════════

  对端发来 decoder stream 字节
        │
        ▼
  QpackDecoderReceiverStream::OnData ──► 解析三类反馈
        │
        ├── Section Ack(stream_id)     → blocked_registry_->AckByStreamId
        ├── Stream Cancellation        → blocked_registry_->RemoveByStreamId
        └── Insert Count Increment(d)  → 提示对端：我们已 insert 到第 X 项
                                         （仅作 known-received-count 推进，不影响本端动态表）
```

---

## 2. 数据结构布局

QPACK 的实现拆成 5 个文件，每个文件各管一件事：

| 文件 | 职责 |
|---|---|
| `static_table.{h,cpp}` | RFC 9204 Appendix A 的 99 项静态表，只读单例。 |
| `dynamic_table.{h,cpp}` | 单端动态表，纯压缩状态——`deque<HeaderItem>` + `unordered_map<(name,value), deque_position>` + 单调递增的 `total_insert_count_`。 |
| `blocked_registry.{h,cpp}` | 解码侧专用——存放"等 IIC 抵达再 retry"的 header block，按 `(stream_id, section_no)` 复合 key 索引。 |
| `qpack_encoder.{h,cpp}` | **同时承担 encoder + decoder 角色**。命名带 Encoder 是历史遗留；Decode/DecodeEncoderInstructions 都在同一类里。 |
| `qpack_constants.h` | RFC 9204 §4.3/§4.5 的所有 wire-format 常量（pattern、mask、prefix），手动展开成 `constexpr uint8_t`。 |

为什么 `QpackEncoder` 是双角色？因为 H3 连接里**每端都各持一对** `qpack_encoder_` / `qpack_decoder_`：自己那个 `qpack_encoder_` 用 `Encode()`、自己那个 `qpack_decoder_` 用 `Decode()`，但二者实例化的是**同一个类**。这导致一个常被忽视的细节——`SetMaxTableCapacity` 设的其实是"我作为 encoder 的发送侧能用多大表"，而**我作为 decoder 能容忍对端多大表**走的是 `SETTINGS_QPACK_MAX_TABLE_CAPACITY`，这两个值不必相同。具体见 §9。

---

## 3. 编码：两遍策略

`QpackEncoder::Encode` 是 quicX 里少见的"明显的两遍算法"。它的存在不是为了性能，而是为了**正确性**：RFC 9204 §4.5 要求 header block prefix 写在头部 payload 的最前面，但 prefix 里包含的 RIC 取决于 payload 里所有字段的最大动态表引用——没扫一遍是算不出来的。

代码实现见 `qpack_encoder.cpp:13-228`，关键骨架：

```text
Pass 1（决策 + 副作用）:
  for each header in ordered_headers:
      尝试 static exact      → kStaticIndexed
      尝试 static name-only  → kStaticNameRef
      尝试 dynamic exact     → kDynamicIndexed / kDynamicPostBaseIndexed
      否则:
          if can_insert_to_dynamic_table:
              dynamic_table_.AddHeaderItem(name, value)         ← 副作用 1
              instruction_sender_({...})                        ← 副作用 2：encoder stream
              kDynamicIndexed (refer to the just-inserted entry)
          else:
              kLiteralNoNameRef
      max_required_insert_count = max(...)

Pass 2（写 wire）:
  WriteHeaderPrefix(max_required_insert_count, base_insert_count_at_start)
  for each encoding:
      根据 EncodeAction 写出对应 wire 表示
```

### 3.1 Header 顺序：伪首部前置

`Encode()` 进来的 `headers` 是 `unordered_map`，遍历顺序不确定。RFC 9114 §4.3 要求伪首部（`:method`、`:scheme`、`:authority`、`:path`、`:status`）必须排在前面且按固定顺序——所以 `Encode()` 第一件事就是按白名单顺序拣出伪首部，剩下的按字典序排。这个排序看起来纯粹是"美学"，但它解决了一个真实问题：**头部 map 迭代顺序在不同机器上不同，会让 fuzz 测试不复现**（commit 备注里的 P2-3）。

### 3.2 静态表 vs 动态表的优先级

匹配顺序硬编码为：static exact → static name-only → dynamic exact → dynamic insert →fallback literal。这背后的考虑是：

- 静态表的索引固定不会失效，**永远是最稳的引用**——RIC 不会因为 static 引用而上升；
- 动态表的引用每次都把 RIC 推高，会对解码侧造成 head-of-line 风险（§5）；
- 所以"能用 static 就别用 dynamic"，即便 dynamic 里有 exact match——也要让位给 static name-only。

### 3.3 dynamic table 的副作用是双重的

Pass 1 里 dynamic insert 不止改本端 `dynamic_table_` 的状态，还**同时往 encoder stream 上写一条 wire 指令**——这就是 `instruction_sender_` 回调。两边的状态必须保持原子一致：本端 `total_insert_count_` 在 push_front 之后才递增，对端在 `DecodeEncoderInstructions` 里也是先 `AddHeaderItem` 再算 delta。任何一边漏写或写早了都会让两端的 RIC 视图错位，导致解码失败。

---

## 4. RIC 与 Base：动态表的"两个时间戳"

QPACK 最容易混淆的概念就是 **Required Insert Count (RIC)** 和 **Base**。两个都是绝对索引（absolute index），但语义不同：

| 字段 | 含义 | 谁产生 | 怎么用 |
|---|---|---|---|
| RIC | 解码这个 header block **所需**的最小 insert count——比这低的话，被引用的动态表项可能还没插入。 | encoder：取 block 中最大动态引用的 absolute index + 1。 | decoder：必须等 `dynamic_table_.GetInsertCount() >= RIC` 才能解。否则 §5 阻塞。 |
| Base | 这个 block "锚定的"动态表起点——从此往新的方向是 **post-base**，往旧的方向是 **pre-base**。 | encoder：取**编码开始时**的 `total_insert_count_`。 | decoder：用于把相对索引还原成绝对索引。 |

两者都用绝对索引，但 RIC ≥ Base 是常见情形——意味着这个 block 引用了"自己刚刚插入的"那批新条目（post-base reference）。代码里：

- RIC：`qpack_encoder.cpp` 中 `max_required_insert_count`（变量名直接对应）。
- Base：`qpack_encoder.cpp` 中 `base_insert_count`，在 Encode 入口处从 `dynamic_table_.GetInsertCount()` 抓快照（`qpack_encoder.cpp:51`）。

### 4.1 RIC 的 wire 编码不是直接写

RFC 9204 §4.5.1.1 设计了一个回环编码方案：

```text
MaxEntries = floor(MaxTableCapacity / 32)
EncodedRIC = (RIC % (2 * MaxEntries)) + 1     当 RIC > 0
EncodedRIC = 0                                当 RIC == 0
```

为什么要绕一圈？因为 RIC 是单调递增的，连接长跑下去会变得很大；用模值传输既能压缩长度又能让 decoder 在不知道 encoder 最新 insert count 的前提下解出真值（前提是 decoder 自己的 insert count 与 RIC 差距 < MaxEntries，否则定义不出唯一真值）。这就是为什么 SETTINGS 里要预先协商 `QPACK_MAX_TABLE_CAPACITY`——MaxEntries 是双方协商决定的。

实现见 `qpack_encoder.cpp:783-796`。注意 `max_entries == 0` 时退化为 1，避免 `RIC % 0` 崩溃。

---

## 5. 阻塞协调：blocked_registry 的全部职责

解码侧最棘手的问题是：**HEADERS 帧到了，但它要引用的动态表项还没插入**。这种情况由 `RIC > dynamic_table_.GetInsertCount()` 检测出，QpackEncoder::Decode 返回 false。

朴素做法是直接报错关连接——但 QPACK 故意设计成允许这种"乱序"，原因是 encoder stream 和 request stream 是两条独立的 QUIC stream，它们的字节并不保证按 encoder 写入顺序到达 decoder。所以 decoder 必须把这个 header block "park" 起来，等动态表追上来再 retry。

### 5.1 三件必须做的事

`req_resp_base_stream.cpp::HandleHeaders` 的 retry 路径解决了三个相互独立的正确性问题（注释里编号 1/2/3）：

**问题 1：retry 必须填进 `self->headers_`，不是局部 map。**
最初版的 retry 把 decode 结果写进了一个 lambda 局部变量 `tmp`，导致重试成功之后 `headers_` 仍然是空的，应用层收到一个**没有任何 header 的请求**。修复就是把 weak_ptr 抓住的 `self->headers_` 作为输出参数。

**问题 2：retry 失败要重新挂回 registry。**
`NotifyAll` 一次会调遍 *所有* pending 回调，但**这一次 IIC delta 不一定够你的 RIC**——其它 block 可能只需要 +1 就解开了，你需要 +5。如果 retry 不重新 `Add`，下一次 IIC 来的时候就找不到你了，header block 永远丢失。修复就是 retry 末尾的 `if (still_blocked) blocked_registry_->Add(self, retry)`。

**问题 3：阻塞期间到达的后续帧必须按序排队。**
DATA 帧、后续的 HEADERS 帧（trailers）都可能在 blocked HEADERS 之后到达。直接派给应用层会导致**应用先看到 body 才看到 header**——HTTP 语义错乱。所以一旦 `is_currently_blocked_` 置位，`OnData/ProcessFrames` 会把后续帧塞进 `pending_blocked_frames_` 队列，等 retry 成功后由 `DrainPendingFrames` 按序回放。

### 5.2 buffer 快照：防止 retry 读错位置

QPACK Decode 是 streaming 解析，会推进 buffer 的读指针。如果第一次 decode 失败已经吃掉了前几个字节（比如 RIC prefix 已被解掉），第二次 retry 直接拿原 buffer 就会从中段开始解——结果是 garbage。

解法是在第一次尝试**之前**就拍快照：

```cpp
auto encoded_fields_template = encoded_fields->CloneReadable(
    encoded_fields->GetDataLength(), /*move_write_pt=*/false);
```

`move_write_pt=false` 是关键——它让 clone 共享底层 chunk 但**不消费**源 buffer 的读指针。每次 retry 再从 template clone 一份新的 readable view，就拿到了一个干净的、读指针指向 prefix 起点的 buffer。代码见 `req_resp_base_stream.cpp:289-303`。

### 5.3 max_blocked_streams 的语义陷阱

`SetMaxBlockedStreams(0)` 的语义是 RFC 9204 §5 规定的："peer's encoder MUST NOT cause any stream to become blocked"——也就是**等价于"完全禁止阻塞"**，每次 `Add` 都返回 false。如果你想要"无限制"，必须显式传 `UINT64_MAX`。这种语义反直觉的设定在 `blocked_registry.h:26-29` 注释里说得很清楚。

更微妙的是 `max_blocked_explicit_` 这个 bool：构造默认 false，意味着**没调过 SetMaxBlockedStreams 的 registry 是无限制的**。这个 fallback 是为了向后兼容老代码路径（早期不知道 SETTINGS 就构造 registry 的场景）。新代码应该一定显式调 `SetMaxBlockedStreams`，把 0 当作"禁止阻塞"来设。

---

## 6. 动态表本身：deque + map + 绝对索引

`DynamicTable` 是个 ~170 行的小类，但它把 QPACK 最容易出 bug 的几处全踩了一遍。

### 6.1 三个索引视角

```text
deque_position    : front=0=最新条目, size()-1=最旧条目（评估退化为 deque size 后的位置）
absolute_index    : RFC 9204 概念，单调递增，evict 也不重用
total_insert_count: 已插入条数（永不下降，等于历史 absolute_index 上界 + 1）
evicted_count     : total_insert_count - deque.size()
```

三者关系：

```text
absolute_index_of_front = total_insert_count − 1
absolute_index_of_back  = evicted_count
deque_position(abs)     = (total_insert_count − 1) − abs_idx
```

### 6.2 `FindHeaderItemByAbsoluteIndex` 的修复历史

最初版本的转换是 `deque_pos = total_insert_count - 1 - absolute_index`，这在**没有 eviction** 时是对的；但一旦 evict 发生，deque 从背面缩短，旧条目的 absolute index 就被"埋"了——再用这个公式访问会越界或拿到错误条目。

修复见 `dynamic_table.cpp:60-80`：先算 `evicted_count = total_insert_count - deque.size()`，落在 `[evicted_count, total_insert_count)` 之外的 absolute index 直接 return nullptr（条目已 evict）。这就是注释里 P1-1 bugfix 的内容。

### 6.3 push_front + 索引整体偏移

每次 `AddHeaderItem` 把新条目从 deque 前端 push 进去，旧条目的 deque position 全部 +1。`headeritem_index_map_`（`(name,value) → deque_pos`）必须同步整体 +1：

```cpp
for (auto& kv : headeritem_index_map_) {
    kv.second += 1;
}
```

这个 O(N) 偏移看起来很难看，但 QPACK 动态表的体量天然不大（典型 4-8KB，几十到几百项），实测远低于 hash table 重建成本。

更微妙的是 evict 时的处理：`headeritem_index_map_` 里可能存在**同名同值的更新条目**（比如 cookie=abc 被插了两次，第二次 push_front 会把第一个的 map 槽 overwrite 成新位置 0，旧位置在 deque 中段还活着）。所以 evict 旧条目时，**只能在 map 槽指向的就是被 evict 那个位置时**才删 map 槽——否则会误删新条目的索引：

```cpp
if (it != headeritem_index_map_.end() && it->second == evicted_index) {
    headeritem_index_map_.erase(it);
}
```

### 6.4 Duplicate 的 dangling reference 陷阱

`DuplicateEntry(absolute_index)` 实现 RFC 9204 §4.3.4 的 Duplicate 指令——本质上是"复制一份 absolute_index 处的条目，作为新条目插到表头"。直觉的实现是：

```cpp
HeaderItem* item = FindHeaderItemByAbsoluteIndex(absolute_index);
return AddHeaderItem(item->name_, item->value_);   // BUG
```

但 `AddHeaderItem` 内部会调 `EvictEntries()` 来腾空间——而被复制的 `item` **本身就可能是即将被 evict 的那个**！于是 evict 把 item 后面的内存释放/复用了，name/value 就成了悬垂引用。

修复非常朴素：先把字符串拷出来再 Add：

```cpp
std::string name = item->name_;
std::string value = item->value_;
return AddHeaderItem(name, value);
```

这是 `dynamic_table.cpp:143-160` 的全部内容。一行注释比代码长。

---

## 7. encoder stream wire format 与 capacity 协商

`DecodeEncoderInstructions` 是接收 encoder stream 字节并落入本端动态表的入口。RFC 9204 §4.3 定义了 4 类指令：

| 指令 | 高位模式 | prefix | 实现入口 |
|---|---|---|---|
| Insert With Name Reference | `1Sxxxxxx` | 6-bit | `DecodeEncoderInstructions` 分支 1 |
| Insert Without Name Reference | `01xxxxxx` | 6-bit | 分支 2 |
| Set Dynamic Table Capacity | `001xxxxx` | 5-bit | `UpdateMaxTableSize` |
| Duplicate | `0001xxxx` | 4-bit | `DuplicateEntry` |

S 位（Static bit，仅 Insert With Name Reference 有）：1=name 来自静态表，0=来自动态表的相对索引（`relative = ric - 1 - abs_idx`）。

这里有一个常被忽视的细节：**Insert With Name Reference 的 dynamic 分支，相对索引是相对于 *encoder 的当前 insert count* 的**——而不是 decoder 的。Decoder 收到时必须用自己刚刚 advance 完的 `total_insert_count_` 来还原绝对索引。这就是为什么 `qpack_encoder.cpp:529` 写出时取的是 `dynamic_table_.GetInsertCount()` 而不是别的什么基准。

### 7.1 capacity 协商：local vs peer

`max_table_capacity_` 是 **min(local, peer)**——RFC 9204 §3.2.3 明文要求双方都不能突破自己的限制。代码实现拆成三个字段（`qpack_encoder.h:30-45`）：

```text
local_max_table_capacity_   : 我配置的上限（一般来自 Http3Settings）
peer_max_table_capacity_    : 对端 SETTINGS 抵达后填入
peer_cap_known_             : 对端 SETTINGS 是否已收到
max_table_capacity_         : 实际生效的 = min(local, peer) (peer 已知时) 或 local (peer 未知时)
```

`SetLocalMaxTableCapacity` / `SetPeerMaxTableCapacity` 都会调 `RecomputeMaxTableCapacity`，里面有一行非常关键的隐含副作用：

```cpp
dynamic_table_.UpdateMaxTableSize(max_table_capacity_);
```

没有这一行就有 bug——构造时 `dynamic_table_(1024)` 就把 max 设成 1024 了，即使 SETTINGS 协商出 16KB，只要忘了 sync 到底层 dynamic_table，AddHeaderItem 会因为"超过 1024"而拒绝插入，但上层 Encode 已经决定 Insert 并发出了 wire 指令——结果对端动态表里有这个条目、本端没有，**两端永久 desync**。这个 bugfix 的注释在 `qpack_encoder.h:93-101`，写得相当详细，值得读一下。

---

## 8. decoder stream 反馈的三类与三种触发

`QpackDecoderReceiverStream::OnData` 把 decoder stream 字节解析成三类反馈：

| 类型 | 触发时机 | 处理 |
|---|---|---|
| Section Acknowledgment | decoder 解完一个 RIC > 0 的 header block | encoder 端：`blocked_registry_->AckByStreamId`——但注意，本仓 encoder 自己**不维护 blocked_registry**（它是 decoder 端的概念）；这里其实是把对端 SectionAck 当作 known-received-count 推进信号。 |
| Stream Cancellation | decoder 端 stream 被对端 reset | encoder 端：从 known-received-count 中扣减、复位对应 stream 的 outstanding sections。 |
| Insert Count Increment | decoder 端有新条目插入到 *encoder 写过的* 位置 | encoder 端：推进 known-received-count，让 encoder 知道哪些动态表项已被对端"确认收到"，可以放心引用而无需担心阻塞。 |

发出反馈的入口统一是 `EmitDecoderFeedback(type, value)`（`qpack_encoder.h:65`），它把字节交给 `decoder_feedback_sender_` 回调——这个回调由 `H3Connection::Init` 阶段绑定到 decoder stream 的 send 方法上。

### 8.1 Section Ack 的 RIC=0 例外

RFC 9204 §4.4.1 明确："The decoder MUST NOT emit Section Acknowledgment for header blocks with a Required Insert Count of 0"。原因是 RIC=0 的 header block 只引用静态表，encoder 完全不需要追踪它的状态——多发个 Section Ack 反而会让 encoder 的 known-received-count 状态机出 bug。

实现见 `req_resp_base_stream.cpp:344-349, 385-392`：每次解出 header block 都先看 `last_decoded_ric_`，>0 才 emit。

### 8.2 IIC delta 的精确计算

`QpackEncoderReceiverStream::ParseEncoderInstructions`（注意是 *Encoder* Receiver Stream，名字反直觉——它接收对端发来的 encoder stream，但本端把字节交给本端的 `qpack_encoder_` 实例处理）必须计算"这次 batch 处理新增了几条"：

```cpp
uint64_t insert_count_before = qpack_encoder_->GetInsertCount();
qpack_encoder_->DecodeEncoderInstructions(buffer);
uint64_t insert_count_after = qpack_encoder_->GetInsertCount();
uint64_t delta = insert_count_after - insert_count_before;

if (delta > 0) {
    qpack_encoder_->EmitDecoderFeedback(kInsertCountInc, delta);
}
```

为什么不能在 `DecodeEncoderInstructions` 内部每插一条就 emit 一次？因为对端常常一次 send 多条 Insert 指令（甚至带 Set Capacity + Duplicate 混合）——如果每次插入都 emit 一次 IIC，decoder stream 的字节量会爆炸，而且 IIC 的语义也是"累计 delta"，应该攒批发。这就是为什么计数差值这步逻辑必须在 stream 层实现，而不在 qpack_encoder 内部实现。

---

## 9. 与上下游的边界

QPACK 在 quicX 中只关心 wire 上的字节和动态表的状态，明确**不关心**：

| 不关心的事 | 它在哪里被处理 |
|---|---|
| HEADERS 帧的封装/解封 | `headers_frame.{h,cpp}`（HTTP/3 frame 层） |
| HEADERS 是否 well-formed（必填 pseudo-header、值合法性） | `req_resp_base_stream` + 上层 router |
| QUIC stream 的拥塞、重传 | `quic/stream/`、`quic/connection/controler/send_control` |
| H3 SETTINGS 协商 | `connection_client/server.cpp` 里的 `HandleSettings` |
| 大小写归一化 | `qpack_encoder.cpp:91-93` 显式 `std::tolower`，因为静态表是全小写 |

反过来看，QPACK 也不主动调任何上层 API——所有"我要发 encoder 指令"都通过 `instruction_sender_` 回调上抛，所有"我要发 decoder 反馈"都通过 `decoder_feedback_sender_` 上抛。这两条 std::function 由 H3 connection 在初始化时绑定到对应的 unidirectional stream 上。这种 callback 注入式解耦让 QPACK 模块在测试时可以脱离真实 QUIC stream，直接 mock 这两个 sender——`qpack_dynamic_table_e2e_test.cpp` 就是这样做的，整套流程都在内存中跑。

---

## 10. 验证入口

| 测试文件 | 覆盖范围 |
|---|---|
| `test/unit_test/http3/qpack/qpack_dynamic_table_e2e_test.cpp` | 端到端：encoder + decoder 双 instance，覆盖 insert / lookup / evict / RIC blocking / Section Ack / IIC 全链路（98 KB 的最大单测）。 |
| `test/unit_test/http3/qpack/qpack_dynamic_update_test.cpp` | Set Dynamic Table Capacity 后 evict 行为。 |
| `test/unit_test/http3/stream/qpack_blocked_inline_data_test.cpp` | 阻塞 + DATA 帧排队回放（§5.1 问题 3）。 |
| `test/unit_test/http3/stream/qpack_encoder_stream_test.cpp` | encoder stream wire format 编解码。 |
| `test/unit_test/http3/stream/qpack_decoder_stream_test.cpp` | decoder stream 三类反馈解析。 |
| `test/unit_test/http3/frame/qpack_encoder_frames_test.cpp` | 4 类 encoder 指令的 frame 级 codec。 |
| `test/unit_test/http3/frame/qpack_decoder_frames_test.cpp` | 3 类 decoder 反馈的 frame 级 codec。 |
| `test/perf/qpack_perf_test.cpp` | Encode/Decode 吞吐与动态表命中率。 |

排查 QPACK 问题的常用入口：

- 对端报 `QPACK_DECOMPRESSION_FAILED` → 看 §6.2 absolute index 转换，或 §7.1 capacity 协商；
- HEADERS 永远不上交应用 → 看 §5 阻塞协调，特别是 §5.1 三个问题里的 1（headers_ 没填）和 2（retry 没重新 Add）；
- 内存增长不止 → 看 §6 evict 是否被 capacity 协商正确触发（特别是 §7.1 那一行 `dynamic_table_.UpdateMaxTableSize`）；
- 偶发的 garbage 解码 → 看 §5.2 buffer 快照。

---

## 11. 关键不变量

排查 QPACK bug 时，下面这几条不变量是首要核对项：

1. **`total_insert_count_` 单调递增、永不回退**——即便 `UpdateMaxTableSize(0)` 把整个动态表 evict 光，`total_insert_count_` 也保持原值。RIC 编码依赖这个性质。
2. **encoder 写出的 wire 指令与本端 `dynamic_table_` 的状态严格同步**——`AddHeaderItem` 成功后立即调 `instruction_sender_`，二者之间不能有可能 throw 的代码。
3. **`max_table_capacity_` 永远 = min(local, peer)**——只在 `RecomputeMaxTableCapacity` 中赋值，且必须同步刷到 `dynamic_table_`。
4. **`is_currently_blocked_` 与 `pending_blocked_frames_` 同生死**——置位时必须保证 retry 已经成功 Add 到 registry，清位时必须立即 DrainPendingFrames。
5. **`last_decoded_ric_` 在每次 `Decode()` 末尾更新**——上层判断"要不要发 Section Ack"完全依赖这个值，遗漏更新会让 Section Ack 永远不发或乱发。
6. **`base_insert_count` 在 Encode 入口处从 `GetInsertCount()` 抓快照**——Pass 1 中 dynamic insert 会推高 `total_insert_count_`，但 Base 必须停留在编码起点。

---

## 12. 关联文档

- [`packet_lifecycle.md`](packet_lifecycle.md) §6 QUIC stream 字节如何抵达 H3 layer。
- [`handshake_state_machine.md`](handshake_state_machine.md) H3 SETTINGS 在握手哪个阶段抵达，QPACK 如何 hook 进去。
- [`ownership_and_memory.md`](ownership_and_memory.md) `CloneReadable` 的共享 chunk 语义（§5.2 buffer 快照依赖此）。

---

## 13. 关联 RFC

| RFC | 章节 | 内容 |
|---|---|---|
| RFC 9204 | §3.2 | 动态表语义、capacity 协商。 |
| RFC 9204 | §3.2.4 | 绝对索引（§4-§6）。 |
| RFC 9204 | §4.3 | encoder stream 4 类指令（§7）。 |
| RFC 9204 | §4.4 | decoder stream 3 类反馈（§8）。 |
| RFC 9204 | §4.5 | header block prefix + RIC/Base 编码（§4）。 |
| RFC 9204 | §2.1.4 | 阻塞与 RIC 比较（§5）。 |
| RFC 9204 | §5 | SETTINGS 参数（QPACK_MAX_TABLE_CAPACITY、QPACK_BLOCKED_STREAMS）。 |
| RFC 9204 | Appendix A | 静态表（99 项）。 |
| RFC 7541 | §4.1 | 表项大小公式 `name + value + 32` —— quicX 的 `CalculateEntrySize` 直接复用。 |
| RFC 9114 | §4.3 | 伪首部顺序（§3.1）。 |
