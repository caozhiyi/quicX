# 握手状态机

> 这篇文档讲 quicX 的连接状态怎么从 `Connecting` 推进到 `Connected`，再到 `Closing / Draining / Closed`；以及在握手期间 `Initial / Handshake / 1-RTT` 三个加密级别如何被 BoringSSL 驱动着依次就绪。
> 
> 关联：握手过程产生/丢弃的密钥对应每包加解密，详见 [`packet_lifecycle.md`](packet_lifecycle.md)；握手中 PTO 重传与丢包检测属于 RFC 9002 范畴，详见 [`loss_recovery.md`](loss_recovery.md)；连接级缓冲与生命周期详见 [`ownership_and_memory.md`](ownership_and_memory.md)。

---

## 1. 两层状态：上层连接状态 + TLS 驱动的加密级别

quicX 把"握手"这件事拆成**正交的两层**，理解这一点是看懂代码的前提：

| 层 | 类 / 字段 | 状态空间 | 谁推进 |
| :--- | :--- | :--- | :--- |
| 上层连接 | `ConnectionStateMachine`（5 态枚举 `ConnectionStateType`） | `Connecting / Connected / Closing / Draining / Closed` | QUIC 协议事件（HANDSHAKE_DONE 收到 / 主动 Close / 收到 CONNECTION_CLOSE / 超时） |
| 下层加密 | `ConnectionCrypto::cur_encryption_level_` + `cryptographers_[4]` | `kInitial / kEarlyData / kHandshake / kApplication` | BoringSSL 通过 `SetReadSecret` / `SetWriteSecret` 回调驱动 |

**两层并不严格同步**：

- 上层进入 `Connected` 的时机：
  - 服务端：`DoHandleShake()` 返回 true 后立刻调用 `state_machine_.OnHandshakeDone()`（见 `connection_server.cpp:182`）。
  - 客户端：必须等到收到对端的 **HANDSHAKE_DONE 帧**才推进（`connection_client.cpp:299`，`HandleHandshakeDoneFrame`）；这是 RFC 9001 §4.1.2 的硬性要求——`DoHandleShake()` 返回 true 只代表 TLS 完成，不代表 QUIC 握手完成。
- 下层加密级别的就绪只受 BoringSSL 控制；`Connection` 永远只是 `TlsHandlerInterface` 的回调实现者，不主动"切换"级别。

---

## 2. 上层 5 态状态机

实现：[`connection_state_machine.h`](../../../src/quic/connection/connection_state_machine.h) / [`connection_state_machine.cpp`](../../../src/quic/connection/connection_state_machine.cpp)（约 130 行，逻辑很轻）。

### 2.1 状态转移图

```
            初始 → ── 收到 HANDSHAKE_DONE / 服务端 DoHandleShake 完成
              │                      │
              ▼                      ▼
       ┌─Connecting─┐         ┌──Connected──┐
       │            │         │             │
       │        OnClose       OnClose       │
       │            │         │             │
       │            ▼         ▼             │
       │       ┌────Closing────┐  收 CONNECTION_CLOSE
       │       │  发出 CC，     │  ──────────────────┐
       │       │  等 1×PTO     │                   │
       │       └─────┬─────────┘                   │
       │             │ OnCloseTimeout              │
       │             ▼                             ▼
       │       ┌─────Closed─────┐    ┌─────Draining────┐
       └──────►│ 不收不发，资源回收 │    │ 收到对端 CC，    │
               └────────────────┘    │ 不再发包，等 3×PTO │
                       ▲             └────────┬────────┘
                       │  OnCloseTimeout       │
                       └──────────────────────┘
```

### 2.2 三组事件入口

`ConnectionStateMachine` 只暴露 4 个事件方法 + 4 个查询方法。以下是事件入口与触发位置：

| 事件 | 触发点 | 行为 |
| :--- | :--- | :--- |
| `OnHandshakeDone()` | 服务端 `DoHandleShake() == true` 后；客户端收到 `HANDSHAKE_DONE` 帧后 | `Connecting → Connected`，并打点 `QuicHandshakeSuccess` |
| `OnClose()` | 本端调用 `Close()` / 致命错误（如 `SendAlert` 触发的 `handshake_error_cb_`） | `Connecting | Connected → Closing`；若从 `Connecting` 触发额外打点 `QuicHandshakeFail` |
| `OnConnectionCloseFrameReceived()` | 收到对端 `CONNECTION_CLOSE` 帧 | 任意非终态 `→ Draining` |
| `OnCloseTimeout()` | `connection_closer.cpp` 启动的 1×/3×PTO 定时器超时 | `Closing | Draining → Closed` |

**注意 `Closing` 与 `Draining` 不对称**：

- `Closing` 是**本端先关**：本端可能仍需重传 `CONNECTION_CLOSE` 直到对端 ACK 或自身超时（约 1×PTO），所以 `AllowSend()` 此时仍允许发送。`CanSendData()` 这类"严格能否发送应用数据"的查询则会返回 false。
- `Draining` 是**对端先关**：本端**不能再发任何包**（RFC 9000 §10.2.2），只是把 fd 留着观察是否还会收到迟到的包，时长 ~3×PTO。

### 2.3 两个语义不同的"能不能发/收"

```cpp
// 宽松版本（用于路径一）：处理协议层 close 期间的 CONNECTION_CLOSE 重传
bool AllowSend() const     { return state == Connecting || state == Connected; }
bool AllowReceive() const  { return state != Closed; }

// 严格版本（用于路径二）：业务层数据流是否畅通
bool CanSendData() const   { return state == Connected; }
bool CanReceiveData() const{ return state == Connected || state == Closing; }
bool ShouldIgnorePackets() const { return state == Draining || state == Closed; }
```

`packet_lifecycle.md` §5 提到的"状态门禁"用的是这套查询；写代码时优先用 `IsTerminating()` / `ShouldIgnorePackets()` 这类语义清楚的命名，而不是直接和枚举值比较。

---

## 3. 下层：TLS 驱动的加密级别推进

`EncryptionLevel` 定义在 [`crypto/tls/type.h`](../../../src/quic/crypto/tls/type.h)：

```cpp
enum EncryptionLevel : int8_t {
    kInitial     = 0,
    kEarlyData   = 1,   // 0-RTT
    kHandshake   = 2,
    kApplication = 3,   // 1-RTT
};
```

握手期间这 4 个级别**异步、独立**地变得可读 / 可写。BoringSSL 通过 `TlsHandlerInterface`（在 [`crypto/tls/tls_connection.h`](../../../src/quic/crypto/tls/tls_connection.h)）回调，`ConnectionCrypto` 实现这个接口（[`connection_crypto.cpp`](../../../src/quic/connection/connection_crypto.cpp)）。

### 3.1 BoringSSL → quicX 回调一览

| BoringSSL 回调 | `ConnectionCrypto` 实现 | 作用 |
| :--- | :--- | :--- |
| `SetReadSecret(level, secret)` | 创建/更新 `cryptographers_[level]` 的读密钥 | 该级别可解密入包 |
| `SetWriteSecret(level, secret)` | 创建/更新 `cryptographers_[level]` 的写密钥；若 `level == kEarlyData` 触发 `early_data_ready_cb_` | 该级别可加密出包 |
| `WriteMessage(level, data, len)` | 投递到 `crypto_stream_->Send()` 排队成 CRYPTO 帧 | TLS 想发握手字节 |
| `FlushFlight()` | no-op（quicX 把握手字节直接交给 crypto stream，没有缓冲） | （历史接口） |
| `SendAlert(level, alert)` | 调用 `handshake_error_cb_(0x0100 + alert, "tls handshake alert")`，由连接转化为 `CONNECTION_CLOSE` | 见 §6 |
| `OnTransportParams(level, tp, len)` | 解码 `TransportParam` → `transport_param_cb_` | 第一次拿到对端传输参数 |

`cryptographers_[]` 是一个长度 4 的数组，每个槽是 `std::shared_ptr<ICryptographer>`。"该级别就绪"的判定就是对应槽**非空且** AEAD/HP key 都安装好了。

### 3.2 Initial 密钥的特殊性：来源于 DCID

Initial 密钥不是 TLS 协商的——它是用客户端首个 Initial 包的 **DCID** 通过 HKDF 派生的（RFC 9001 §5.2）。所以三个安装入口都不走 `SetReadSecret/SetWriteSecret`：

| 入口 | 何时 | 谁调用 | 关键参数 |
| :--- | :--- | :--- | :--- |
| `InstallInitSecret` / `WithVersion` | 收到首个 Initial 包后 | `BaseConnection::OnInitialPacket`（`connection_base.cpp:725`） | `secret = DCID 字节`，盐由 QUIC 版本决定 |
| `InstallInitSecretForRetry` | 收到 Retry 后客户端重发 Initial 时 | `ClientConnection::HandleRetryPacket` | 用**非对称 CID**：写密钥用 Retry 的 SCID，读密钥用客户端自己的 SCID |
| `RekeyInitialForVersion` | 兼容版本协商（RFC 9368）成功后 | 客户端 `OnInitialPacket` 中确认服务端用的是 v2 时 | 同 DCID 但换新版本的盐重新派生 |

这三种情形共享同一个底层逻辑：**HKDF-Extract(salt, dcid) → initial_secret → HKDF-Expand → 客户端/服务端方向密钥**。复杂度集中在 Retry 与 Compatible VN 的 DCID/版本组合上，已在文件头注释里详细写明（参见 `connection_crypto.cpp:182-285`）。

### 3.3 上层如何选当前的"出包级别"

```cpp
EncryptionLevel ConnectionCrypto::GetCurEncryptionLevel() {
    uint8_t level = crypto_stream_->GetWaitSendEncryptionLevel();
    return (EncryptionLevel)std::min<uint8_t>(level, cur_encryption_level_);
}
```

- `crypto_stream_->GetWaitSendEncryptionLevel()`：CryptoStream 里**最低的、有数据待发**的级别——握手字节必须按级别从低到高出去，否则对端解不开。
- `cur_encryption_level_`：最近一次 `SetReadSecret/SetWriteSecret` 触发设置的级别——表示"BoringSSL 当前推进到哪了"。
- 取较小值是为了：即使 1-RTT 已经就绪，但仍有 Initial 级别的握手字节没发完，这一回合先把 Initial 发完再换更高级别。

---

## 4. 一次完整握手的时序

### 4.1 客户端视角（无 Retry / 无 0-RTT 简化版）

```
[Client]                                                 [Server]
ClientConnection() ──┐
  ConnectionCrypto::InstallInitSecret(dcid)              （首包到达后）
  TLSClientConnection::Init()                            ServerConnection()
  TLSClientConnection::DoHandleShake()                     InstallInitSecret(client_dcid)
    ├─ BoringSSL 生成 ClientHello                         TLSServerConnection::Init()
    └─ 回调 WriteMessage(kInitial, ClientHello bytes)
       └─ crypto_stream_->Send → CRYPTO frame (Initial)

  ── Initial(ClientHello) ──────────────────────────────►  OnInitialPacket
                                                              ├─ InstallInitSecret(my_dcid)
                                                              ├─ packet.DecodeWithCrypto
                                                              ├─ FrameProcessor 派发 CRYPTO →
                                                              │   ConnectionCrypto::OnCryptoFrame
                                                              │   ├─ crypto_stream->OnFrame
                                                              │   └─ TlsServer::ProcessCryptoData
                                                              │       └─ DoHandleShake()
                                                              │           ├─ SetReadSecret(kHandshake, …)
                                                              │           ├─ SetWriteSecret(kHandshake, …)
                                                              │           ├─ SetWriteSecret(kApplication, …)
                                                              │           └─ WriteMessage(kInitial,  ServerHello)
                                                              │               WriteMessage(kHandshake, EE/Cert/Fin)
                                                                            返回 false（握手未完）

  ◄─ Initial(ServerHello)+Handshake(EE,Cert,Fin) ─────────  ToSendFrame …

  OnInitialPacket(ServerHello)
    └─ ProcessCryptoData → DoHandleShake
       ├─ SetReadSecret(kHandshake, …)
       ├─ SetWriteSecret(kHandshake, …)
       └─ SetReadSecret(kApplication, …)  ← 还差最后客户端 Finished
  OnHandshakePacket(EE,Cert,Fin)
    └─ ProcessCryptoData → DoHandleShake → true (TLS 完成)
       └─ WriteMessage(kHandshake, ClientFinished)
       但客户端**仍是 Connecting**

  ── Handshake(ClientFinished) ─────────────────────────► OnHandshakePacket
                                                              └─ DoHandleShake() == true
                                                                 ├─ ToSendFrame(HandshakeDoneFrame)
                                                                 ├─ send_manager_.SetHandshakeComplete()
                                                                 ├─ Discard Initial / Handshake space (read+send)
                                                                 ├─ cryptographers_[kInitial] = nullptr
                                                                 │ （隐式：HANDSHAKE 也被弃，由 send/recv control 标记）
                                                                 └─ state_machine_.OnHandshakeDone()
                                                                    Connecting → Connected

  ◄─ 1-RTT(HANDSHAKE_DONE, …) ──────────────────────────  OnNormalPacket
  HandleHandshakeDoneFrame
    ├─ state_machine_.OnHandshakeDone()      Connecting → Connected
    ├─ send_manager_.SetHandshakeComplete()
    └─ Discard Initial / Handshake spaces
```

关键代码路径：

| 步骤 | 文件:行 |
| :--- | :--- |
| 客户端起 TLS | `connection_client.cpp:24` (`tls_connection_->Init()`) → `:223` (`InstallInitSecret`) → `:225` (`DoHandleShake`) |
| 服务端 TLS 完成 + 发 HANDSHAKE_DONE + 丢弃前两套 PN 空间 | `connection_server.cpp:150-188` |
| 客户端处理 HANDSHAKE_DONE 帧 | `connection_client.cpp:297-330` |

### 4.2 0-RTT 路径的旁路

`SetWriteSecret(kEarlyData, …)` 触发后，`ConnectionCrypto` 不通知状态机，而是直接调用注册好的 `early_data_ready_cb_`（`connection_crypto.cpp:81`）。0-RTT 数据在 `Connecting` 状态下就能发，但 PNS 仍是 Application；上层判断"能否真正传应用数据"时除了看状态机，还要看 `cryptographers_[kEarlyData]` 是否就绪 + 服务端是否接受了 early data。

---

## 5. anti-amplification：握手期间的发送闸门

实现：[`anti_amplification_controller.h`](../../../src/quic/connection/controler/anti_amplification_controller.h)，由 `SendManager` 持有。

**意义**：服务端在地址未验证前，对未验证地址发送的字节数 ≤ **3 ×** 收到该地址的字节数（RFC 9000 §8.1）；这是阻止 QUIC 沦为反射放大器的核心机制。

```cpp
class AntiAmplificationController {
    bool is_unvalidated_;
    uint64_t sent_bytes_;
    uint64_t received_bytes_;

    static constexpr uint64_t kAmplificationFactor = 3;
    static constexpr uint64_t kDefaultInitialCredit = 400; // 让 PATH_CHALLENGE 能发出去
    // ...
};
```

要点：

1. **谁在闸门后**：服务端默认 unvalidated；连接级"验证"在收到客户端的 Handshake 包能解密时完成（RFC 9000 §8.1）。客户端默认 validated（不需此闸门）。
2. **预算补给**：每收到一个客户端的 Initial 字节就把预算 +1，因此最大可发 = `received × 3`。
3. **打开预算的另一条路**：路径迁移时会重新进入 unvalidated（见 [`connection_path_manager.h`](../../../src/quic/connection/connection_path_manager.h) §60-69 的 `EnterUnvalidatedState()`），用 `PATH_CHALLENGE / PATH_RESPONSE` 完成验证后再 `ExitUnvalidatedState()`。
4. **临近上限的逃生口**：`IsNearLimit()`（90%）让服务端在快撞墙时考虑发 Retry，逼客户端二次发 Initial 以换取更多预算。

---

## 6. 握手失败：TLS Alert → CONNECTION_CLOSE

`SendAlert(level, alert)`（`connection_crypto.cpp:97`）实现 RFC 9001 §4.8 的硬要求：握手期间任何 fatal TLS alert 必须以 `CRYPTO_ERROR (0x0100 + alert)` 形式装进 `CONNECTION_CLOSE` 投递给对端，否则连接会"静默挂着"。

```cpp
void ConnectionCrypto::SendAlert(EncryptionLevel level, uint8_t alert) {
    static const uint64_t kCryptoErrorBase = 0x0100;
    uint64_t error_code = kCryptoErrorBase + alert;
    // ...
    if (handshake_error_cb_) {
        handshake_error_cb_(error_code, "tls handshake alert");
    }
}
```

`handshake_error_cb_` 由 `BaseConnection` 注册，落到 `ConnectionCloser::CloseWithError()`，最终：状态 `Connecting → Closing`，并尝试在合适的加密级别发 `CONNECTION_CLOSE`。

---

## 7. 关闭阶段的两条 PTO 计时

`Closing` 的 1×PTO 与 `Draining` 的 3×PTO 都不来自 `ConnectionStateMachine`，而是由 [`connection_closer.cpp`](../../../src/quic/connection/connection_closer.cpp) / [`connection_timer_coordinator.cpp`](../../../src/quic/connection/connection_timer_coordinator.cpp) 在每次 RTT 更新后用 `send_manager_.GetPTO(max_ack_delay)` 取实时 PTO 值再 ×3 投递给 `EventLoop` 定时器（`connection_closer.cpp:50-56`）。

| 状态 | 等待时长 | 期间能做什么 | 期间收到的包 |
| :--- | :--- | :--- | :--- |
| `Closing` | ~1×PTO | 重传 `CONNECTION_CLOSE`（节流）+ 计算下一次重传时机 | 处理；若收到对端 `CONNECTION_CLOSE` 切到 `Draining` |
| `Draining` | ~3×PTO | **完全不发包** | 全部丢弃（`ShouldIgnorePackets()`） |
| `Closed` | — | 等连接表清理 | 已不被路由到（`packet_lifecycle.md` §3 的 cid_worker_map_/conn_map_ 已删除） |

这部分的 PTO 计算属于 RFC 9002 范围，详见 [`loss_recovery.md`](loss_recovery.md)。

---

## 8. 不变量速查（debug 用）

写代码 / 排查 bug 时反复用到的几条硬约束：

1. **HANDSHAKE_DONE 单向**：服务端发，客户端收。客户端绝不能发（`connection_server.cpp:93-100` 验证：服务端收到一律 `PROTOCOL_VIOLATION` 关闭）。
2. **客户端进 `Connected` ≠ TLS 握手完成**：客户端必须等收到 HANDSHAKE_DONE 帧；`DoHandleShake() == true` 只是必要条件。
3. **HANDSHAKE_DONE 之后必须丢两套空间**：`kInitialNumberSpace` 与 `kHandshakeNumberSpace` 的 send + recv 都要 `DiscardPacketNumberSpace`，否则旧 packet number 会泄漏，导致 ACK 计算错误 & 丢包检测异常。两端实现都遵守这一对：`connection_server.cpp:166-169` 与 `connection_client.cpp:305-308`。
4. **Initial 密钥与 DCID 绑定**：任何换 DCID 的事件（Retry / Compatible VN）都必须 `Reset()` + 重新派生 Initial 密钥；不能指望旧 Initial 密钥还能工作。
5. **anti-amplification 仅作用于服务端的 unvalidated 路径**：客户端方向无此限制；服务端在已验证后也无此限制。把它当作"地址未验证 → 收 1 发 3"的硬上限即可。
6. **`Draining` 期间不能发任何包**：包括 `CONNECTION_CLOSE` 也不再重传（与 `Closing` 区别于此）。

---

## 9. 关联文档

- [`packet_lifecycle.md`](packet_lifecycle.md) — `OnPackets / OnInitialPacket / OnHandshakePacket / OnNormalPacket` 的派发分支与 `state_machine_.AllowReceive()` / `ShouldIgnorePackets()` 门禁。
- [`loss_recovery.md`](loss_recovery.md) — PTO 计算公式与 `SetHandshakeComplete()` 后停止 PTO probe 的逻辑。
- [`ownership_and_memory.md`](ownership_and_memory.md) — `cryptographers_[]` / `crypto_stream_` 的所有权与生命周期。
- [`congestion_control.md`](congestion_control.md) — 握手期间的拥塞窗口上限与 `SetHandshakeComplete()` 之间的耦合点。

## 10. 关联 RFC

- RFC 9000 §4.1 / §4.10 / §8.1 / §10.2 — Address validation、anti-amplification、Closing/Draining 阶段。
- RFC 9001 §4.1 / §4.8 / §5.2 — TLS / QUIC 集成、握手失败处理、Initial 密钥派生。
- RFC 9001 §6 — Key Update（实现见 `TriggerKeyUpdate / TriggerReadKeyUpdate`，与状态机不直接耦合，但共用 `cryptographers_[kApplication]`）。
- RFC 9368 — Compatible Version Negotiation（`RekeyInitialForVersion` 的实现）。
- RFC 9369 — QUIC v2（Initial 盐表）。
