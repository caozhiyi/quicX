# Quick Start: Running Your First quicX Program

After successfully compiling `quicX`, the best way to understand its workings is to run a basic `Hello World` example. In this tutorial, we will take you through compiling and running native HTTP/3 Client and Server applications, break down its core code line by line, and list all advanced examples in the project you can reference.

---

## 1. Running Hello World

All example codes are located in the `example/` directory. If you kept `BUILD_EXAMPLES=ON` (enabled by default) during CMake configuration, the executable files for these examples should already be generated under `build/bin/`.

To establish a simple HTTP/3 connection:

1. **Start the server process**:
   Open a terminal and execute:
   ```bash
   ./build/bin/hello_world_server
   ```
   *The server is now listening for UDP-based QUIC traffic at `0.0.0.0:7001`.*

2. **Start the client process to send a request**:
   Open a new terminal and execute:
   ```bash
   ./build/bin/hello_world_client
   ```

If everything goes well, you should see output resembling the following in the client's terminal:
```text
======== Response Received ========
Status: 200
Body: hello world
===================================
Request took: 12 ms
```

Awesome! You just completed an encrypted handshake and data transfer based on the latest HTTP/3 protocol. Let's dive into the code to see how it works.

---

## 2. Server Code Deconstruction

Open `example/hello_world/server.cpp`, and you'll see a very minimal setup of an HTTP/3 Router.

### 2.1 Initialization & Route Registration
`quicX`'s HTTP/3 layer abstracts away complex Stream handling, directly exposing request/response semantics.

```cpp
auto server = quicx::IServer::Create();

// Register a route: When a GET request is received and matches "/hello", this lambda callback is invoked
server->AddHandler(quicx::HttpMethod::kGet, "/hello",
    [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
        std::cout << "get request body: " << req->GetBodyAsString() << std::endl;

        // Populate response body and status code
        resp->AppendBody(std::string("hello world"));
        resp->SetStatusCode(200);
    });
```
* **Non-blocking Model**: Driven by an event-driven underlying model, `quicX` uses the complete mode, invoking the callback only after buffering the entire Body. For massive file uploads, you should use the streaming processor derived from `IAsyncServerHandler` (refer to other Examples).

### 2.2 Configuration & Startup
QUIC strictly demands TLS 1.3, so even for local testing, a certificate must be provided.

```cpp
quicx::Http3ServerConfig config;
config.quic_config_.cert_pem_ = cert_pem; // Fill in the PEM format certificate string or read from a file
config.quic_config_.key_pem_ = key_pem;   // Fill in the PEM format private key string
config.quic_config_.config_.worker_thread_num_ = 1; // Set the number of worker threads

server->Init(config);

// Bind the port and block waiting
if (!server->Start("0.0.0.0", 7001)) {
    // Error handling
}
server->Join(); 
```

---

## 3. Client Code Deconstruction

Open `example/hello_world/client.cpp`. The core logic of the client initiates the request and asynchronously handles the response.

```cpp
auto client = quicx::IClient::Create();

quicx::Http3ClientConfig config;
config.quic_config_.config_.worker_thread_num_ = 1;
client->Init(config);

// Construct the request payload
auto request = quicx::IRequest::Create();
request->AppendBody(std::string("hello world"));

// Initiate the QUIC-backed HTTP/3 request
client->DoRequest("https://127.0.0.1:7001/hello", quicx::HttpMethod::kGet, request,
    [&](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
        if (error != 0) {
            std::cout << "Request failed with error: " << error << std::endl;
        } else {
            std::cout << "Status: " << response->GetStatusCode() << std::endl;
            std::cout << "Body: " << response->GetBodyAsString() << std::endl;
        }
        // ... notify main thread of completion
    });
```
* **Asynchronous Non-blocking**: The `DoRequest` call returns immediately. During this time, the underlying state machine is busy taking action: UDP addressing -> 1-RTT TLS Handshake -> QPACK Header Compression -> Data flow transmission.
* **Error Checking**: It's crucial in the callback to check if `error` is 0 (meaning no network congestion, packet loss, or interruption happened), and only then attempt to read the `response`.

---

## 4. Advanced Exploration: Current Example Reference Manual

`Hello World` is just the beginning. To help developers handle various complex business edge cases and technical research, `quicX` provides a rich set of comprehensive tutorials under `example/`.

Whenever you have a specific requirement, you are highly advised to browse these examples first:

| Example Directory (`example/`) | Addressed Core Business Scenarios Recommended for Reference |
| :--- | :--- |
| **`hello_world`** | **Basic GET Req/Res Handling**, great for beginners grasping the lifecycle. |
| **`restful_api`** | **REST APIs with path param**, displaying how url variables are extracted into JSON replies. |
| **`file_transfer`** | **Large File Uploads/Downloads**, displaying streams to bypass memory explosions. |
| **`streaming_api`** | **Chunked streaming responses**, showing Chat-GPT "typewriter" style text feeds. |
| **`bidirectional_comm`** | **Pure Dual-Directional messaging**, proving communication beyond strict request-and-reply limitations. |
| **`concurrent_requests`** | **High Concurrency handling**, illustrating how QUIC's multiplexing works wonders with mass simultaneous asks. |
| **`connection_lifecycle`** | **Lifecycles & Graceful shutdown**, displaying the best ways to observe and terminate events safely. |
| **`error_handling`** | **Error catching best practices**, proving capabilities on offline reconnections or route misses. |
| **`server_push`** | **HTTP/3 Active Server Pushes**, showing the implementation of RFC `PUSH_PROMISE` features to proactively deliver client-bound assets. |
| **`load_testing`** | **Simple Internal Load test**, for gauging script optimizations. |
| **`metrics_monitoring`** | **Observability**, letting users learn to export live dropping packets, congestion latency, and buffer RTT graphs. |
| **`qlog_integration`** | **Qlog Generation**, seamlessly pairing with Wireshark and Qvis. |
| **`quicx_curl`** | **cURL-like console client**, making manual testing an absolute breeze. |

You are free to navigate any of these directories and inspect the `.cpp` source directly.
