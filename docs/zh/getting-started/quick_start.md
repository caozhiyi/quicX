# 快速上手：运行你的第一个 quicX 程序

在成功编译 `quicX` 之后，最快了解它工作原理的方式就是运行一个最基础的 `Hello World` 示例。在这篇教程中，我们将带你编译并运行一个原生的 HTTP/3 客户端与服务端，并逐行解析它的核心代码，最后为你列出项目中所有可以直接参考的高级示例。

---

## 一、 运行 Hello World

所有的示例代码都放置在代码库的 `example/` 目录下。如果你在 CMake 配置时开启了 `BUILD_EXAMPLES=ON`（默认开启），那么在 `build/bin/` 目录下已经生成了这些示例的可执行文件。

为了跑通最简单的 HTTP/3 通信：

1. **启动服务端进程**：
   打开一个终端，执行：
   ```bash
   ./build/bin/hello_world_server
   ```
   *此时服务端会在 `0.0.0.0:7001` 开始监听基于 UDP 的 QUIC 流量。*

2. **启动客户端进程发起请求**：
   新开一个终端，执行：
   ```bash
   ./build/bin/hello_world_client
   ```

如果一切顺利，你应该能在客户端终端看到类似如下的输出：
```text
======== Response Received ========
Status: 200
Body: hello world
===================================
Request took: 12 ms
```

太棒了！你刚刚完成了基于最新 HTTP/3 协议的加密握手和数据传输。下面让我们潜入代码了解它是怎么运作的。

---

## 二、 服务端代码解构

打开 `example/hello_world/server.cpp`，你能看到一个极简的 HTTP/3 Router 的搭建过程。

### 1. 实例化与路由注册
`quicX` 的 HTTP/3 层不需要你处理复杂的 Stream，而是直接暴露了请求/响应语义。

```cpp
auto server = quicx::IServer::Create();

// 注册路由：当收到 GET 请求并匹配 "/hello" 路径时，触发该 Lambda 回调
server->AddHandler(quicx::HttpMethod::kGet, "/hello",
    [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
        std::cout << "get request body: " << req->GetBodyAsString() << std::endl;

        // 填充响应体和状态码
        resp->AppendBody(std::string("hello world"));
        resp->SetStatusCode(200);
    });
```
* **非阻塞模型**：由于底层的基于事件驱动的模型，`quicX` 采用将全部 Body 缓冲完毕后，再触发一次性回调的方式（即完整模式）。如果你需要处理大文件上传，可以使用基于 `IAsyncServerHandler` 的流式处理器（详见其他 Example）。

### 2. 配置与启动
QUIC 是强制要求开启 TLS 1.3 的协议，因此即使是本机测试，也必须提供证书。

```cpp
quicx::Http3ServerConfig config;
config.quic_config_.cert_pem_ = cert_pem; // 填入 PEM 格式的证书字符串或读取的文件内容
config.quic_config_.key_pem_ = key_pem;   // 填入 PEM 格式的私钥字符串
config.quic_config_.config_.worker_thread_num_ = 1; // 设置工作线程数

server->Init(config);

// 绑定端口并阻塞等待
if (!server->Start("0.0.0.0", 7001)) {
    // 错误处理
}
server->Join(); 
```

---

## 三、 客户端代码解构

打开 `example/hello_world/client.cpp`，客户端的核心逻辑是发起请求并异步处理响应。

```cpp
auto client = quicx::IClient::Create();

quicx::Http3ClientConfig config;
config.quic_config_.config_.worker_thread_num_ = 1;
client->Init(config);

// 构建请求载荷
auto request = quicx::IRequest::Create();
request->AppendBody(std::string("hello world"));

// 发起基于 QUIC 的 HTTP/3 请求
client->DoRequest("https://127.0.0.1:7001/hello", quicx::HttpMethod::kGet, request,
    [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
        if (error != 0) {
            std::cout << "Request failed with error: " << error << std::endl;
        } else {
            std::cout << "Status: " << response->GetStatusCode() << std::endl;
            std::cout << "Body: " << response->GetBodyAsString() << std::endl;
        }
        // ... 通知主线程任务已完成
    });
```
* **异步非阻塞**：`DoRequest` 调用会立即返回。此时底层的状态机正在执行：UDP 寻址 -> 1-RTT TLS 握手 -> QPACK 首部压缩 -> 数据流发送。
* **错误判断**：在回调中，一定要检查 `error` 是否为 0（代表没有发生网络或协议丢包及中断错误），然后再读取 `response`。

---

## 四、 进阶探索：现有示例参考手册

`Hello World` 仅仅是一个开场。为了方便开发者应对各种复杂的业务场景和技术预研，`quicX` 的 `example/` 目录下提供了非常丰富的保姆级示例代码。

当你有特定的需求时，强烈建议你先在下面的列表中寻找对应的示例进行参考：

| 示例目录 (`example/`) | 解决的核心业务问题推荐参考 |
| :--- | :--- |
| **`hello_world`** | **最基础的 GET 请求/响应处理**，适合刚入门理解生命周期。 |
| **`restful_api`** | **带路径参数的 REST API**，展示如何捕获 URL 中的参数并返回 JSON。 |
| **`file_transfer`** | **大文件上传/下载**，展示了极其耗费 IO 的大块数据下，如何使用流式处理器避免内存爆炸。 |
| **`streaming_api`** | **分块式的流式传输响应**，模拟类似 ChatGPT 的打字机推送效果或音视频流。 |
| **`bidirectional_comm`** | **纯粹的双向流通信机制**，展示不拘泥于请求/响应，双方自由发送数据的模式。 |
| **`concurrent_requests`** | **高并发请求处理**，展示 QUIC 的多路复用能力如何同时承载大量请求而不阻塞。 |
| **`connection_lifecycle`** | **生命周期与优雅关闭**，演示如何监听页面事件并干净地关闭资源退出。 |
| **`error_handling`** | **错误抛出与处理最佳实践**，模拟断网、非匹配路由、重传超时的捕获手段。 |
| **`server_push`** | **HTTP/3 服务器主动推送**，使用 RFC 规定中的 `PUSH_PROMISE` 主动提前把数据下发给客户端缓存。|
| **`load_testing`** | **简单的内部压测脚本**，适合自测代码优化的效果。 |
| **`metrics_monitoring`** | **可观测性与监控**，向你演示如何在运行时导出内置的丢包率、RTT 延迟、拥塞窗口等监控数据。 |
| **`qlog_integration`** | **生成 Qlog**，用以无缝对接 Wireshark 或 qvis 前端实现极致的可视化调试。 |
| **`quicx_curl`** | **类 curl 命令行客户端**，可以直接当作一个 HTTP/3 的命令行测试工具来使用。 |

你可以直接打开上述目录中的 `.cpp` 源码进行阅读。
