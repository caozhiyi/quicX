# Handshake State Machine

This document covers how a quicX connection state advances from `Connecting` to `Connected`, then to `Closing / Draining / Closed`; and how, during the handshake, the three encryption levels `Initial / Handshake / 1-RTT` are driven to readiness one after another by BoringSSL. For the keys produced/discarded during the handshake and per-packet encryption/decryption see [`packet_lifecycle.md`](packet_lifecycle.md); for PTO retransmission and loss detection during the handshake see [`loss_recovery.md`](loss_recovery.md); for connection-level buffering and lifetimes see [`ownership_and_memory.md`](ownership_and_memory.md).

---
## 1. Two Layers: Upper Connection State + TLS-Driven Encryption Levels

quicX splits "the handshake" into **two orthogonal layers**; understanding this is the prerequisite for reading the code:

| Layer | Class / Field | State Space | Who Drives It |
| :--- | :--- | :--- | :--- |
| Upper connection | `ConnectionStateMachine` (5-state enum `ConnectionStateType`) | `Connecting / Connected / Closing / Draining / Closed` | QUIC protocol events (HANDSHAKE_DONE received / local Close / CONNECTION_CLOSE received / timeout) |
| Lower crypto | `ConnectionCrypto::cur_encryption_level_` + `cryptographers_[4]` | `kInitial / kEarlyData / kHandshake / kApplication` | BoringSSL, driven via the `SetReadSecret` / `SetWriteSecret` callbacks |

**The two layers are not strictly synchronized**:

- When the upper layer enters `Connected`:
  - Server side: immediately after `DoHandleShake()` returns true, `state_machine_.OnHandshakeDone()` is called (see `connection_server.cpp:182`).
  - Client side: it must wait until the peer's **HANDSHAKE_DONE frame** is received to advance (`connection_client.cpp:299`, `HandleHandshakeDoneFrame`); this is a hard requirement of RFC 9001 §4.1.2 — `DoHandleShake()` returning true only means TLS is complete, not that the QUIC handshake is complete.
- The readiness of the lower encryption levels is controlled solely by BoringSSL; `Connection` is always just the implementer of the `TlsHandlerInterface` callbacks and never actively "switches" levels.

---

## 2. The Upper 5-State Machine

Implementation: [`connection_state_machine.h`](../../../src/quic/connection/connection_state_machine.h) / [`connection_state_machine.cpp`](../../../src/quic/connection/connection_state_machine.cpp) (~130 lines, very light logic).

### 2.1 State Transition Diagram

```
            initial → ── HANDSHAKE_DONE received / server DoHandleShake done
              │                      │
              ▼                      ▼
       ┌─Connecting─┐         ┌──Connected──┐
       │            │         │             │
       │        OnClose       OnClose       │
       │            │         │             │
       │            ▼         ▼             │
       │       ┌────Closing────┐  CONNECTION_CLOSE received
       │       │  send CC,     │  ──────────────────┐
       │       │  wait 1×PTO   │                   │
       │       └─────┬─────────┘                   │
       │             │ OnCloseTimeout              │
       │             ▼                             ▼
       │       ┌─────Closed─────┐    ┌─────Draining────┐
       └──────►│ no send/recv,  │    │ peer CC received,│
               │ resource reclaim│   │ no more sending, │
               └────────────────┘    │ wait 3×PTO      │
                       ▲             └────────┬────────┘
                       │  OnCloseTimeout       │
                       └──────────────────────┘
```

### 2.2 Three Groups of Event Entry Points

`ConnectionStateMachine` exposes only 4 event methods + 4 query methods. The event entry points and where they are triggered:

| Event | Trigger Point | Behavior |
| :--- | :--- | :--- |
| `OnHandshakeDone()` | Server: after `DoHandleShake() == true`; client: after a `HANDSHAKE_DONE` frame is received | `Connecting → Connected`, plus the `QuicHandshakeSuccess` metric |
| `OnClose()` | Local end calls `Close()` / fatal error (e.g. `handshake_error_cb_` triggered by `SendAlert`) | `Connecting \| Connected → Closing`; if triggered from `Connecting`, additionally emits `QuicHandshakeFail` |
| `OnConnectionCloseFrameReceived()` | The peer's `CONNECTION_CLOSE` frame is received | Any non-terminal state `→ Draining` |
| `OnCloseTimeout()` | The 1×/3×PTO timer started by `connection_closer.cpp` fires | `Closing \| Draining → Closed` |

**Note the asymmetry between `Closing` and `Draining`**:

- `Closing` means **the local end closes first**: the local end may still need to retransmit `CONNECTION_CLOSE` until the peer ACKs or it times out (~1×PTO), so `AllowSend()` still permits sending. Queries like `CanSendData()` — "strictly, can application data be sent" — return false.
- `Draining` means **the peer closed first**: the local end **must not send any packet** (RFC 9000 §10.2.2); it merely keeps the fd around to observe whether late packets still arrive, for ~3×PTO.

### 2.3 Two Semantically Different "Can I Send/Receive" Checks

```cpp
// Loose version (path 1): handles CONNECTION_CLOSE retransmission during protocol-level close
bool AllowSend() const     { return state == Connecting || state == Connected; }
bool AllowReceive() const  { return state != Closed; }

// Strict version (path 2): whether the application-layer data path is open
bool CanSendData() const   { return state == Connected; }
bool CanReceiveData() const{ return state == Connected || state == Closing; }
bool ShouldIgnorePackets() const { return state == Draining || state == Closed; }
```

The "state gates" mentioned in `packet_lifecycle.md` §5 use this set of queries; when writing code, prefer semantically clear names like `IsTerminating()` / `ShouldIgnorePackets()` rather than comparing against enum values directly.

---

## 3. The Lower Layer: TLS-Driven Encryption Level Advance

`EncryptionLevel` is defined in [`crypto/tls/type.h`](../../../src/quic/crypto/tls/type.h):

```cpp
enum EncryptionLevel : int8_t {
    kInitial     = 0,
    kEarlyData   = 1,   // 0-RTT
    kHandshake   = 2,
    kApplication = 3,   // 1-RTT
};
```

During the handshake these 4 levels become readable / writable **asynchronously and independently**. BoringSSL calls back through `TlsHandlerInterface` (in [`crypto/tls/tls_connection.h`](../../../src/quic/crypto/tls/tls_connection.h)); `ConnectionCrypto` implements this interface ([`connection_crypto.cpp`](../../../src/quic/connection/connection_crypto.cpp)).

### 3.1 BoringSSL → quicX Callback Overview

| BoringSSL Callback | `ConnectionCrypto` Implementation | Purpose |
| :--- | :--- | :--- |
| `SetReadSecret(level, secret)` | Creates/updates the read key of `cryptographers_[level]` | Inbound packets at this level can be decrypted |
| `SetWriteSecret(level, secret)` | Creates/updates the write key of `cryptographers_[level]`; if `level == kEarlyData`, triggers `early_data_ready_cb_` | Outbound packets at this level can be encrypted |
| `WriteMessage(level, data, len)` | Posted to `crypto_stream_->Send()`, queued as CRYPTO frames | TLS wants to emit handshake bytes |
| `FlushFlight()` | no-op (quicX hands handshake bytes directly to the crypto stream, no buffering) | (legacy interface) |
| `SendAlert(level, alert)` | Calls `handshake_error_cb_(0x0100 + alert, "tls handshake alert")`, which the connection turns into `CONNECTION_CLOSE` | see §6 |
| `OnTransportParams(level, tp, len)` | Decodes `TransportParam` → `transport_param_cb_` | First time the peer's transport parameters are obtained |

`cryptographers_[]` is an array of length 4, each slot a `std::shared_ptr<ICryptographer>`. "This level is ready" is determined by the corresponding slot being **non-null and** having both AEAD/HP keys installed.

### 3.2 The Special Case of Initial Keys: Derived from the DCID

Initial keys are not negotiated by TLS — they are derived via HKDF from the **DCID** of the client's first Initial packet (RFC 9001 §5.2). So none of the three installation entry points go through `SetReadSecret/SetWriteSecret`:

| Entry Point | When | Who Calls It | Key Parameters |
| :--- | :--- | :--- | :--- |
| `InstallInitSecret` / `WithVersion` | After the first Initial packet is received | `BaseConnection::OnInitialPacket` (`connection_base.cpp:725`) | `secret = DCID bytes`, salt determined by the QUIC version |
| `InstallInitSecretForRetry` | When the client resends Initial after receiving Retry | `ClientConnection::HandleRetryPacket` | Uses **asymmetric CIDs**: the write key uses the Retry's SCID, the read key uses the client's own SCID |
| `RekeyInitialForVersion` | After compatible version negotiation (RFC 9368) succeeds | In the client's `OnInitialPacket`, once it confirms the server is using v2 | Same DCID but re-derived with the new version's salt |

All three cases share the same underlying logic: **HKDF-Extract(salt, dcid) → initial_secret → HKDF-Expand → client/server direction keys**. The complexity is concentrated in the DCID/version combinations of Retry and Compatible VN, documented in detail in the file header comment (see `connection_crypto.cpp:182-285`).

### 3.3 How the Upper Layer Picks the Current "Outbound Level"

```cpp
EncryptionLevel ConnectionCrypto::GetCurEncryptionLevel() {
    uint8_t level = crypto_stream_->GetWaitSendEncryptionLevel();
    return (EncryptionLevel)std::min<uint8_t>(level, cur_encryption_level_);
}
```

- `crypto_stream_->GetWaitSendEncryptionLevel()`: the **lowest** level in the CryptoStream that **still has data pending** — handshake bytes must go out in order from low to high level, otherwise the peer cannot decrypt them.
- `cur_encryption_level_`: the level set by the most recent `SetReadSecret/SetWriteSecret` — indicating "where BoringSSL has advanced to".
- Taking the minimum ensures: even if 1-RTT is already ready but there are still Initial-level handshake bytes unsent, this round finishes sending Initial first before switching to the higher level.

---

## 4. Sequence of a Complete Handshake

### 4.1 Client Perspective (Simplified: No Retry / No 0-RTT)

```
[Client]                                                 [Server]
ClientConnection() ──┐
  ConnectionCrypto::InstallInitSecret(dcid)              (after first packet arrives)
  TLSClientConnection::Init()                            ServerConnection()
  TLSClientConnection::DoHandleShake()                     InstallInitSecret(client_dcid)
    ├─ BoringSSL generates ClientHello                   TLSServerConnection::Init()
    └─ callback WriteMessage(kInitial, ClientHello bytes)
       └─ crypto_stream_->Send → CRYPTO frame (Initial)

  ── Initial(ClientHello) ──────────────────────────────►  OnInitialPacket
                                                              ├─ InstallInitSecret(my_dcid)
                                                              ├─ packet.DecodeWithCrypto
                                                              ├─ FrameProcessor dispatches CRYPTO →
                                                              │   ConnectionCrypto::OnCryptoFrame
                                                              │   ├─ crypto_stream->OnFrame
                                                              │   └─ TlsServer::ProcessCryptoData
                                                              │       └─ DoHandleShake()
                                                              │           ├─ SetReadSecret(kHandshake, …)
                                                              │           ├─ SetWriteSecret(kHandshake, …)
                                                              │           ├─ SetWriteSecret(kApplication, …)
                                                              │           └─ WriteMessage(kInitial,  ServerHello)
                                                              │               WriteMessage(kHandshake, EE/Cert/Fin)
                                                                            returns false (handshake incomplete)

  ◄─ Initial(ServerHello)+Handshake(EE,Cert,Fin) ─────────  ToSendFrame …

  OnInitialPacket(ServerHello)
    └─ ProcessCryptoData → DoHandleShake
       ├─ SetReadSecret(kHandshake, …)
       ├─ SetWriteSecret(kHandshake, …)
       └─ SetReadSecret(kApplication, …)  ← still missing the final client Finished
  OnHandshakePacket(EE,Cert,Fin)
    └─ ProcessCryptoData → DoHandleShake → true (TLS complete)
       └─ WriteMessage(kHandshake, ClientFinished)
       but the client **is still Connecting**

  ── Handshake(ClientFinished) ─────────────────────────► OnHandshakePacket
                                                              └─ DoHandleShake() == true
                                                                 ├─ ToSendFrame(HandshakeDoneFrame)
                                                                 ├─ send_manager_.SetHandshakeComplete()
                                                                 ├─ Discard Initial / Handshake space (read+send)
                                                                 ├─ cryptographers_[kInitial] = nullptr
                                                                 │ (implicit: HANDSHAKE is also discarded,
                                                                 │  marked by send/recv control)
                                                                 └─ state_machine_.OnHandshakeDone()
                                                                    Connecting → Connected

  ◄─ 1-RTT(HANDSHAKE_DONE, …) ──────────────────────────  OnNormalPacket
  HandleHandshakeDoneFrame
    ├─ state_machine_.OnHandshakeDone()      Connecting → Connected
    ├─ send_manager_.SetHandshakeComplete()
    └─ Discard Initial / Handshake spaces
```

Key code paths:

| Step | File:Line |
| :--- | :--- |
| Client starts TLS | `connection_client.cpp:24` (`tls_connection_->Init()`) → `:223` (`InstallInitSecret`) → `:225` (`DoHandleShake`) |
| Server TLS completes + sends HANDSHAKE_DONE + discards the first two PN spaces | `connection_server.cpp:150-188` |
| Client handles the HANDSHAKE_DONE frame | `connection_client.cpp:297-330` |

### 4.2 The 0-RTT Bypass

When `SetWriteSecret(kEarlyData, …)` fires, `ConnectionCrypto` does not notify the state machine; instead it directly invokes the registered `early_data_ready_cb_` (`connection_crypto.cpp:81`). 0-RTT data can be sent while still in the `Connecting` state, but the PNS is already Application; when the upper layer decides "can application data really be transmitted", besides consulting the state machine it must also check whether `cryptographers_[kEarlyData]` is ready + whether the server accepted the early data.

---

## 5. Anti-Amplification: The Send Gate During the Handshake

Implementation: [`anti_amplification_controller.h`](../../../src/quic/connection/controller/anti_amplification_controller.h), owned by `SendManager`.

**Why it matters**: before the address is validated, the server may send at most **3 ×** the number of bytes received from that unvalidated address (RFC 9000 §8.1); this is the core mechanism preventing QUIC from becoming a reflection amplifier.

```cpp
class AntiAmplificationController {
    bool is_unvalidated_;
    uint64_t sent_bytes_;
    uint64_t received_bytes_;

    static constexpr uint64_t kAmplificationFactor = 3;
    static constexpr uint64_t kDefaultInitialCredit = 400; // lets a PATH_CHALLENGE go out
    // ...
};
```

Key points:

1. **Who is behind the gate**: the server is unvalidated by default; connection-level "validation" completes once the client's Handshake packets can be decrypted (RFC 9000 §8.1). The client is validated by default (no such gate needed).
2. **Budget replenishment**: every byte received from the client's Initial adds +1 to the budget, so the maximum sendable = `received × 3`.
3. **The other way to open the budget**: on path migration the connection re-enters unvalidated (see `EnterUnvalidatedState()` at [`connection_path_manager.h`](../../../src/quic/connection/connection_path_manager.h) §60-69), and only after validation via `PATH_CHALLENGE / PATH_RESPONSE` does it call `ExitUnvalidatedState()`.
4. **Escape hatch near the limit**: `IsNearLimit()` (90%) lets the server consider sending a Retry when about to hit the wall, forcing the client to send Initial again in exchange for more budget.

---

## 6. Handshake Failure: TLS Alert → CONNECTION_CLOSE

`SendAlert(level, alert)` (`connection_crypto.cpp:97`) implements the hard requirement of RFC 9001 §4.8: any fatal TLS alert during the handshake must be delivered to the peer as `CRYPTO_ERROR (0x0100 + alert)` inside a `CONNECTION_CLOSE`, otherwise the connection "hangs silently".

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

`handshake_error_cb_` is registered by `BaseConnection` and lands in `ConnectionCloser::CloseWithError()`, which ultimately: sets state `Connecting → Closing` and attempts to send `CONNECTION_CLOSE` at an appropriate encryption level.

---

## 7. The Two PTO Timers of the Closing Phase

Neither `Closing`'s 1×PTO nor `Draining`'s 3×PTO comes from `ConnectionStateMachine`; they are maintained by [`connection_closer.cpp`](../../../src/quic/connection/connection_closer.cpp) / [`connection_timer_coordinator.cpp`](../../../src/quic/connection/connection_timer_coordinator.cpp), which after every RTT update fetch the live PTO via `send_manager_.GetPTO(max_ack_delay)`, multiply by 3, and submit it to the `EventLoop` timer (`connection_closer.cpp:50-56`).

| State | Wait Duration | What Can Be Done | Packets Received During It |
| :--- | :--- | :--- | :--- |
| `Closing` | ~1×PTO | Retransmit `CONNECTION_CLOSE` (throttled) + compute the next retransmission time | Processed; on receiving the peer's `CONNECTION_CLOSE`, switch to `Draining` |
| `Draining` | ~3×PTO | **Send nothing at all** | All dropped (`ShouldIgnorePackets()`) |
| `Closed` | — | Wait for connection-table cleanup | No longer routed here (`packet_lifecycle.md` §3: already removed from cid_worker_map_/conn_map_) |

The PTO computation here falls under RFC 9002 — see [`loss_recovery.md`](loss_recovery.md).

---

## 8. Key Invariants
Hard constraints reused over and over when writing code / hunting bugs:

1. **HANDSHAKE_DONE is one-way**: sent by the server, received by the client. The client must never send it (`connection_server.cpp:93-100` enforces: if the server receives one, it always closes with `PROTOCOL_VIOLATION`).
2. **Client entering `Connected` ≠ TLS handshake complete**: the client must wait for the HANDSHAKE_DONE frame; `DoHandleShake() == true` is only a necessary condition.
3. **Both spaces must be discarded after HANDSHAKE_DONE**: for both `kInitialNumberSpace` and `kHandshakeNumberSpace`, send + recv must call `DiscardPacketNumberSpace`, otherwise old packet numbers leak, causing wrong ACK computation & broken loss detection. Both endpoints honor this pair: `connection_server.cpp:166-169` and `connection_client.cpp:305-308`.
4. **Initial keys are bound to the DCID**: any event that changes the DCID (Retry / Compatible VN) must `Reset()` + re-derive the Initial keys; never assume the old Initial keys still work.
5. **Anti-amplification only applies to the server's unvalidated path**: there is no such limit in the client direction; nor for the server after validation. Treat it as the hard cap "unvalidated address → receive 1, send 3".
6. **No packet may be sent during `Draining`**: not even `CONNECTION_CLOSE` retransmissions (this is the difference from `Closing`).

---

## 9. Related Documents

- [`packet_lifecycle.md`](packet_lifecycle.md) — the dispatch branches of `OnPackets / OnInitialPacket / OnHandshakePacket / OnNormalPacket` and the `state_machine_.AllowReceive()` / `ShouldIgnorePackets()` gates.
- [`loss_recovery.md`](loss_recovery.md) — the PTO formula and the logic that stops PTO probes after `SetHandshakeComplete()`.
- [`ownership_and_memory.md`](ownership_and_memory.md) — ownership and lifetime of `cryptographers_[]` / `crypto_stream_`.
- [`congestion_control.md`](congestion_control.md) — the handshake-phase congestion window cap and its coupling with `SetHandshakeComplete()`.

## 10. Related RFCs

- RFC 9000 §4.1 / §4.10 / §8.1 / §10.2 — address validation, anti-amplification, the Closing/Draining phases.
- RFC 9001 §4.1 / §4.8 / §5.2 — TLS / QUIC integration, handshake failure handling, Initial key derivation.
- RFC 9001 §6 — Key Update (implemented in `TriggerKeyUpdate / TriggerReadKeyUpdate`, not directly coupled to the state machine, but sharing `cryptographers_[kApplication]`).
- RFC 9368 — Compatible Version Negotiation (implementation of `RekeyInitialForVersion`).
- RFC 9369 — QUIC v2 (Initial salt table).
