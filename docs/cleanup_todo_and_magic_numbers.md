# 工程清理清单：TODO 项 与 硬编码魔法数字

> 扫描时间：2026-05-23
> 扫描范围：`src/**`、`include/**`（排除 `build*`、`test`、`third`、`example`、`docs`、`scripts`、`qlogs*`）
> 扫描方法：`search_content` + 上下文核对，已剔除 RFC 注释中的位模式（如 `1xxxxxxx`、`001xxxxx`）

本清单仅作工程修正补充用，**不涉及发布阻塞项**。发布相关任务请参见 `docs/release_plan_v0.1.0.md`。

---

## Part 1 · TODO 注释清单（28 条）

工程中未发现 `FIXME` / `HACK` / 独立 `XXX` 关键字（`xxxxxxxx` 仅出现于 RFC 9204 位模式注释，已排除）。下列均为 `// TODO`。

### 1.1 HTTP/3（11 条）

| 位置 | 注释 | 类别 |
|---|---|---|
| `src/http3/stream/push_receiver_stream.cpp:28` | `// TODO send callback` | 未实现回调 |
| `src/http3/stream/push_receiver_stream.cpp:122` | `// TODO check if headers is complete and headers length is correct` | 缺少校验 |
| `src/http3/stream/control_receiver_stream.cpp:20` | `// TODO send callback` | 未实现回调 |
| `src/http3/stream/push_sender_stream.cpp:74` | `// Send DATA frame if body exists, TODO may send multiple DATA frames if body is large` | 大 body 分帧未实现 |
| `src/http3/stream/if_stream.h:27` | `virtual void Close(uint32_t error_code) {} // TODO: implement` | 接口空实现 |
| `src/http3/stream/req_resp_base_stream.cpp:142` | `// TODO check if headers is complete and headers length is correct` | 缺少校验 |
| `src/http3/frame/frame_decoder.cpp:92` | `// resumes correctly. (Regression: TODO #24)` | 引用 Bug #24，已修复，可清理引用 |
| `src/http3/frame/goaway_frame.cpp:50` | `// TODO: check length` | 缺少长度校验 |
| `src/http3/frame/settings_frame.cpp:70` | `while (len > 0) {  // TODO: check max loop times` | 潜在 DoS 风险，应加上界 |
| `src/http3/connection/if_connection.cpp:152` | `100);  // 100ms TODO, do not use fix time, loop support defer` | 应改可配置 |
| `src/http3/connection/connection_server.cpp:308` | `// TODO: implement goaway` | 优雅关闭未实现 |

### 1.2 QUIC 核心（15 条）

| 位置 | 注释 | 类别 |
|---|---|---|
| `src/quic/stream/send_stream.cpp:273` | `// TODO: 1300 is the max length of a stream frame` | 应改 MTU/MSS 推导 |
| `src/quic/stream/if_stream.cpp:21` | `// return; // TODO crypto stream need resend` | 死代码 + 需重发逻辑 |
| `src/quic/stream/crypto_stream.cpp:40` | `// TODO: move to common/util` | 重构提示 |
| `src/quic/frame/new_token_frame.h:31` | `// ...future Initial packet. TODO: use shared buffer span` | 内存所有权改造 |
| `src/quic/connection/connection_crypto.cpp:92` | `// TODO close connectoin whit error` | 错误未传播（含拼写错） |
| `src/quic/connection/connection_id_manager.cpp:119` | `// TODO(D): the auto-incrementing sequence number ...` | 设计存疑，需复核 |
| `src/quic/connection/controler/recv_control.cpp:60` | `uint8_t ecn = 0;  // TODO: Get from packet header when ECN is implemented` | ECN 未实现 |
| `src/quic/connection/controler/rtt_calculator.cpp:57` | `// TODO:` | 空 TODO，应清理或补充 |
| `src/quic/connection/controler/send_manager.cpp:70` | `uint64_t can_send_size = 1500;  // TODO: set to mtu size` | 应取动态 MTU |
| `src/quic/connection/controler/send_control.cpp:387` | `// fires → 8s idle timeout (TODO Bug #18).` | Bug #18 已 [~]，需要核对状态 |
| `src/quic/connection/connection_server.cpp:60` | `data.dst_ip = "0.0.0.0";  // (TODO: get from socket)` | qlog 字段未填 |
| `src/quic/connection/connection_server.cpp:61` | `data.dst_port = 0; // TODO: get from socket` | 同上 |
| `src/quic/connection/connection_server.cpp:66` | `// TODO: Add IsIPv6() method to Address class` | API 缺失 |
| `src/quic/connection/connection_server.cpp:95` | `// TODO: implement server retry packet` | RFC 9000 §17.2.5 Retry 未实现 |
| `src/quic/connection/connection_server.cpp:132` | `// TODO, The server MUST send a HANDSHAKE_DONE frame as soon as the handshake is complete` | RFC 9001 §4.1.2 时机存疑 |
| `src/quic/connection/connection_client.cpp:238` | `// TODO: Add IsIPv6() method to Address class` | API 缺失（同上） |
| `src/quic/connection/connection_base.cpp:281` | `// set transport param. TODO define tp length` | 长度限制未设 |

### 1.3 Common（2 条）

| 位置 | 注释 | 类别 |
|---|---|---|
| `src/common/network/windows/io_handle.cpp:334` | `// TODO: Implement via WSARecvMsg ancillary data options if needed` | Windows 平台缺失 |
| `src/common/network/windows/io_handle.cpp:349` | `// TODO: implement IP_TOS/TrafficClass on Windows if required` | Windows 平台缺失 |

### 1.4 建议处置优先级

| 优先级 | 项目 | 理由 |
|---|---|---|
| **P0**（建议发布前修） | `settings_frame.cpp:70` 无上限循环 | DoS 风险，恶意 peer 可构造任意长 SETTINGS |
| **P0** | `connection_crypto.cpp:92` 错误未关连接 | 握手错误静默吞掉，可能导致挂起 |
| **P1** | `send_stream.cpp:273` / `send_manager.cpp:70` 硬编码 1300/1500 | MTU 应统一来源，避免 v6/Path MTU 场景错误 |
| **P1** | `if_connection.cpp:152` 100ms 固定周期 | 性能/能耗，且影响测试稳定性 |
| **P1** | `connection_server.cpp:308` GOAWAY 未实现 | RFC 9114 §5.2 优雅关闭必须能力 |
| **P2** | qlog 字段未填（`connection_server.cpp:60-66`） | 不影响协议，但影响 Bug #25 之后的可观测性 |
| **P2** | `Address::IsIPv6()` 缺失 | 两处 client/server 重复 TODO，一次性补 |
| **P2** | `connection_id_manager.cpp:119` 序列号设计存疑 | 已经过 Bug #14 修复但 TODO 未清，需复核 |
| **P3** | `frame_decoder.cpp:92` 引用 TODO #24 | Bug #24 已 [x]，注释陈旧 |
| **P3** | `rtt_calculator.cpp:57` 空 TODO | 信号噪声 |
| **P3** | Push 流 / Crypto 流 / NEW_TOKEN 改造 | 功能完整性增强，非阻塞 |

---

## Part 2 · 硬编码魔法数字清单（高价值候选）

> 仅列"语义不明、应改具名常量"的高价值点；明显的位掩码、协议固定常量（`0xff`、TLS handshake type 等）不计入。

### A. 超时 / 时长 / 间隔

| 位置 | 字面量 | 上下文 | 建议常量名 |
|---|---|---|---|
| `src/http3/connection/if_connection.cpp:152` | `100` | HTTP/3 流清理周期 100 ms | `kStreamCleanupIntervalMs` |
| `src/upgrade/handlers/base_smart_handler.cpp:33` | `30000` | 协议协商超时 30 s | `kUpgradeNegotiationTimeoutMs` |
| `src/quic/connection/connection_path_manager.cpp:38` | `6000` | 路径验证超时 6 s 默认值 | `kDefaultPathValidationTimeoutMs`（RFC 9000 §8.2.4 建议 ≥3×PTO） |
| `src/quic/connection/controler/send_manager.cpp` | `100` | flow-control recheck 周期 | `kFlowControlRecheckIntervalMs`（已在 Bug #21 注释） |
| `src/quic/connection/connection_base.cpp` | `10000` | idle timeout 默认 10 s | `kDefaultIdleTimeoutMs`（应来自 transport param） |

### B. MTU / 包 / 帧 大小

| 位置 | 字面量 | 上下文 | 建议常量名 |
|---|---|---|---|
| `src/quic/stream/send_stream.cpp:273` | `1300` | STREAM frame 最大 payload（带 TODO） | `kMaxStreamFramePayload` 或推导自 `kQuicMaxPacketSize - kFrameHeaderOverhead` |
| `src/quic/connection/controler/send_manager.cpp:70` | `1500` | 单次 send 缓冲（带 TODO） | 应取 `path_mtu_` 而非裸 1500 |
| `src/quic/connection/connection_base.cpp` | `1200` | PATH_CHALLENGE/RESPONSE bypass 包大小 | `kProbingPacketSize`（RFC 9000 §14.1 最小 1200） |
| 多处 | `1460` | TCP/QUIC 经典 MSS（initial cwnd 计算等） | `kQuicMSS` 或来自 transport |

> **建议**：在 `src/quic/common/constants.h` 集中定义 MTU 体系，由 `path_manager` 暴露动态值，发送侧统一通过它取大小，杜绝 `1300` / `1500` 双标。

### C. 缓冲区 / 容量

| 位置 | 字面量 | 上下文 | 备注 |
|---|---|---|---|
| 多处 send/recv buffer 默认容量 | `4096` / `8192` / `16384` | 无注释 | 建议归并到 `kDefaultStreamBufferBytes` 等 |
| `recv_stream` 重组上界 | `1024` | out-of-order frames 上限（曾在 Bug #22 出现）| 应改 `kMaxOutOfOrderFrames` |

> 这一类需要逐文件 read 才能确定，**未在本次扫描完整列出**；建议作为后续单独 commit "buffer constants centralization" 处理。

### D. 协议常量样式（裸字面量，无 enum 包裹）

| 位置 | 字面量 | 上下文 | 备注 |
|---|---|---|---|
| 多处 ALPN 字符串构造 | `"\x02h3"` 长度前缀 | 应改 `kAlpnH3` | 避免长度/字符串错配 |
| 错误码若干处 | 直接 `0x100`/`0x10c` 等 | 应使用 `QuicErrorCode` enum | RFC 9000 §20 / §20.1 |

> 这一项需要进一步审查 `frame/`、`packet/`、`connection/` 子目录里所有十六进制裸量，建议按子模块分批。

---

## Part 3 · 行动建议

### 3.1 立即处理（P0）
1. `src/http3/frame/settings_frame.cpp:70` — 加循环上界，最少 `len > 0 && loop_count < kMaxSettingsLoops`。
2. `src/quic/connection/connection_crypto.cpp:92` — 把握手错误传播为 `CRYPTO_ERROR` CONNECTION_CLOSE。

### 3.2 一次重构（P1，建议合一个 commit）
- 新增 `src/quic/common/constants.h`，集中所有 MTU/超时/缓冲容量。
- 替换 `send_stream.cpp:273` 的 `1300`、`send_manager.cpp:70` 的 `1500`、`if_connection.cpp:152` 的 `100`、`base_smart_handler.cpp:33` 的 `30000`、`connection_path_manager.cpp:38` 的 `6000`。
- 同步更新 unit test fixture（如有引用）。

### 3.3 清理（P3）
- `src/quic/connection/controler/rtt_calculator.cpp:57` 空 TODO 删除。
- `src/http3/frame/frame_decoder.cpp:92` 把 "TODO #24" 改为 "Fix for Bug #24（已修复）"。
- `src/quic/connection/controler/send_control.cpp:387` 把 "TODO Bug #18" 同步成当前状态（Bug #18 在 `TODO.md` 标记 `[~]`，需根据最新验证结论收口）。

### 3.4 不打算修的项目
- `src/common/network/windows/io_handle.cpp:334,349` — Windows ECN/TOS 平台能力，按需后置。
- `src/quic/frame/new_token_frame.h:31` — `shared_buffer_span` 重构属于性能优化，非正确性，按需排期。

---

> 完成上述修正后，`TODO.md` 中无需新增条目；若把 P0/P1 一次性处理掉，可在 `CHANGELOG.md` 加一条 "chore: centralize constants and remove stale TODOs"。
