# quicX Ownership & Memory Management

This document describes the **object lifetime, ownership hierarchy, and reference
rules** that the quicX codebase follows. After reading it you should be able to:

- decide whether a new member should be `shared_ptr` / `weak_ptr` /
  `unique_ptr` / raw pointer;
- avoid reference cycles and dangling references when adding callbacks,
  timers, or event subscriptions;
- spot ownership violations quickly during code review.

---

## 1. Guiding Principle: Exclusive Ownership + Observer Reference

quicX uses an **"exclusive ownership + observer reference"** model. There are
only two rules:

1. **Every object has exactly one owner.** The owner holds the lifetime via
   `shared_ptr` (when it must be shared) or `unique_ptr` (when exclusive).
2. **All other users observe via `weak_ptr`.** Call `lock()` on use; give up
   cleanly if it returns empty.

This supersedes the previous "everyone holds a `shared_ptr`, everyone points
at each other" style, which produced:

- **Cycle leaks** — `A->shared_ptr<B>`, `B->shared_ptr<A>`; neither can be
  released.
- **Callbacks that outlive their object** —
  `AddTimer([self=shared_from_this()]{...})` keeps a "closed" object alive
  until the timer fires.
- **Fragile destruction order** — multiple `shared_ptr` destruction order is
  hard to predict, leading to use-after-free.

---

## 2. Ownership Hierarchy

quicX lifetimes form a **directed, acyclic ownership tree**. Top-down:

```
 QuicClient / QuicServer   ← held by the user
  │  owns(shared_ptr)
  ├── common::IEventLoop            (I/O + Timer hub)
  ├── ISender                       (UDP sender)
  └── IWorker                       (connection container: Master/Worker)
        │  owns(shared_ptr in conn_map_)
        └── IConnection (BaseConnection)
              │  owns(unique_ptr)
              ├── TimerCoordinator
              ├── ConnectionIDCoordinator
              ├── PathManager
              ├── StreamManager  ── owns(shared_ptr) ─→  IStream
              ├── ConnectionCloser
              ├── FrameProcessor
              ├── EncryptionLevelScheduler
              └── PacketBuilder
```

HTTP/3 follows the same structure:

```
 Http3Client / Http3Server
  │  owns(shared_ptr)
  └── IQuicClient / IQuicServer       (reuses the QUIC layer)
  └── Http3Connection
        │  owns(shared_ptr)
        └── ReqRespBaseStream (RequestStream / ResponseStream / PushStream)
              │  owns(shared_ptr)
              └── IQuicBidirectionStream   (QUIC stream, owned by StreamManager)
```

### 2.1 Owners at a glance

| Object | Owner | Held as |
|---|---|---|
| `IEventLoop` | `QuicClient` / `QuicServer` | `shared_ptr` |
| `ISender` (UDP) | `QuicClient` / `QuicServer` | `shared_ptr` |
| `IWorker` | `Master` / `QuicClient` | `shared_ptr` |
| `BaseConnection` | `Worker::conn_map_` | `shared_ptr<IConnection>` |
| `IStream` (QUIC) | `StreamManager` (within `BaseConnection`) | `shared_ptr` |
| `ReqRespBaseStream` (H3) | `Http3Connection` | `shared_ptr` |
| `Coordinator` / `Manager` | `BaseConnection` | `unique_ptr` |

### 2.2 Observers at a glance

| Holder → observed object | Field form | Notes |
|---|---|---|
| `BaseConnection::event_loop_` → `IEventLoop` | `std::weak_ptr<IEventLoop>` | Owner is `QuicClient` |
| `IStream::event_loop_` → `IEventLoop` | `std::weak_ptr<IEventLoop>` | same |
| `Worker::event_loop_` → `IEventLoop` | `std::weak_ptr<IEventLoop>` | same |
| `ResponseStream::http_processor_` → `IHttpProcessor` | `std::weak_ptr<IHttpProcessor>` | User-level handler, owned at the top |
| `http3::IStream` callbacks → `http3::IConnection` | `std::weak_ptr<IConnection>` (lambda capture) | See §3.5 — streams must never hold a strong back-reference to the connection |

Rule of thumb: **upward / sideways references use `weak_ptr`; only downward
(owner → ownee) references may be `shared_ptr`.**

---

## 3. API Conventions

### 3.1 Callbacks and `shared_from_this()`

Classes that need to access themselves inside async callbacks (typically
`BaseConnection`, `ReqRespBaseStream`) must inherit
`std::enable_shared_from_this<T>`:

```cpp
class BaseConnection:
    public IConnection,
    public std::enable_shared_from_this<BaseConnection> { ... };
```

Inside **short-lived** callbacks (one-shot timers, OnX events), use
`weak_from_this() + lock()`:

```cpp
// Preferred: does not extend lifetime; silently skip if gone.
timer_coordinator_->AddTimer([weak = weak_from_this()] {
    auto self = weak.lock();
    if (!self) return;
    self->OnIdleTimeout();
}, timeout_ms);
```

**Exception — Self-pinning**: when a piece of code must run atomically (e.g.
`OnStateToClosing` invokes the user close callback, which may drop the last
`shared_ptr`), pin with a local `shared_from_this()`:

```cpp
void BaseConnection::OnStateToClosing() {
    auto self = shared_from_this();   // pin: we won't die inside this function
    if (callbacks_.close_cb) callbacks_.close_cb(...);
    connection_closer_->Start();
}
```

### 3.2 `EventLoop::AddFixedProcess`

Callbacks that run on every loop iteration **must** use the weak-ptr-guarded
overload:

```cpp
// Correct: auto-disabled and removed when owner expires
event_loop_->AddFixedProcess(
    weak_from_this(),          // std::weak_ptr<void>
    [weak = weak_from_this()] {
        auto self = weak.lock();
        if (!self) return;
        self->DoPeriodicWork();
    });

// Deprecated (causes a cycle leak under ASan):
event_loop_->AddFixedProcess([self = shared_from_this()] { ... });
```

Without an owner guard the fixed process keeps a `shared_ptr` inside
`EventLoop::fixed_processes_` forever, closing a reference cycle; only a
manual `ClearFixedProcesses()` breaks it. The guarded overload locks the
owner every iteration and drops the entry as soon as it's gone.

### 3.3 Timer lifetime

`EventLoop` stores every registered timer callback in `timers_`. If the
closure captures a `shared_ptr<BaseConnection>`:

- close initiated while the timer is still pending → the closure keeps the
  `shared_ptr` alive until the timer fires;
- if the master loop is stopped first → the timer never fires →
  **~120 KB leaked per connection**.

Therefore:

1. **Prefer capturing `weak_from_this()`.**
2. During shutdown, `QuicClient::Destroy` / `QuicServer::Destroy` must call
   `event_loop_->ClearAllTimers()` *after* stopping the loop, so that every
   outstanding closure is released deterministically.

### 3.4 Connection pinning in Worker

`Worker::HandlePacket` looks up an `IConnection` in `conn_map_`, **copies the
`shared_ptr` into a local first**, then calls `OnPackets()`:

```cpp
// Correct
auto conn_sp = conn_map_[cid_hash];      // copy the shared_ptr
if (!conn_sp) return;
conn_sp->OnPackets(now, packets);        // safe even if the callback erases us from conn_map_
```

Reason: `OnPackets` may fire a `CONNECTION_CLOSE` callback → user code →
`Destroy()` → erases self from `conn_map_`. Accessing the map entry directly
like `conn_map_[...]->OnPackets()` triggers a use-after-free under ASan.

### 3.5 HTTP/3 Connection: Two-Phase Initialization + Weak-Self Lambda

`http3::IConnection` and its subclasses `ClientConnection` / `ServerConnection`
create their control / QPACK encoder / QPACK decoder streams during setup and
bind several callbacks to themselves (`HandleError` / `HandleSettings` /
`HandlePushPromise`, ...). Two traps you must never step into:

1. **`weak_from_this()` is empty inside the constructor.** The internal
   weak_ptr of `std::enable_shared_from_this<T>` is only populated once
   `std::make_shared<T>` completes; calling `weak_from_this()` /
   `shared_from_this()` inside the constructor body fails (the former yields
   a forever-expired weak, the latter throws `bad_weak_ptr`).
2. **Callbacks that `std::bind(&Xxx::HandleYyy, this, ...)` or capture a raw
   `this`** will invoke virtual functions on a destroyed object if a stream
   callback fires after the connection is destructed, hitting
   `__cxa_pure_virtual` → SIGABRT.

`http3::IConnection` therefore uses **two-phase initialization**:

```cpp
// 1) Constructor: only initialization that does not depend on shared_from_this
IConnection(unique_id, quic_conn, error_handler);   // just assign members

// 2) Call Init() right after factory creation; shared_from_this() is now usable
auto conn = std::make_shared<ServerConnection>(...);
conn->Init();   // creates control / QPACK streams and binds weak-self callbacks
```

#### The three standard callback patterns

**(a) Base-class public callbacks** — via the factories provided by
`IConnection`:

```cpp
// if_connection.h
std::function<void(uint64_t, uint32_t)> MakeErrorHandler();
std::function<void(const std::unordered_map<uint16_t, uint64_t>&)> MakeSettingsHandler();

// subclass usage
control_stream_->SetErrorCallback(MakeErrorHandler());
control_stream_->SetSettingsCallback(MakeSettingsHandler());
```

Internally the factories bind a `weak_ptr<IConnection>` and `lock()` on
invocation, giving up cleanly if the connection is already gone.

**(b) Subclass-private callbacks** — inline weak-lambda via `WeakSelfAs<T>()`
at the call site:

```cpp
// connection_client.cpp
auto weak_self = WeakSelfAs<ClientConnection>();
stream->SetPushPromiseCallback([weak_self](uint64_t push_id) {
    if (auto self = weak_self.lock()) self->HandlePushPromise(push_id);
});
```

`WeakSelfAs<T>()` is a protected template on `IConnection`, essentially
`weak_ptr<T>(static_pointer_cast<T>(shared_from_this()))`. Subclass-private
methods thus need neither friends nor pollution of the base interface.

**(c) Stream-state callbacks** — registered centrally inside
`IConnection::Init()`:

```cpp
void IConnection::Init() {
    auto weak_self = weak_from_this();   // usable at this point
    quic_connection_->SetStreamStateCallBack(
        [weak_self](uint64_t stream_id, StreamState state) {
            if (auto self = weak_self.lock()) self->OnStreamState(stream_id, state);
        });
}
```

#### Anti-patterns

```cpp
//  bind raw this: stream callback fires after connection destruction → __cxa_pure_virtual
control_stream_->SetErrorCallback(
    std::bind(&ServerConnection::HandleError, this, _1, _2));

//  capturing shared_from_this(): closes the Connection → streams_ → Stream → cb → Connection cycle
control_stream_->SetErrorCallback(
    [self = shared_from_this()](uint64_t id, uint32_t ec) { self->HandleError(id, ec); });

//  creating streams in the constructor then binding weak: weak_from_this() is empty there
ServerConnection::ServerConnection(...) {
    control_stream_ = std::make_shared<ControlStream>(...);
    control_stream_->SetErrorCallback(MakeErrorHandler());  // the bound weak is forever expired
}
```

The correct pattern: the constructor only stores parameters; stream creation
and callback binding all move into `Init()`.

### 3.6 Sender sharing

`ISender` (the UDP sender) is legitimately held by multiple parties:

- created and truly owned by `QuicClient` / `QuicServer`;
- `Worker::sender_`, `BaseConnection::sender_` are both `shared_ptr` — this
  is a **shared downstream utility** and does not form a cycle.

This is the sole exception to the exclusive model: **pure data sinks**
(sender / logger / metrics) are allowed to be shared because they never
hold any upward object.

---

## 4. Close & Destruction Sequence

The most bug-prone path. Follow the exact order:

```
user calls QuicClient::Destroy()
  │
  ├── 1) for every IWorker: worker->Shutdown()
  │       └── clears conn_map_ / connecting_set_ / active_send_connections_
  │           → drops BaseConnection shared_ptr refcount to zero
  │
  ├── 2) event_loop_->ClearAllTimers()
  │       └── releases every closure still in timers_, including
  │           self-pinned BaseConnection objects
  │
  ├── 3) reset master_event_loop_ / join thread
  │
  └── 4) all shared_ptr members destructed
```

Step 1 triggers the full close sequence per connection:

```
Worker::Shutdown
  → conn.reset()  (refcount --)
    → if last ref → ~BaseConnection()
        → state_machine_ reaches Closed
        → OnStateToClosed()
        → unique_ptr members destructed in reverse declaration order
```

If RFC 9000 Closing / Draining timers are still attached to the loop,
step 2 is the safety net that cleans them up.

---

## 5. Common Pitfalls & Correct Patterns

### Pitfall 1 — Binding self into a fixed process

```cpp
// ❌ Cycle: EventLoop owns shared_ptr<Worker>, Worker owns shared_ptr<EventLoop>
event_loop_->AddFixedProcess([self = shared_from_this()] {
    self->ProcessSend();
});

// ✅
event_loop_->AddFixedProcess(weak_from_this(), [weak = weak_from_this()] {
    auto self = weak.lock();
    if (!self) return;
    self->ProcessSend();
});
```

### Pitfall 2 — Stream strongly referencing Connection

```cpp
// ❌ Stream ↔ Connection bidirectional shared_ptr → neither is freed after close
class Stream {
    std::shared_ptr<BaseConnection> conn_;   // wrong
};

// ✅ Use a callback + EventLoop observer
class IStream {
    std::weak_ptr<common::IEventLoop> event_loop_;
    std::function<void(std::shared_ptr<IStream>)> active_send_cb_;  // injected by Connection
};
```

### Pitfall 3 — Test helper returning a short tuple

```cpp
// ❌ loop dies at return; BaseConnection::event_loop_.lock() returns null
std::tuple<Conn, Conn, Sender, Sender> Make() {
    auto loop = MakeEventLoop();
    ...
    return {client, server, cs, ss};   // loop is gone
}

// ✅ keep the loop alive in the tuple, held by the test scope
std::tuple<Conn, Conn, Sender, Sender, std::shared_ptr<IEventLoop>> Make() {
    ...
    return {client, server, cs, ss, loop};
}
```

### Pitfall 4 — Calling `weak_from_this()` / `shared_from_this()` in a constructor

```cpp
//  the internal weak of enable_shared_from_this is populated only after
//  make_shared completes; here it is forever expired and never locks
class Http3Conn : public std::enable_shared_from_this<Http3Conn> {
public:
    Http3Conn() {
        auto weak = weak_from_this();
        stream_->SetCb([weak]{ if (auto s = weak.lock()) s->Do(); });
    }
};

//  two-phase init: the constructor only assigns, Init() binds the callbacks
class Http3Conn : public std::enable_shared_from_this<Http3Conn> {
public:
    Http3Conn(...) { /* just store parameters */ }
    void Init() {
        auto weak = weak_from_this();   // usable now
        stream_ = std::make_shared<Stream>(...);
        stream_->SetCb([weak]{ if (auto s = weak.lock()) s->Do(); });
    }
};

// caller
auto c = std::make_shared<Http3Conn>(...);
c->Init();   // must immediately follow make_shared
```

### Pitfall 5 — `std::bind(&X::M, this, ...)` in stream callbacks

```cpp
//  a stream callback may fire after the connection is destructed
//  → calls a pure virtual function → __cxa_pure_virtual
control_stream_->SetErrorCallback(
    std::bind(&ServerConnection::HandleError, this, _1, _2));

//  base-class public callbacks go through the factories
control_stream_->SetErrorCallback(MakeErrorHandler());

//  subclass-private callbacks use WeakSelfAs<T>() inline lambdas
auto weak_self = WeakSelfAs<ServerConnection>();
push_stream->SetCallback([weak_self](uint64_t id) {
    if (auto self = weak_self.lock()) self->HandlePush(id);
});
```

### Pitfall 6 — User callback fired from `OnStateToClosing`

```cpp
// ❌ If the user callback drops the last shared_ptr, the rest of this
//    function touches a destroyed `this` — UAF.
void OnStateToClosing() {
    if (close_cb_) close_cb_(...);
    connection_closer_->Start();   // this may already be gone
}

// ✅ self-pin
void OnStateToClosing() {
    auto self = shared_from_this();
    if (close_cb_) close_cb_(...);
    connection_closer_->Start();
}
```

---

## 6. Verification Toolchain

Continuously enabled in CI:

- **AddressSanitizer (ASan)** — `build-asan/`. After every core benchmark
  and unit test run we verify zero reference cycles, zero use-after-free,
  zero heap-use-after-free.
- **LeakSanitizer (LSan)** — `test/perf/lsan_suppressions.txt` only
  suppresses `BlockMemoryPool` global / thread-local pools (those are
  intentional global-lifetime allocations, not cycles).
- **Unit tests** — `test/unit_test/quic/connection/` includes
  `connection_close_test.cpp`, `connection_base_close_behavior_test.cpp`,
  `path_migration_test.cpp` covering close / migration lifetime scenarios.

Before adding any new `shared_ptr` member, ask:

> Does this field actually express *"I own it"*? If not — use `weak_ptr`.

---

## 7. Glossary

| Term | Meaning |
|---|---|
| Owner | The unique party responsible for an object's lifetime (`shared_ptr` / `unique_ptr` holder) |
| Observer | A user that needs access but no lifetime responsibility (`weak_ptr` holder) |
| Self-pinning | `auto self = shared_from_this();` at the entry of a critical section, extending self-lifetime to function end |
| Guarded Fixed Process | `AddFixedProcess(weak_ptr<void> owner, cb)`; auto-skipped when owner expires |
| Connection Pinning | Worker copies `shared_ptr<IConnection>` into a local before dispatch, so erasure during dispatch is safe |
| Two-phase Init | The constructor only assigns members; `Init()` is called after `make_shared` and is where `weak_from_this()` / `shared_from_this()` may bind callbacks |
| Weak-self Lambda | `auto weak_self = weak_from_this();` + `[weak_self](...){ if (auto s = weak_self.lock()) s->Do(); }` — the standard pattern for binding stream callbacks in the HTTP/3 connection layer |

---

*Matches the completed Exclusive Ownership refactor + HTTP/3 connection two-phase-init fixes.*
