# QuicX 项目上下文 - Claude Code 参考文档

> 本文档为 Claude Code AI 助手提供 QuicX 项目的完整上下文信息，包括技术栈、架构设计、实现细节等。
> 最后更新：2025-11-30

---

## 📋 项目概述

**QuicX** 是一个生产级的 QUIC 和 HTTP/3 网络通信库，使用 C++17 实现。

- **项目类型**: 静态库（libquicx.a, libhttp3.a）
- **主要功能**: 完整的 QUIC（RFC 9000）和 HTTP/3（RFC 9114）协议实现
- **开源协议**: BSD 3-Clause License
- **代码规模**: 198 个 .cpp 文件 + 265 个 .h 头文件
- **当前分支**: dev（主分支：main）

---

## 🛠️ 技术栈

### 编程语言
- **C++17** - 主要实现语言
- 代码标准：严格遵循 C++17 特性

### 核心依赖库

| 库名称 | 版本 | 用途 | 位置 |
|--------|------|------|------|
| **BoringSSL** | - | TLS 1.3 加密、AEAD（AES-GCM, ChaCha20）、HKDF 密钥派生 | `/third/boringssl` |
| **GoogleTest** | v1.17.0 | 单元测试框架（140+ 测试文件） | `/third/googletest` |
| **Google Benchmark** | v1.8.3 | 性能基准测试（14 个基准测试） | - |
| **LibFuzzer** | - | 模糊测试（帧/包解析器） | - |

### 构建系统

**主构建系统：CMake**
- 最低版本：3.16
- C++ 标准：C++17
- 输出目录：
  - 可执行文件：`${PROJECT_BINARY_DIR}/bin`
  - 库文件：`${PROJECT_BINARY_DIR}/lib`

**备选构建系统：Bazel**
- Bzlmod 模块系统
- 30 个 BUILD.bazel 文件

**构建选项：**
```cmake
BUILD_EXAMPLES=ON         # 构建示例（默认 ON）
ENABLE_TESTING=ON         # 单元测试（默认 ON）
ENABLE_BENCHMARKS=ON      # 性能测试（默认 ON）
ENABLE_FUZZING=OFF        # 模糊测试（默认 OFF）
ENABLE_CC_SIMULATOR=ON    # 拥塞控制模拟器（默认 ON）
```

**支持平台：**
- Linux（GCC/Clang）- 使用 epoll
- macOS（Clang）- 使用 kqueue
- Windows（MSVC）- 使用 IOCP

---

## 🏗️ 项目架构

### 主要输出产物

```
libquicx.a    # QUIC 协议核心库（common + quic 模块）
libhttp3.a    # HTTP/3 完整库（common + quic + upgrade + http3 模块）
```

### 目录结构

```
/mnt/d/code/quicX/
├── src/                          # 源代码目录
│   ├── common/                   # 🔹 通用基础设施模块
│   │   ├── alloter/             # 内存分配器（池化分配器、普通分配器）
│   │   ├── buffer/              # 缓冲区管理系统（零拷贝设计）
│   │   ├── decode/              # 数据解码工具
│   │   ├── http/                # HTTP 通用功能（URL 解析）
│   │   ├── lock/                # 并发控制（自旋锁）
│   │   ├── log/                 # 日志系统
│   │   ├── network/             # 🔸 网络抽象层
│   │   │   ├── linux/           # Linux epoll 实现
│   │   │   ├── macos/           # macOS kqueue 实现
│   │   │   └── windows/         # Windows IOCP 实现
│   │   ├── os/                  # 操作系统抽象
│   │   ├── structure/           # 数据结构（链表、线程安全队列）
│   │   ├── thread/              # 线程封装
│   │   ├── timer/               # 定时器实现（TreemapTimer）
│   │   └── util/                # 工具函数
│   │
│   ├── quic/                     # 🔹 QUIC 协议实现（RFC 9000）
│   │   ├── common/              # QUIC 公共组件
│   │   ├── congestion_control/  # 🔸 拥塞控制算法
│   │   │   ├── bbr_v1_congestion_control.cpp  # BBR v1
│   │   │   ├── bbr_v2_congestion_control.cpp  # BBR v2
│   │   │   ├── bbr_v3_congestion_control.cpp  # BBR v3
│   │   │   ├── cubic_congestion_control.cpp   # Cubic
│   │   │   └── reno_congestion_control.cpp    # Reno
│   │   ├── connection/          # 连接管理（~4100 行核心代码）
│   │   ├── crypto/              # 🔸 加密和 TLS 处理
│   │   │   ├── tls/             # TLS 上下文管理
│   │   │   ├── aes_128_gcm_cryptographer.cpp
│   │   │   ├── aes_256_gcm_cryptographer.cpp
│   │   │   ├── chacha20_poly1305_cryptographer.cpp
│   │   │   └── hkdf.cpp         # HKDF 密钥派生
│   │   ├── frame/               # QUIC 帧处理（20+ 种帧类型）
│   │   ├── packet/              # 🔸 数据包编解码
│   │   │   ├── header/          # 包头处理
│   │   │   ├── init_packet.cpp          # Initial 包
│   │   │   ├── handshake_packet.cpp     # Handshake 包
│   │   │   ├── rtt_0_packet.cpp         # 0-RTT 包
│   │   │   ├── rtt_1_packet.cpp         # 1-RTT 包
│   │   │   └── retry_packet.cpp         # Retry 包
│   │   ├── stream/              # 流管理
│   │   ├── udp/                 # UDP 传输层
│   │   └── include/             # 公共 API 头文件
│   │
│   ├── http3/                    # 🔹 HTTP/3 实现（RFC 9114）
│   │   ├── connection/          # HTTP/3 连接管理
│   │   ├── frame/               # HTTP/3 帧（DATA, HEADERS, SETTINGS 等）
│   │   ├── http/                # HTTP 请求/响应处理
│   │   ├── qpack/               # 🔸 QPACK 头部压缩（RFC 9204）
│   │   │   ├── qpack_encoder.h  # 编码器（静态表 + 动态表）
│   │   │   ├── dynamic_table.h  # 动态表管理
│   │   │   └── huffman 编码
│   │   ├── router/              # HTTP 路由系统
│   │   ├── stream/              # HTTP/3 流管理
│   │   └── include/             # 公共 API 头文件
│   │
│   └── upgrade/                  # 🔹 HTTP 协议升级模块
│       ├── core/                # 协议检测和协商
│       ├── handlers/            # 智能处理器（HTTP/1.1、HTTP/2、HTTP/3）
│       ├── http/                # HTTP 处理
│       ├── network/             # TCP socket 封装
│       └── server/              # 升级服务器
│
├── example/                      # 🔹 示例代码（8 个完整示例）
│   ├── hello_world/             # 基础客户端/服务器（7 行核心代码）
│   ├── concurrent_requests/     # 并发请求演示（6x 性能提升）
│   ├── restful_api/             # RESTful API 完整示例（CRUD）
│   ├── server_push/             # HTTP/3 服务器推送
│   ├── streaming_api/           # 流式传输 API（大文件上传/下载）
│   ├── upgrade_h3/              # HTTP 协议升级示例
│   └── quicx_curl/              # 类似 curl 的命令行工具
│
├── unit_test/                    # 🔹 单元测试（140+ 测试文件）
│   ├── common/                  # 通用模块测试
│   ├── http3/                   # HTTP/3 模块测试
│   ├── quic/                    # QUIC 模块测试
│   └── upgrade/                 # 升级模块测试
│
├── test/                         # 🔹 高级测试
│   ├── benchmarks/              # 性能基准测试（14 个）
│   ├── congestion_control/      # 拥塞控制模拟器
│   └── fuzz/                    # 模糊测试
│
└── third/                        # 第三方依赖
    ├── boringssl/               # TLS/加密库
    └── googletest/              # 测试框架
```

### 设计模式

1. **接口-实现分离**
   - 所有公共 API 都以 `I` 开头的接口类定义
   - 示例：`IClient`, `IServer`, `IQuicStream`, `IRequest`, `IResponse`

2. **工厂模式**
   - 通过静态工厂方法创建对象
   - 示例：`IClient::Create()`, `IServer::Create()`

3. **访问者模式**
   - 用于帧和缓冲区遍历
   - 示例：`IFrameVisitor`, `VisitData()`

4. **策略模式**
   - 拥塞控制算法可插拔
   - 示例：BBR、Cubic、Reno 算法切换

5. **观察者模式**
   - 回调机制处理异步事件
   - 示例：连接回调、流回调、数据接收回调

---

## 🧩 核心模块详解

### 1. Common 模块 - 基础设施

#### 内存管理（alloter/）

**PoolAlloter（内存池）：**
```cpp
class PoolAlloter {
    PoolBlock blocks_;              // 预分配的内存块
    std::vector<void*> free_list_;  // 空闲列表

    // 优势：
    // - 减少 malloc/free 调用
    // - 提高内存局部性
    // - 支持固定大小快速分配
}
```

#### 缓冲区系统（buffer/）

**核心组件：**
- `IBuffer` - 统一的缓冲区接口
- `SingleBlockBuffer` - 单块连续缓冲区
- `MultiBlockBuffer` - 多块链式缓冲区
- `BufferChunk` - 底层内存块
- `BufferSpan` - 非拥有视图（类似 std::span）
- `SharedBufferSpan` - 共享所有权视图
- `BufferReadView/WriteView` - 读写视图

**零拷贝优化：**
```cpp
GetSharedReadableSpan()  // 共享内存而非复制
CloneReadable()          // 引用计数共享
VisitData()              // 访问者模式避免拷贝
```

#### 网络抽象层（network/）

**跨平台事件循环：**
- **Linux**: `src/common/network/linux/` - epoll 实现
- **macOS**: `src/common/network/macos/` - kqueue 实现
- **Windows**: `src/common/network/windows/` - IOCP 实现

**统一接口：**
- `IEventLoop` - 事件循环接口
- `ISocket` - Socket 抽象
- `IAddress` - 地址抽象

#### 并发数据结构（structure/）
- `ThreadSafeBlockQueue` - 线程安全阻塞队列
- `ThreadSafeQueue` - 无锁队列
- 双向链表等

#### 定时器（timer/）

**TreemapTimer：**
- 基于红黑树的定时器
- 支持高精度定时任务
- 用于重传超时（RTO）、空闲超时等

---

### 2. QUIC 模块 - RFC 9000 实现

#### 传输参数（QuicTransportParams）

**默认配置：**
```cpp
max_idle_timeout_ms_ = 120000                     // 2 分钟空闲超时
max_udp_payload_size_ = 1472                      // MTU - 28
initial_max_data_ = 10MB                          // 连接级流控窗口
initial_max_stream_data_bidi_local_ = 1MB         // 双向流窗口（本地发起）
initial_max_stream_data_bidi_remote_ = 1MB        // 双向流窗口（远程发起）
initial_max_stream_data_uni_ = 1MB                // 单向流窗口
initial_max_streams_bidi_ = 100                   // 最大双向流数
initial_max_streams_uni_ = 100                    // 最大单向流数
```

#### 连接管理（connection/）

**核心文件：**
- `connection_client.cpp` - 客户端连接（~2100 行）
- `connection_server.cpp` - 服务端连接（~2000 行）

**连接生命周期：**
1. **Initial 阶段** - TLS 1.3 握手开始
2. **Handshake 阶段** - 握手确认
3. **0-RTT 阶段**（可选）- 早期数据传输
4. **1-RTT 阶段** - 应用数据传输
5. **连接迁移** - 支持 IP/端口变更
6. **连接关闭** - 优雅关闭或错误关闭

#### 拥塞控制（congestion_control/）

**支持的算法：**

| 算法 | 文件 | 特点 |
|------|------|------|
| **BBR v1** | `bbr_v1_congestion_control.cpp` | 基于带宽和 RTT 估计 |
| **BBR v2** | `bbr_v2_congestion_control.cpp` | 改进的 BBR |
| **BBR v3** | `bbr_v3_congestion_control.cpp` | 最新 BBR 版本 |
| **Cubic** | `cubic_congestion_control.cpp` | 三次方增长函数 |
| **Reno** | `reno_congestion_control.cpp` | 传统 TCP AIMD |

**BBR 模式：**
- Startup - 快速启动
- Drain - 排空队列
- ProbeBW - 探测带宽
- ProbeRTT - 探测 RTT

**Pacing（发包调速）：**
- `NormalPacer` 实现
- 平滑数据包发送，避免突发流量

#### 流控制

**两级流控：**
1. **连接级流控**：`MAX_DATA` 帧
2. **流级流控**：`MAX_STREAM_DATA` 帧

**阻塞信号：**
- `DATA_BLOCKED` - 连接级阻塞
- `STREAM_DATA_BLOCKED` - 流级阻塞

#### 数据包类型（packet/）

| 类型 | 文件 | 用途 |
|------|------|------|
| **Initial** | `init_packet.cpp` | 初始握手包 |
| **Handshake** | `handshake_packet.cpp` | 握手确认包 |
| **0-RTT** | `rtt_0_packet.cpp` | 早期数据包 |
| **1-RTT** | `rtt_1_packet.cpp` | 应用数据包（短头部） |
| **Retry** | `retry_packet.cpp` | 重试包 |
| **Version Negotiation** | - | 版本协商包 |

#### QUIC 帧类型（frame/）

**20+ 种帧类型：**
- `CRYPTO` - 加密握手数据
- `STREAM` - 流数据
- `ACK` - 确认帧
- `PING/PONG` - 心跳
- `RESET_STREAM` - 流重置
- `STOP_SENDING` - 停止发送
- `CONNECTION_CLOSE` - 连接关闭
- `MAX_DATA` - 流控更新（连接级）
- `MAX_STREAM_DATA` - 流控更新（流级）
- `MAX_STREAMS` - 流数量限制
- `DATA_BLOCKED` - 数据阻塞
- `STREAM_DATA_BLOCKED` - 流数据阻塞
- `STREAMS_BLOCKED` - 流数量阻塞
- `NEW_CONNECTION_ID` - 新连接 ID
- `RETIRE_CONNECTION_ID` - 废弃连接 ID
- `PATH_CHALLENGE/RESPONSE` - 路径验证
- `NEW_TOKEN` - 新令牌

#### 加密实现（crypto/）

**AEAD 算法：**
```cpp
AES-128-GCM              // aes_128_gcm_cryptographer.cpp
AES-256-GCM              // aes_256_gcm_cryptographer.cpp
ChaCha20-Poly1305        // chacha20_poly1305_cryptographer.cpp
```

**密钥派生：**
- `hkdf.cpp` - HKDF（基于 HMAC 的密钥派生函数）
- 基于 BoringSSL 实现

**包保护：**
- 头部保护（Header Protection）
- 负载加密（Payload Encryption）

---

### 3. HTTP/3 模块 - RFC 9114 实现

#### QPACK 头部压缩（qpack/ - RFC 9204）

**核心组件：**
```cpp
class QpackEncoder {
    DynamicTable dynamic_table_;       // 动态表
    static StaticTable static_table_;  // 静态表（61 个预定义条目）

    // Huffman 编码支持
    // 高效字符串压缩

    // 编码器指令流（Encoder Stream）
    - Insert entries
    - Set capacity
    - Duplicate

    // 解码器反馈流（Decoder Stream）
    - Section Ack
    - Stream Cancel
    - Insert Count Increment
}
```

**静态表示例：**
```
:authority
:path /
:method GET
:method POST
:status 200
content-type application/json
...
```

#### HTTP/3 流类型

| 流类型 | 方向 | 用途 |
|--------|------|------|
| **请求流** | 双向 | HTTP 请求/响应 |
| **控制流** | 单向 | SETTINGS, GOAWAY 等 |
| **QPACK 编码器流** | 单向 | 编码器指令 |
| **QPACK 解码器流** | 单向 | 解码器反馈 |
| **推送流** | 单向 | 服务器推送 |

#### HTTP/3 帧（frame/）

| 帧类型 | 用途 |
|--------|------|
| `DATA` | 响应体数据 |
| `HEADERS` | 压缩的 HTTP 头部 |
| `SETTINGS` | 配置参数交换 |
| `PUSH_PROMISE` | 服务器推送承诺 |
| `CANCEL_PUSH` | 取消推送 |
| `GOAWAY` | 优雅关闭连接 |
| `MAX_PUSH_ID` | 最大推送 ID |

#### HTTP/3 配置（Http3Settings）

```cpp
struct Http3Settings {
    uint64_t max_header_list_size = 100;          // 最大头部列表大小
    uint64_t enable_push = 0;                     // 推送开关（0=禁用, 1=启用）
    uint64_t max_concurrent_streams = 100;        // 最大并发流
    uint64_t max_frame_size = 16384;              // 最大帧大小
    uint64_t qpack_max_table_capacity = 0;        // QPACK 动态表容量
    uint64_t qpack_blocked_streams = 0;           // QPACK 阻塞流
};
```

#### 路由系统（router/）

**路由特性：**
- **路径参数**：`/users/:id`, `/posts/:post_id/comments/:comment_id`
- **通配符**：`/static/*`
- **HTTP 方法**：GET, POST, PUT, DELETE, HEAD, OPTIONS, PATCH, TRACE, CONNECT

**中间件机制：**
```cpp
enum class MiddlewarePosition {
    kBefore,  // 处理器前执行
    kAfter    // 处理器后执行
};

server->AddMiddleware(MiddlewarePosition::kBefore,
    [](req, resp) { /* 日志、认证等 */ });
```

**路由示例：**
```cpp
server->AddHandler(HttpMethod::kGet, "/users/:id",
    [](auto req, auto resp) {
        std::string id = req->GetPathParam("id");
        // 处理逻辑
    });
```

#### 请求处理模式

**1. 完整模式（Complete Mode）：**
```cpp
server->AddHandler(HttpMethod::kPost, "/api/upload",
    [](std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) {
        // 整个请求体已缓冲完成
        std::string body = req->GetBody();
        process(body);
        resp->SetBody("OK");
        resp->SetStatusCode(200);
    });
```

**2. 流式模式（Streaming Mode）：**
```cpp
class FileUploadHandler : public IAsyncServerHandler {
    void OnHeaders(std::shared_ptr<IRequest> req) override {
        // 收到头部时调用
        file_ = fopen("upload.bin", "wb");
    }

    void OnBodyChunk(const uint8_t* data, size_t len, bool is_last) override {
        // 分块接收数据
        fwrite(data, 1, len, file_);
        if (is_last) {
            fclose(file_);
        }
    }
};
```

#### 客户端 API

**简单请求：**
```cpp
auto client = IClient::Create();
client->Init(config);

client->DoRequest(url, HttpMethod::kGet, request,
    [](std::shared_ptr<IResponse> resp, uint32_t error) {
        if (error == 0) {
            std::cout << "Status: " << resp->GetStatusCode() << std::endl;
            std::string body = resp->GetBodyAsString();
        }
    });
```

**流式上传/下载：**
```cpp
class StreamingHandler : public IAsyncClientHandler {
    void OnHeaders(std::shared_ptr<IResponse> resp) override {
        // 接收到响应头
    }

    void OnBodyChunk(const uint8_t* data, size_t len, bool is_last) override {
        // 分块接收响应体
    }
};

client->DoRequest(url, method, request,
    std::make_shared<StreamingHandler>());
```

#### 服务器推送

**服务端：**
```cpp
server->AddHandler(HttpMethod::kGet, "/page",
    [](auto req, auto resp) {
        // 推送 CSS 资源
        auto push_req = IPushRequest::Create();
        push_req->SetPath("/style.css");
        resp->PushResource(push_req);

        // 返回 HTML
        resp->SetBody("<html>...</html>");
    });
```

**客户端：**
```cpp
client->SetPushHandler([](auto push_resp) {
    std::cout << "Received push: " << push_resp->GetPath() << std::endl;
});
```

---

### 4. Upgrade 模块 - 协议升级

**功能：**
智能检测和协商 HTTP 协议版本（HTTP/1.1 → HTTP/2 → HTTP/3）

**核心组件：**
- **ProtocolDetector** - 协议检测器
- **VersionNegotiator** - 版本协商
- **SmartHandler** - 智能处理器
  - `HttpSmartHandler` - HTTP/1.1
  - `HttpsSmartHandler` - HTTP/2（TLS ALPN）
  - HTTP/3 处理（QUIC）

**配置示例：**
```cpp
struct UpgradeSettings {
    bool enable_http1 = true;
    bool enable_http2 = true;
    bool enable_http3 = true;

    std::vector<std::string> preferred_protocols = {"h3", "h2", "http/1.1"};

    uint32_t detection_timeout_ms = 5000;
    uint32_t upgrade_timeout_ms = 10000;
};
```

---

## 🧪 测试框架

### 单元测试（unit_test/）

**框架**: GoogleTest/GMock
**数量**: 140+ 个测试文件

**测试组织：**
```
unit_test/
├── common/
│   ├── alloter/      # 内存分配器测试
│   ├── buffer/       # 缓冲区测试（9 个测试文件）
│   ├── decode/       # 解码器测试
│   ├── network/      # 网络层测试（地址、事件循环）
│   └── structure/    # 数据结构测试
├── http3/
│   ├── qpack/        # QPACK 编解码测试
│   ├── frame/        # HTTP/3 帧测试
│   └── ...
├── quic/
│   ├── crypto/       # 加密测试
│   ├── frame/        # QUIC 帧测试
│   ├── packet/       # 包编解码测试
│   └── ...
└── upgrade/          # 协议升级测试
```

**运行测试：**
```bash
cd build
ctest --output-on-failure
# 或
./bin/unit_test_runner
```

### 性能基准测试（test/benchmarks/）

**框架**: Google Benchmark
**数量**: 14 个基准测试

**测试列表：**
- `qpack_bench` - QPACK 压缩/解压性能
- `buffer_bench` - 缓冲区操作性能
- `quic_frame_bench` - 帧编解码性能
- `quic_aead_bench` - AEAD 加密性能
- `congestion_bench` - 拥塞控制算法性能
- `http3_e2e_bench` - HTTP/3 端到端性能
- `memorypool_bench` - 内存池性能
- `timer_bench` - 定时器性能
- 等等...

**运行基准测试：**
```bash
cd build
./bin/qpack_bench
./bin/http3_e2e_bench
```

### 模糊测试（test/fuzz/）

**框架**: LibFuzzer（LLVM）

**测试目标：**
- QUIC 帧解析器
- QUIC 包解析器（Initial, Handshake, 0-RTT, 1-RTT, Retry）
- 包头解析
- 包编号编码

**启用模糊测试：**
```bash
cmake -DENABLE_FUZZING=ON ..
cmake --build . --target fuzz_quic_frame
./bin/fuzz_quic_frame corpus/ -max_len=65536
```

### CI/CD

**GitHub Actions 工作流：**
- 多平台测试矩阵（Ubuntu、macOS、Windows）
- 自动化构建和测试
- 代码覆盖率报告

---

## 🔧 构建系统详解

### CMake 构建

**基本构建流程：**
```bash
# 1. 创建构建目录
mkdir build && cd build

# 2. 配置（默认 Release 模式）
cmake ..

# 3. 编译
cmake --build .

# 4. 运行测试
ctest --output-on-failure
```

**自定义构建选项：**
```bash
cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_EXAMPLES=ON \
  -DENABLE_TESTING=ON \
  -DENABLE_BENCHMARKS=ON \
  -DENABLE_FUZZING=OFF
```

**构建特定目标：**
```bash
# 只构建库
cmake --build . --target quicx
cmake --build . --target http3

# 构建示例
cmake --build . --target hello_world_server
cmake --build . --target concurrent_requests_client

# 构建测试
cmake --build . --target unit_test_runner
```

### 输出目录结构

```
build/
├── bin/                    # 可执行文件
│   ├── hello_world_server
│   ├── hello_world_client
│   ├── concurrent_requests_server
│   ├── concurrent_requests_client
│   ├── restful_api_server
│   ├── unit_test_runner
│   └── ...
└── lib/                    # 库文件
    ├── libquicx.a
    ├── libhttp3.a
    └── ...
```

---

## 📚 示例代码详解

### 1. hello_world - 基础示例

**位置**: `example/hello_world/`

**服务器（7 行核心代码）：**
```cpp
// server.cpp
auto server = IServer::Create();
server->AddHandler(HttpMethod::kGet, "/hello",
    [](auto req, auto resp) {
        resp->AppendBody("hello world");
        resp->SetStatusCode(200);
    });
server->Init(config);
server->Start("0.0.0.0", 8883);
```

**客户端（5 行核心代码）：**
```cpp
// client.cpp
auto client = IClient::Create();
client->Init(config);
client->DoRequest("https://127.0.0.1:8883/hello", HttpMethod::kGet, request,
    [](auto response, uint32_t error) {
        std::cout << response->GetBodyAsString() << std::endl;
    });
```

**运行：**
```bash
# 终端 1
./build/bin/hello_world_server

# 终端 2
./build/bin/hello_world_client
```

---

### 2. concurrent_requests - 并发演示

**位置**: `example/concurrent_requests/`
**文档**: `example/concurrent_requests/README_cn.md`

**展示内容：**
- HTTP/3 多路复用优势
- 并发请求 vs 顺序请求性能对比

**性能数据：**
- **测试场景**: 15 个混合请求（5 个快速 + 5 个中速 + 5 个慢速）
- **顺序执行**: ~3073ms（队头阻塞）
- **并发执行**: ~509ms（多路复用）
- **加速比**: **6.04x**

**核心代码：**
```cpp
// server.cpp:25
// 快速请求（0ms 延迟）
server->AddHandler(HttpMethod::kGet, "/api/fast/:id", ...);

// 中速请求（100ms 延迟）
server->AddHandler(HttpMethod::kGet, "/api/medium/:id", ...);

// 慢速请求（500ms 延迟）
server->AddHandler(HttpMethod::kGet, "/api/slow/:id", ...);
```

**可视化时间线**:
```
顺序请求（3073ms）:
[Slow1][Slow2][Slow3][Slow4][Slow5][Medium1-5][Fast1-5]

并发请求（509ms）:
[Slow1-5 并行]
[Medium1-5 并行]
[Fast1-5 并行]
```

---

### 3. restful_api - RESTful API 示例

**位置**: `example/restful_api/`
**文档**: `example/restful_api/README_cn.md`

**功能：**
完整的用户管理 RESTful API

**API 端点：**
```
GET    /users           # 获取所有用户
GET    /users/:id       # 获取单个用户
POST   /users           # 创建用户
PUT    /users/:id       # 更新用户
DELETE /users/:id       # 删除用户
```

**示例请求：**
```bash
# 创建用户
curl -X POST https://localhost:8883/users \
  -H "Content-Type: application/json" \
  -d '{"name": "Alice", "email": "alice@example.com"}'

# 获取用户
curl https://localhost:8883/users/1

# 更新用户
curl -X PUT https://localhost:8883/users/1 \
  -H "Content-Type: application/json" \
  -d '{"name": "Alice Smith", "email": "alice.smith@example.com"}'
```

**核心代码：**
```cpp
// server.cpp
// GET /users
server->AddHandler(HttpMethod::kGet, "/users",
    [&db](auto req, auto resp) {
        std::string json = db.GetAllUsersJson();
        resp->SetBody(json);
        resp->SetHeader("Content-Type", "application/json");
    });

// POST /users
server->AddHandler(HttpMethod::kPost, "/users",
    [&db](auto req, auto resp) {
        std::string body = req->GetBody();
        User user = ParseUserFromJson(body);
        int id = db.CreateUser(user);
        resp->SetStatusCode(201);
        resp->SetBody("{\"id\": " + std::to_string(id) + "}");
    });
```

---

### 4. streaming_api - 流式传输

**位置**: `example/streaming_api/`

**功能：**
- 大文件上传（分块接收）
- 大文件下载（分块发送）
- 实时数据流

**流式下载示例：**
```cpp
class StreamDownloadHandler : public IAsyncClientHandler {
    FILE* file_;

    void OnHeaders(std::shared_ptr<IResponse> resp) override {
        file_ = fopen("download.bin", "wb");
    }

    void OnBodyChunk(const uint8_t* data, size_t len, bool is_last) override {
        fwrite(data, 1, len, file_);
        if (is_last) {
            fclose(file_);
            std::cout << "Download complete" << std::endl;
        }
    }
};
```

---

### 5. server_push - 服务器推送

**位置**: `example/server_push/`

**功能：**
演示 HTTP/3 服务器推送特性

**服务器端：**
```cpp
server->AddHandler(HttpMethod::kGet, "/page",
    [](auto req, auto resp) {
        // 推送 CSS
        auto css_push = IPushRequest::Create();
        css_push->SetPath("/style.css");
        css_push->SetMethod(HttpMethod::kGet);
        resp->PushResource(css_push);

        // 推送 JS
        auto js_push = IPushRequest::Create();
        js_push->SetPath("/script.js");
        resp->PushResource(js_push);

        // 返回 HTML
        resp->SetBody("<html>...</html>");
    });
```

**客户端：**
```cpp
client->SetPushHandler([](auto push_resp) {
    std::cout << "Received push: " << push_resp->GetPath() << std::endl;
    // 保存推送的资源到缓存
});
```

---

### 6. upgrade_h3 - 协议升级

**位置**: `example/upgrade_h3/`

**功能：**
演示 HTTP 协议自动升级（HTTP/1.1 → HTTP/2 → HTTP/3）

---

### 7. quicx_curl - 命令行工具

**位置**: `example/quicx_curl/`

**功能：**
类似 curl 的 HTTP/3 客户端工具

**使用示例：**
```bash
./build/bin/quicx_curl https://example.com/api
./build/bin/quicx_curl -X POST -d '{"key":"value"}' https://api.example.com
```

---

## 💡 核心实现细节

### 并发模型

**线程模式：**
```cpp
enum class ThreadMode {
    kSingleThread,  // 单线程：主线程处理所有 I/O
    kMultiThread,   // 多线程：1 个主线程 + N 个工作线程
};
```

**配置示例：**
```cpp
QuicConfig config;
config.thread_mode = ThreadMode::kMultiThread;
config.thread_num = 4;  // 4 个工作线程
```

### 错误处理

**QUIC 错误码：**
- `NO_ERROR` (0x00) - 无错误
- `INTERNAL_ERROR` (0x01) - 内部错误
- `CONNECTION_REFUSED` (0x02) - 连接被拒绝
- `FLOW_CONTROL_ERROR` (0x03) - 流控错误
- `STREAM_LIMIT_ERROR` (0x04) - 流数量超限
- `PROTOCOL_VIOLATION` (0x0A) - 协议违规
- `CRYPTO_ERROR` (0x100-0x1FF) - TLS 错误

**HTTP/3 错误码：**
- `H3_NO_ERROR` (0x100) - 无错误
- `H3_GENERAL_PROTOCOL_ERROR` (0x101) - 协议错误
- `H3_INTERNAL_ERROR` (0x102) - 内部错误
- `H3_STREAM_CREATION_ERROR` (0x103) - 流创建错误
- `H3_CLOSED_CRITICAL_STREAM` (0x104) - 关键流关闭
- `H3_FRAME_UNEXPECTED` (0x105) - 意外帧
- `H3_FRAME_ERROR` (0x106) - 帧错误
- `H3_EXCESSIVE_LOAD` (0x107) - 负载过高

### 日志系统

**日志级别：**
```cpp
enum class LogLevel {
    kTrace,    // 最详细
    kDebug,
    kInfo,
    kWarn,
    kError,
    kFatal     // 最严重
};
```

**配置日志：**
```cpp
QuicConfig config;
config.log_level = LogLevel::kInfo;
config.log_file = "/var/log/quicx.log";
```

---

## 🚀 性能优化技巧

### 1. 调整传输参数

```cpp
QuicTransportParams params;
params.initial_max_data_ = 100 * 1024 * 1024;  // 100MB 连接窗口
params.initial_max_stream_data_bidi_local_ = 10 * 1024 * 1024;  // 10MB 流窗口
params.initial_max_streams_bidi_ = 1000;  // 1000 个并发流
```

### 2. 选择合适的拥塞控制算法

```cpp
QuicConfig config;
config.congestion_control_algorithm = CongestionControlAlgorithm::kBBRv2;
```

**算法选择建议：**
- **BBR v2/v3**: 高带宽、高延迟网络（互联网长距离传输）
- **Cubic**: 通用场景，友好共存
- **Reno**: 保守场景，兼容性好

### 3. 启用 0-RTT

```cpp
QuicConfig config;
config.enable_0rtt = true;
```

**注意**: 0-RTT 可能存在重放攻击风险，仅用于幂等操作。

### 4. 调整 MTU

```cpp
QuicTransportParams params;
params.max_udp_payload_size_ = 1500 - 28;  // 根据网络 MTU 调整
```

### 5. 使用内存池

```cpp
// 库内部已使用内存池，无需额外配置
// 减少了频繁的 malloc/free 调用
```

---

## 📖 常用开发命令

### 构建命令

```bash
# 完整构建（Debug 模式）
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . -j$(nproc)

# 完整构建（Release 模式）
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)

# 只构建库
cmake --build . --target quicx -j$(nproc)

# 构建示例
cmake --build . --target hello_world_server -j$(nproc)
cmake --build . --target concurrent_requests_client -j$(nproc)
```

### 测试命令

```bash
# 运行所有单元测试
cd build
ctest --output-on-failure

# 运行特定测试
./bin/unit_test_runner --gtest_filter=BufferTest.*

# 运行性能基准测试
./bin/qpack_bench
./bin/http3_e2e_bench --benchmark_filter=.*
```

### 运行示例

```bash
# hello_world 示例
./build/bin/hello_world_server &
./build/bin/hello_world_client

# concurrent_requests 示例
./build/bin/concurrent_requests_server &
./build/bin/concurrent_requests_client

# restful_api 示例
./build/bin/restful_api_server &
curl -k https://localhost:8883/users
```

### 清理命令

```bash
# 清理构建产物
cd build
make clean

# 完全清理（删除构建目录）
rm -rf build
```

---

## 🐛 常见问题

### 1. BoringSSL 编译失败

**问题**: 找不到 BoringSSL 或编译错误

**解决**:
```bash
cd third/boringssl
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 2. 证书错误

**问题**: TLS 握手失败，证书验证错误

**解决**:
```cpp
QuicConfig config;
config.verify_peer = false;  // 仅用于测试！生产环境请使用有效证书
```

### 3. 端口被占用

**问题**: `Address already in use`

**解决**:
```bash
# 查找占用端口的进程
lsof -i :8883
# 或
netstat -tulpn | grep 8883

# 杀死进程
kill -9 <PID>
```

### 4. 连接超时

**问题**: 客户端连接服务器超时

**检查**:
- 防火墙是否阻止 UDP 端口
- 服务器是否正确监听
- 客户端和服务器配置是否匹配

```bash
# 检查 UDP 端口是否开放
nc -u -v localhost 8883
```

### 5. 内存泄漏

**调试**:
```bash
# 使用 Valgrind 检测
valgrind --leak-check=full ./build/bin/hello_world_server

# 使用 AddressSanitizer
cmake -DCMAKE_CXX_FLAGS="-fsanitize=address" ..
cmake --build .
```

---

## 📝 开发指南

### 代码风格

- **命名约定**:
  - 类名：`PascalCase`（如 `QuicConnection`）
  - 函数/方法：`PascalCase`（如 `SendPacket`）
  - 变量：`snake_case_`（如 `max_data_`，成员变量以 `_` 结尾）
  - 常量：`kPascalCase`（如 `kMaxPacketSize`）

- **接口类**:
  - 以 `I` 开头（如 `IClient`, `IServer`）
  - 纯虚函数接口

- **智能指针**:
  - 优先使用 `std::shared_ptr` 和 `std::unique_ptr`
  - 避免裸指针（除非性能关键路径）

### 添加新的拥塞控制算法

1. 继承 `ICongestionControl` 接口
2. 实现必要的方法（`OnPacketSent`, `OnPacketAcked`, `OnPacketLost` 等）
3. 在 `CongestionControlFactory` 中注册
4. 添加单元测试

### 添加新的 HTTP/3 帧类型

1. 在 `src/http3/frame/` 中创建新文件
2. 继承 `IHttp3Frame` 接口
3. 实现编码/解码方法
4. 更新帧类型枚举
5. 添加单元测试

### 添加新的示例

1. 在 `example/` 中创建目录
2. 编写 `server.cpp` 和 `client.cpp`
3. 更新 `example/CMakeLists.txt`
4. 编写 `README_cn.md` 说明文档

---

## 🔗 重要文件位置

### API 头文件
- QUIC 客户端: `src/quic/include/if_client.h`
- QUIC 服务器: `src/quic/include/if_server.h`
- HTTP/3 客户端: `src/http3/include/if_client.h`
- HTTP/3 服务器: `src/http3/include/if_server.h`
- HTTP 请求/响应: `src/http3/include/if_request.h`, `if_response.h`

### 核心实现
- QUIC 连接: `src/quic/connection/connection_client.cpp`, `connection_server.cpp`
- BBR 拥塞控制: `src/quic/congestion_control/bbr_v2_congestion_control.cpp`
- QPACK 编码器: `src/http3/qpack/qpack_encoder.cpp`
- 路由系统: `src/http3/router/router.cpp`

### 配置文件
- CMake: `CMakeLists.txt`
- Bazel: `MODULE.bazel`, `BUILD.bazel`
- TODO 列表: `TODO.md`

### 文档
- 英文 README: `README.md`
- 中文 README: `README_cn.md`
- 并发请求示例文档: `example/concurrent_requests/README_cn.md`
- RESTful API 示例文档: `example/restful_api/README_cn.md`

---

## 🎯 适用场景

1. **高性能 Web 服务**
   - 低延迟 API 服务
   - 实时通信应用

2. **CDN 和边缘计算**
   - HTTP/3 内容分发
   - 边缘节点通信

3. **流媒体服务**
   - 音视频实时传输
   - 直播推流/拉流

4. **移动应用后端**
   - 连接迁移支持网络切换
   - 0-RTT 快速连接

5. **IoT 设备通信**
   - 不稳定网络环境
   - 低功耗长连接

6. **微服务架构**
   - 服务间 RPC 通信
   - API 网关

---

## 📊 项目统计

- **代码规模**: 198 个 .cpp 文件 + 265 个 .h 文件
- **连接管理代码**: ~4100 行（connection_client.cpp + connection_server.cpp）
- **测试覆盖**: 140+ 单元测试 + 14 个性能测试 + 模糊测试
- **示例数量**: 8 个完整可运行示例
- **支持平台**: 3 个（Linux、macOS、Windows）
- **拥塞控制算法**: 5 个（BBR v1/v2/v3、Cubic、Reno）
- **QUIC 帧类型**: 20+ 种
- **HTTP/3 帧类型**: 7 种
- **第三方依赖**: 2 个（BoringSSL、GoogleTest）

---

## 🔄 最近修改

**当前 git 状态**:
```
分支: dev
已修改文件:
- example/concurrent_requests/client.cpp

未跟踪文件:
- MODULE.bazel
- package_repo.sh
- q3.tar.gz

最近提交:
- 4b034e8: fix concurrent requests request blocked.
- 1a09e62: fix concurrent requests request blocked.
- d464ac8: fix github ci build issues.
```

---

## 📞 联系与资源

- **项目地址**: `/mnt/d/code/quicX`
- **主分支**: `main`
- **开发分支**: `dev`
- **开源协议**: BSD 3-Clause License
- **问题反馈**: 参考 README.md

---

**最后更新**: 2025-11-30
**文档版本**: 1.0
**适用于**: QuicX 项目所有开发者和 Claude Code AI 助手
