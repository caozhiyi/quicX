# quicX 引用关系与内存管理

本文档系统性介绍 quicX 代码库中**对象生命周期、所有权层级、引用关系**的设计约定。理解本文档后，你能够：

- 判断一个成员应当使用 `shared_ptr` / `weak_ptr` / `unique_ptr` / 裸指针；
- 在新增回调、定时器、事件订阅时，避免引入循环引用与悬空引用；
- 在 Review 代码时，快速识别违反所有权约定的写法。

---

## 1. 指导原则：Exclusive Ownership + Observer Reference

quicX 采用「**独占所有权 + 观察者引用**」模型。核心规则只有两条：

1. **每个对象有且仅有一个"所有者"**。所有者用 `shared_ptr`（若需共享）或 `unique_ptr`（若独占）持有其生命周期。
2. **所有其他"使用者"通过 `weak_ptr` 观察**。使用时 `lock()`，若为空则放弃本次操作。

这个模型替代了早期版本中弥漫的「人人 `shared_ptr`，互相持有」的写法，后者会导致：

- **循环引用泄漏**：`A->shared_ptr<B>`，`B->shared_ptr<A>`，两个对象都无法释放；
- **回调延长生命周期**：`AddTimer([self=shared_from_this()]{...})` 让已经"关闭"的对象无法销毁；
- **析构次序依赖**：多个 `shared_ptr` 的析构次序难以预测，容易 use-after-free。

---

## 2. 所有权层级全景图

quicX 的对象生命周期形成一棵**有向无环的所有权树**。自顶向下：

```
 QuicClient / QuicServer   ← 用户持有
  │  owns(shared_ptr)
  ├── common::IEventLoop            （I/O + Timer 枢纽）
  ├── ISender                       （UDP 发送器）
  └── IWorker                       （连接容器，包含 Master/Worker）
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

HTTP/3 层同样遵循这一结构：

```
 Http3Client / Http3Server
  │  owns(shared_ptr)
  └── IQuicClient / IQuicServer      （复用 QUIC 层）
  └── Http3Connection
        │  owns(shared_ptr)
        └── ReqRespBaseStream (RequestStream / ResponseStream / PushStream)
              │  owns(shared_ptr)
              └── IQuicBidirectionStream  （QUIC 层 Stream，由 StreamManager 所有）
```

### 2.1 所有者（Owner）一览

| 对象 | 所有者 | 持有方式 |
|---|---|---|
| `IEventLoop` | `QuicClient` / `QuicServer` | `shared_ptr` |
| `ISender` (UDP) | `QuicClient` / `QuicServer` | `shared_ptr` |
| `IWorker` | `Master` / `QuicClient` | `shared_ptr` |
| `BaseConnection` | `Worker::conn_map_` | `shared_ptr<IConnection>` |
| `IStream` (QUIC) | `StreamManager`（隶属 `BaseConnection`） | `shared_ptr` |
| `ReqRespBaseStream` (H3) | `Http3Connection` | `shared_ptr` |
| 各 `Coordinator` / `Manager` | `BaseConnection` | `unique_ptr` |

### 2.2 观察者（Observer）一览

| 持有者 → 被观察对象 | 字段形式 | 说明 |
|---|---|---|
| `BaseConnection::event_loop_` → `IEventLoop` | `std::weak_ptr<IEventLoop>` | `QuicClient` 是真实所有者 |
| `IStream::event_loop_` → `IEventLoop` | `std::weak_ptr<IEventLoop>` | 同上 |
| `Worker::event_loop_` → `IEventLoop` | `std::weak_ptr<IEventLoop>` | 同上 |
| `ResponseStream::http_processor_` → `IHttpProcessor` | `std::weak_ptr<IHttpProcessor>` | 应用层处理器，服务器顶层持有 |

经验法则：**向上/向侧的引用一律使用 `weak_ptr`；只有向下的（所有者→被所有者）才允许 `shared_ptr`。**

---

## 3. 关键 API 规范

### 3.1 回调与 `shared_from_this()`

对需要在异步回调中访问自身的类（典型：`BaseConnection`, `ReqRespBaseStream`），必须继承 `std::enable_shared_from_this<T>`。

```cpp
class BaseConnection:
    public IConnection,
    public std::enable_shared_from_this<BaseConnection> { ... };
```

在**短生命周期**回调（如一次性定时器、OnX 事件处理）中使用 `weak_from_this() + lock()`：

```cpp
// 推荐写法：不延长生命周期，连接已销毁则安静放弃
timer_coordinator_->AddTimer([weak = weak_from_this()] {
    auto self = weak.lock();
    if (!self) return;
    self->OnIdleTimeout();
}, timeout_ms);
```

**例外 —— Self-pinning**：当一段代码必须原子性地完成（如 `OnStateToClosing` 会触发用户回调，用户回调可能释放最后一个 shared_ptr），才使用局部 `shared_from_this()` 钉住自己：

```cpp
void BaseConnection::OnStateToClosing() {
    auto self = shared_from_this();   // 钉住，确保本函数内不会被析构
    // 触发用户 close callback —— 用户可能 release 最后一个强引用
    if (callbacks_.close_cb) callbacks_.close_cb(...);
    // 继续做清理工作
    connection_closer_->Start();
}
```

### 3.2 `EventLoop::AddFixedProcess`

每事件循环迭代都会执行的 callback，**必须**使用 weak_ptr 守卫重载：

```cpp
// 正确：owner 过期后 callback 自动失效并被移除
event_loop_->AddFixedProcess(
    weak_from_this(),          // std::weak_ptr<void>
    [weak = weak_from_this()] {
        auto self = weak.lock();
        if (!self) return;
        self->DoPeriodicWork();
    });

// 已废弃（且会在 ASan 下产生环引用泄漏）：
event_loop_->AddFixedProcess([self = shared_from_this()] { ... });
```

原理：无 owner 守卫的 fixed process 会在 `EventLoop::fixed_processes_` 里长期持有 `shared_ptr`，与对象形成环 —— 只有手动 `ClearFixedProcesses()` 才能断开。有 owner 守卫的版本，每次 iteration 前 `owner.lock()`，为空即跳过并从列表移除。

### 3.3 Timer 的生命周期

`EventLoop` 内部 `timers_` 持有所有已登记 timer 的 callback 闭包。若闭包捕获 `shared_ptr<BaseConnection>`，则：

- 对象关闭时 timer 未触发 → 闭包持有的 shared_ptr 直到 timer 过期才释放
- 若 master loop 先 stop → timer 永不触发 → **泄漏 ~120KB/连接**

因此：

1. **优先用 `weak_from_this()` 捕获**。
2. 关闭路径上，`QuicClient::Destroy` / `QuicServer::Destroy` 必须在 loop 停止后调用 `event_loop_->ClearAllTimers()`，把所有闭包显式释放。

### 3.4 Worker 分发数据包时的 Connection Pinning

`Worker::HandlePacket` 从 `conn_map_` 查到一个 `IConnection`，立刻拷贝一份 `shared_ptr` 到局部，再调用 `OnPackets()`：

```cpp
// 正确
auto conn_sp = conn_map_[cid_hash];      // 拷贝 shared_ptr
if (!conn_sp) return;
conn_sp->OnPackets(now, packets);        // 即使回调中把自己从 conn_map_ 移除，这里仍安全
```

原因：`OnPackets` 内部可能触发 `CONNECTION_CLOSE` 回调 → 用户 callback → `Destroy()` → 从 `conn_map_` 擦除自己。若直接用 `conn_map_[...]->OnPackets()` 这种"走引用"写法，ASan 会检出 use-after-free。

### 3.5 Sender 的共享

`ISender`（UDP 发送器）被多方持有是正常的：

- `QuicClient`/`QuicServer` 创建并作为真实所有者；
- `Worker::sender_`、`BaseConnection::sender_` 均为 `shared_ptr` —— 这是"**共享下游工具**"，不会构成循环。

这是独占模型的唯一例外：**纯数据出口**（sender/logger/metrics）允许多个 shared_ptr 共享，因为它们本身不持有任何上层对象。

---

## 4. 关闭与析构流程

这是最容易出 bug 的路径，必须一字不差地按以下次序执行：

```
用户调用 QuicClient::Destroy()
  │
  ├── 1) 对每个 IWorker: worker->Shutdown()
  │       └── 清空 conn_map_ / connecting_set_ / active_send_connections_
  │           → 让 BaseConnection 的 shared_ptr 引用计数归零
  │
  ├── 2) event_loop_->ClearAllTimers()
  │       └── 释放 timers_ 中所有闭包 —— 包括 self-pinned 的 BaseConnection
  │
  ├── 3) master_event_loop_ reset / 线程 join
  │
  └── 4) 所有 shared_ptr 成员析构
```

其中第 1 步会触发每个 `BaseConnection` 的完整关闭序列：

```
Worker::Shutdown
  → conn.reset()  （引用计数 --）
    → 若是最后一个引用 → ~BaseConnection()
        → state_machine_ 走到 Closed
        → OnStateToClosed()
        → 各 unique_ptr 成员按反向声明顺序析构
```

RFC 9000 的 Closing/Draining timer 如果还挂在 EventLoop，必须由第 2 步兜底清理。

---

## 5. 常见陷阱与正确写法对照

### 陷阱 1：把自己绑进 fixed process

```cpp
//  泄漏：EventLoop 持 shared_ptr<Worker>，Worker 持 shared_ptr<EventLoop>（早期设计）→ 环
event_loop_->AddFixedProcess([self = shared_from_this()] {
    self->ProcessSend();
});

//  正确
event_loop_->AddFixedProcess(weak_from_this(), [weak = weak_from_this()] {
    auto self = weak.lock();
    if (!self) return;
    self->ProcessSend();
});
```

### 陷阱 2：Stream 强引用 Connection

```cpp
//  Stream 与 Connection 双向 shared_ptr → 连接关闭后均无法释放
class Stream {
    std::shared_ptr<BaseConnection> conn_;   // 错
};

//  改为回调 + EventLoop 观察
class IStream {
    std::weak_ptr<common::IEventLoop> event_loop_;
    std::function<void(std::shared_ptr<IStream>)> active_send_cb_;  // Connection 传入
};
```

### 陷阱 3：测试辅助函数返回短 tuple

```cpp
//  event_loop 出函数即析构，BaseConnection::event_loop_.lock() 返回 nullptr
std::tuple<Conn, Conn, Sender, Sender> Make() {
    auto loop = MakeEventLoop();
    ...
    return {client, server, cs, ss};   // loop 死了
}

//  把 event_loop 也塞回 tuple，由测试作用域持有
std::tuple<Conn, Conn, Sender, Sender, std::shared_ptr<IEventLoop>> Make() {
    ...
    return {client, server, cs, ss, loop};
}
```

### 陷阱 4：`OnStateToClosing` 中触发用户回调

```cpp
//  若用户回调释放最后一个 shared_ptr，本函数后续代码访问 this 即为 UAF
void OnStateToClosing() {
    if (close_cb_) close_cb_(...);
    connection_closer_->Start();   // this 可能已析构
}

//  self-pin
void OnStateToClosing() {
    auto self = shared_from_this();
    if (close_cb_) close_cb_(...);
    connection_closer_->Start();   // 安全
}
```

---

## 6. 验证工具

本仓库在 CI 中长期开启以下检查：

- **AddressSanitizer (ASan)**：`build-asan/`，每次核心 benchmark / 单元测试 run 后校验零循环引用、零 use-after-free、零 heap-use-after-free。
- **LeakSanitizer (LSan)**：`test/perf/lsan_suppressions.txt` 仅过滤 `BlockMemoryPool` 的全局/线程本地池（那是正常的全局生命周期，不是环引用）。
- **单元测试**：`test/unit_test/quic/connection/` 下专门有 `connection_close_test.cpp`、`connection_base_close_behavior_test.cpp`、`path_migration_test.cpp` 覆盖关闭 / 迁移场景下的生命周期。

任何新增成员引入 `shared_ptr` 时，请先自问：

> 这个字段是在表达"我是它的所有者"吗？如果不是 —— 用 `weak_ptr`。

---

## 7. 附：术语速查

| 术语 | 含义 |
|---|---|
| Owner | 对对象生命周期负责的唯一角色（`shared_ptr`/`unique_ptr` 持有者） |
| Observer | 仅需访问、不对生命周期负责的引用者（`weak_ptr` 持有者） |
| Self-pinning | 在一段关键代码的入口 `auto self = shared_from_this();` 延长自身生命周期直到函数结束 |
| Guarded Fixed Process | `AddFixedProcess(weak_ptr<void> owner, cb)`，owner 过期自动跳过 |
| Connection Pinning | Worker 分发包前先复制一份 `shared_ptr<IConnection>` 到局部，防止分发途中被擦除 |

---

*最后更新：2026-05，对应 Exclusive Ownership 重构完成。*
