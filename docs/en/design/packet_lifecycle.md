# Packet Lifecycle: From the NIC to Frames

This document traces the full path a UDP datagram travels inside quicX — from socket readability, through parsing and routing, decryption, and finally being broken down into frames dispatched to the upper layers. After reading it you should be able to:

- Point out the code location and responsibility boundary of each stage;
- Explain, under the multi-threaded model, **which thread a packet starts on and which thread it ends on**;
- Know at which layer to add logs/breakpoints when debugging "packets get dropped" or "packets get processed twice".

This document only covers the **receive direction** (receive path). The send direction (worker-driven scheduling + sendmmsg batch transmission) is a separate chain — see the send-side discussion in [`design/ownership_and_memory.md`](ownership_and_memory.md).

---

## 1. The Overall Path

```
[ kernel UDP socket ]
        │  EPOLLIN / EVFILT_READ / FD_READ
        ▼
UdpReceiver::OnRead(fd)              ← src/quic/udp/udp_receiver.cpp
        │  RecvFromBatch (recvmmsg)    one syscall fetches up to 64 datagrams
        │  fills NetPacket (from the thread-local packet pool)
        ▼
IPacketReceiver::OnPacket(pkt)       ← src/quic/udp/if_receiver.h
        │  the Master is the receiver, dispatching further to Workers
        ▼
MsgParser::ParsePacket()             ← src/quic/quicx/msg_parser.cpp
        │  parse header flag → identify long/short, version, DCID
        │  product: PacketParseResult (cid, packets array, raw datagram size)
        ▼
Route to a Worker by DCID
   ├─ Master::OnPacket               ← src/quic/quicx/master.cpp
   │      cid_worker_map_ lookup; on miss, dispatch randomly
   │
   └─ WorkerWithThread::HandlePacket ← src/quic/quicx/worker_with_thread.cpp
          ThreadSafeBlockQueue::Emplace (cross-thread delivery)
        ▼
Worker's event loop Pops it out
   └─ Worker::HandlePacket → InnerHandlePacket
        │  ServerWorker / ClientWorker each have their own implementation
        │  conn_map_ lookup → hit means an existing connection; miss + Initial → create new
        ▼
BaseConnection::OnPackets(now, packets)   ← src/quic/connection/connection_base.cpp
        │  state-machine filtering (Closing / Draining take dedicated branches)
        │  for each packet: DispatchByType
        ▼
Branch by packet type
   ├─ OnInitialPacket   →  install Initial keys → OnNormalPacket
   ├─ OnHandshakePacket →  OnNormalPacket
   ├─ On0rttPacket      →  OnNormalPacket (with early-data keys)
   ├─ On1rttPacket      →  OnNormalPacket (with Key-Phase detection)
   ├─ OnRetryPacket     →  special case: the client resends Initial
   └─ OnVersionNegotiationPacket → special case: the client switches version and reconnects
        ▼
IPacket::DecodeWithCrypto(...)       ← src/quic/packet/*.cpp
        │  Header Protection removal → AEAD decrypt → split into frames
        ▼
FrameProcessor::HandleFrames(...)    ← src/quic/connection/connection_frame_processor.cpp
        │  iterate frames, call the matching handler by FrameType
        ▼
Dispatch to the matching manager
   ├─ STREAM / RESET_STREAM / STOP_SENDING / MAX_STREAM_DATA → StreamManager
   ├─ ACK                                                    → SendManager
   ├─ CRYPTO                                                 → ConnectionCrypto / TLS
   ├─ NEW_CONNECTION_ID / RETIRE_CONNECTION_ID               → ConnectionIDCoordinator
   ├─ PATH_CHALLENGE / PATH_RESPONSE                         → PathManager
   └─ CONNECTION_CLOSE / CONNECTION_CLOSE_APP                → StateMachine + ConnectionCloser
        ▼
   RecvControl::OnPacketRecv (record packet number, decide when to send an ACK)
        ▼
   reset idle timeout
```

---

## 2. Stage One: Socket Readable → NetPacket

### 2.1 Who Is Listening on the fd

The EventLoop runs on some thread; the UDP socket's fd is registered into the EventLoop's IO multiplexer via [`UdpReceiver::AddReceiver`](../../../src/quic/udp/udp_receiver.cpp). Linux uses epoll, macOS kqueue, Windows select (IOCP is a completion-based model, incompatible with the ready-pull model of `IEventDriver`/`UdpReceiver`; the former `IOCPEventDriver` emulation has been deleted), uniformly wrapped in [`src/common/network/if_event_driver.h`](../../../src/common/network/if_event_driver.h).

The handler attached at registration is `UdpReceiver` itself (it implements the `IFdHandler` interface), so when a readable event fires it is handled by `UdpReceiver::OnRead(fd)`.

### 2.2 One OnRead Drains the Socket

Historically `OnRead` did one `recvfrom` per packet, causing a full wakeup → process → ACK loop per packet; ACK aggregation (`kAckThreshold=10`) almost never triggered and throughput was severely depressed.

The current implementation takes the batch path:

| Step | Behavior |
| :--- | :--- |
| 1 | Get a set of `NetPacket` from the thread-local `IPacketallocator::Malloc()` |
| 2 | Validate that each NetPacket's writable span can hold a full IPv4 MTU (pool-reuse pitfall: see `udp_receiver.cpp` comment §2.4) |
| 3 | One `RecvFromBatch`: on Linux a single `recvmmsg(MSG_DONTWAIT)`; on macOS/Windows a `recvmsg`/`WSARecvMsg` loop |
| 4 | For each received datagram, set peer address, socket fd, receive timestamp, ECN byte |
| 5 | Call `IPacketReceiver::OnPacket(pkt)` to deliver them one by one |

### 2.3 What Is a NetPacket

Defined in [`src/quic/udp/net_packet.h`](../../../src/quic/udp/net_packet.h); essentially **a carrier for a received datagram**:

| Field | Meaning |
| :--- | :--- |
| `buffer_` | Byte content (`shared_ptr<IBuffer>`; the buffer's internal chunks come from `BlockMemoryPool`) |
| `addr_` | Peer address |
| `sock_` | The socket fd this packet was received on |
| `time_` | Receive timestamp (milliseconds, UTC), used for RTT and idle timeout |
| `ecn_` | IP ECN codepoint (2 bits) |

NetPacket carries no protocol semantics — it is just "a blob of bytes from the NIC + metadata".

### 2.4 Buffers Come from the Pool

The `IBuffer` used by `NetPacket` is allocated by [`IPacketallocator`](../../../src/quic/udp/if_packet_allocator.h). The production path uses `PoolPacketallocator`, whose underlying chunks come from `common::BlockMemoryPool`. One common pitfall: a NetPacket recycled from the pool may have its writable region squeezed below one MTU because external `SharedBufferSpan` references are still held. `OnRead` works around this with a retry loop of up to 8 attempts; if a clean buffer still can't be obtained, it shortens this round's batch.

---

## 3. Stage Two: Parsing and Routing

### 3.1 The Two Implementations of IPacketReceiver::OnPacket

| Mode | Receiver Implementation | Routing |
| :--- | :--- | :--- |
| Single-threaded (`QuicClient` / simple services) | `Master` | After taking the packet, proceed directly to §4 |
| Multi-threaded (`MasterWithThread` + multiple `WorkerWithThread`) | `Master` | See §3.3 for cross-thread dispatch |

### 3.2 MsgParser Extracts the CID

[`MsgParser::ParsePacket`](../../../src/quic/quicx/msg_parser.cpp) does two things:

1. Calls `DecodePackets()` ([`src/quic/packet/packet_decode.cpp`](../../../src/quic/packet/packet_decode.cpp)) to split one datagram into a set of `IPacket`s (a datagram may contain multiple coalesced packets, e.g. Initial + Handshake + 1-RTT);
2. Takes the first packet's header, extracts the `Destination Connection ID`, and writes it into `PacketParseResult.cid_`.

> The edge-case handling of `DecodePackets` (undecodable trailing bytes, unknown versions, packets that cannot be decrypted after coalescing) is documented in the RFC 9000 §12.2 comments inside the source.

### 3.3 Cross-Thread Dispatch (Multi-Threaded Mode)

```
Master thread                            Worker thread
─────────────                            ─────────────
Master::OnPacket
   │
   └→ MsgParser::ParsePacket
          │
          ├─ cid_worker_map_ hit
          │    → worker->HandlePacket(packet_info)
          │           │
          │           └→ WorkerWithThread::HandlePacket
          │                    packet_queue_.Emplace(std::move(packet_info))   ─────┐
          │                                                                         │
          └─ miss (typically: Initial)                                               │
               → pick a random worker, same as above                                 │
                                                                                     ▼
                                                              Worker's EventLoop wakes up
                                                              Worker::Process / loop tick
                                                                  packet_queue_.TryPop
                                                                  → InnerHandlePacket
```

**Two easy-to-get-wrong points**:

1. **`PacketParseResult` must be movable**. It holds a `shared_ptr<NetPacket>` and a set of `shared_ptr<IPacket>`; cross-thread delivery goes through `Emplace(std::move(...))` by move. A hand-written, unnecessary copy ctor would "silently degrade to a deep copy", leaking one reference per queue transit and eventually pinning the NetPacket and the underlying BufferChunk in place. The comments in `msg_parser.h` contain the record of a 92MB/132MB memory-leak reproduction; the conclusion: **follow the Rule of Zero, do not write your own copy ctor**.
2. **The CID table is only a hint**. The "pick a random worker" on a `cid_worker_map_` miss looks absurd, but Initial packets by nature have no CID-to-worker mapping yet; some worker must catch it and create the connection. Afterwards `HandleAddConnectionId` writes the CID into the master's table, and subsequent packets are routed precisely.

### 3.4 Single-Threaded Mode

Single-threaded mode **still has a master**; it just shares the same `IEventLoop` with the worker, running on the same thread, thereby saving the `WorkerWithThread` layer and the cross-thread `packet_queue_`:

- `MasterWithThread` starts as usual; `UdpReceiver` registers on the master's event loop;
- The worker attaches to the same loop via `master_event_loop_->AddFixedProcess(worker, ...)`, and each tick the master thread drives `worker->Process()` in passing (see the `kSingleThread` branch in `Init()` of [`quic_client.cpp`](../../../src/quic/quicx/quic_client.cpp));
- The receive path becomes: `UdpReceiver::OnRead` → `Master::OnPacket` → `MsgParser::ParsePacket` → `Worker::HandlePacket` → directly `Worker::InnerHandlePacket`, **no enqueueing, no thread switch**.

`QuicClient` uses this mode by default. The shutdown contract on `if_worker.h` was explicitly written accordingly: "In single-threaded mode the master thread is also the worker thread; doing `Stop + Join` on the master is sufficient."


---

## 4. Stage Three: The Connection-Side Entry

At this point the packet finally reaches the processing of a specific connection. `Worker::InnerHandlePacket` is a virtual method with two implementations:

### 4.1 ServerWorker::InnerHandlePacket

The core branches of [`src/quic/quicx/worker_server.cpp`](../../../src/quic/quicx/worker_server.cpp):

| Case | Behavior |
| :--- | :--- |
| `conn_map_` hits the DCID | Take the connection, update socket / peer address / ECN, call `OnPackets` |
| Miss, and the first packet is not Initial | Drop (error log) |
| Miss, first packet is Initial, datagram < 1200 bytes | Refuse (RFC 9000 §14.1) |
| Miss, unsupported version | Send Version Negotiation |
| Miss, Retry needed | Send a Retry packet, do **not** create a connection |
| Miss, connection allowed | `make_shared<ServerConnection>` register callbacks, inject sender, bind transport params, add to `connecting_set_`, start the handshake watchdog timer, and finally `OnPackets` |

> The Retry strategy is controlled by `RetryPolicy::{NEVER, ALWAYS, SELECTIVE}`. In SELECTIVE mode, the states of `ConnectionRateMonitor` and `IPRateLimiter` decide whether to send a Retry.

### 4.2 ClientWorker::InnerHandlePacket

The client's `conn_map_` usually has a single entry (typical scenario), and the implementation is far simpler than the server's: directly `conn_map_.find` → `OnPackets`. The Version Negotiation / Retry handling lives inside `ClientConnection::On*Packet` rather than at the worker layer.

### 4.3 A Pinning Detail

Before calling `OnPackets`, `InnerHandlePacket` pins the connection with a **local `shared_ptr` copy**:

```cpp
auto connection = conn->second;   // not conn->second.get()
...
connection->OnPackets(...);
```

The reason: inside `OnPackets`, `OnStateToDraining` → `InvokeConnectionCloseCallback` may fire, and the latter removes the entry from `conn_map_`. Without the pin, the iterator would hold the last reference and `this` would be destructed mid-call.

---

## 5. Stage Four: The State-Machine Gate

[`BaseConnection::OnPackets`](../../../src/quic/connection/connection_base.cpp) first checks the connection state:

| State | Handling |
| :--- | :--- |
| `Closing` | Take `HandlePacketsInClosingState`: try to decrypt and find `CONNECTION_CLOSE`; if found, enter Draining; if not, retransmit our previously sent CLOSE at a measured pace |
| `Draining` / `Closed` | Take `DropPacketsInDrainingState`: drop everything (with qlog enabled, one `packet_dropped` event per dropped packet) |
| `Connecting` / `Connected` | Enter normal processing |

Normal processing calls `DispatchByType` on each `IPacket`, routing by packet type to the matching `On*Packet` method (see the overview in §1). After successful processing it calls `RecvControl::OnPacketRecv` to register the packet number, as input for later ACK decisions.

Finally `timer_coordinator_->ResetIdleTimer()` — **as long as the datagram contains one packet that decrypts successfully, the idle timer is reset**. This is required by RFC 9000 §10.1.

---

## 6. Stage Five: Decryption and Frame Extraction

### 6.1 The Unified Entry of OnNormalPacket

Regardless of Initial / Handshake / 0-RTT / 1-RTT, everything converges into `OnNormalPacket`. It does three things:

1. Fetches the `ICryptographer` for the corresponding encryption level from `ConnectionCrypto`;
2. Calls `IPacket::DecodeWithCrypto(buffer)`: remove Header Protection, AEAD-decrypt, and split the payload into a frame sequence;
3. Hands the frames to `FrameProcessor`.

Each packet type corresponds to a different `IPacket` subclass ([`src/quic/packet/`](../../../src/quic/packet/)), each implementing its own `DecodeWithCrypto`:

| Type | File |
| :--- | :--- |
| Initial | `init_packet.cpp` |
| Handshake | `handshake_packet.cpp` |
| 0-RTT | `rtt_0_packet.cpp` |
| 1-RTT | `rtt_1_packet.cpp` |
| Retry | `retry_packet.cpp` |
| Version Negotiation | `version_negotiation_packet.cpp` |

### 6.2 The 1-RTT Key Update Bypass

When decryption fails, `On1rttPacket` checks whether the Key Phase flipped (`IsKeyPhaseChanged` + `CanKeyUpdate`); if so, it triggers `TriggerReadKeyUpdate` and then `RetryPayloadDecrypt`. This is the implementation path of passive Key Update per RFC 9001 §6.

### 6.3 FrameProcessor::HandleFrames

[`src/quic/connection/connection_frame_processor.cpp`](../../../src/quic/connection/connection_frame_processor.cpp). A `switch (frame->GetType())` dispatches each frame type to the matching module. The common mappings:

| Frame | Receiver |
| :--- | :--- |
| `STREAM`, `RESET_STREAM`, `STOP_SENDING`, `MAX_STREAM_DATA` | `StreamManager` |
| `MAX_DATA`, `MAX_STREAMS_*`, `DATA_BLOCKED`, `STREAMS_BLOCKED` | `BaseConnection`'s own flow controller |
| `ACK` | `SendManager::OnPacketAck` |
| `CRYPTO` | `ConnectionCrypto` → fed to BoringSSL |
| `NEW_CONNECTION_ID`, `RETIRE_CONNECTION_ID` | `ConnectionIDCoordinator` |
| `PATH_CHALLENGE`, `PATH_RESPONSE` | `PathManager` |
| `NEW_TOKEN` | `SessionCache` (client side) |
| `CONNECTION_CLOSE`, `CONNECTION_CLOSE_APP` | `state_machine_` + `connection_closer_` |
| `PING`, `PADDING` | Counted only as "ack-eliciting / non-ack-eliciting" by `RecvControl` |
| `HANDSHAKE_DONE` | `state_machine_.OnHandshakeDoneFrameReceived` (client only) |

Every branch may update `RecvControl`'s "send an immediate ACK?" decision, and may add the current connection to the worker's `active_send_connections_` so the next `ProcessSend` handles the reply.

---

## 7. Key Invariants

A few invariants that span the whole chain, for easier debugging:

1. **A NetPacket's buffer may only be released after `OnPackets` returns**. Every `IPacket` in the `packets_` array holds a `SharedBufferSpan` pointing into the buffer; releasing early would make decryption read recycled memory.
2. **The CID routing tables (the master's `cid_worker_map_` + the worker's `conn_map_`) are only modified on their own EventLoop thread**. Adding a CID goes through the `HandleAddConnectionId` callback; teardown goes through `HandleRetireConnectionId` and `HandleConnectionClose`.
3. **A packet must succeed at `DecodeWithCrypto` before `OnPacketRecv`**. Failed packets do not enter the ACK tracker, avoiding erroneous ACKs.
4. **The idle timer is reset after every datagram that decrypts successfully**; multiple coalesced packets in one datagram only need one reset (at the implementation level, done uniformly at the end of `OnPackets`).
5. **`HandlePacketsInClosingState` does not trigger an ACK for received packets** — once in Closing, the local end only cares whether the peer also sent a CLOSE, so as to enter Draining.

---

## 8. Related Documents

- [`design/ownership_and_memory.md`](ownership_and_memory.md) — explains ownership conventions such as why `BaseConnection` / `IStream` use `weak_ptr<IEventLoop>`.
- [`design/handshake_state_machine.md`](handshake_state_machine.md) — state transitions from receiving the Initial packet to handshake completion.
- [`design/loss_recovery.md`](loss_recovery.md) — how ACK processing drives retransmission (RFC 9002).
- [`tutorial/quic_api_guide.md`](../tutorial/quic_api_guide.md) — the receive-data API from the user's perspective.
- [`reference/qlog_event_coverage.md`](../reference/qlog_event_coverage.md) — field coverage of events like `packet_received` / `packet_dropped`.

---

## 9. Related RFCs

- [RFC 9000 §12.2](https://www.rfc-editor.org/rfc/rfc9000.html#name-coalescing-packets) — Coalescing Packets
- [RFC 9000 §14.1](https://www.rfc-editor.org/rfc/rfc9000.html#name-initial-datagram-size) — Initial Datagram Size
- [RFC 9000 §17.2.1](https://www.rfc-editor.org/rfc/rfc9000.html#name-version-negotiation-packet) — Version Negotiation Packet
- [RFC 9000 §10.1](https://www.rfc-editor.org/rfc/rfc9000.html#name-idle-timeout) — Idle Timeout
- [RFC 9001 §5.4](https://www.rfc-editor.org/rfc/rfc9001.html#name-header-protection) — Header Protection
- [RFC 9001 §6](https://www.rfc-editor.org/rfc/rfc9001.html#name-key-update) — Key Update
- [RFC 9369](https://www.rfc-editor.org/rfc/rfc9369.html) — QUIC Version 2 (the v1/v2 wire-type mapping is handled inside `packet_decode.cpp`)
