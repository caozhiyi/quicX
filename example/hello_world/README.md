# Hello World Example

This example demonstrates a simple HTTP/3 client-server interaction using the `quicX` library. It establishes a QUIC connection, sends a basic HTTP request, and prints the server's response along with the round-trip time.

## Contents

- **`server.cpp`**: A simple HTTP/3 server that listens on port 8883 and responds to requests at `/hello` with "hello world".
- **`client.cpp`**: A standard HTTP/3 client that connects to the server, sends a GET request, and measures the request latency.

## Test Purpose

1. **Connectivity Verification**: Validates that `quicX` can successfully establish a QUIC connection and perform the TLS handshake.
2. **Basic HTTP/3 Flow**: Demonstrates the usage of `IClient` and `IServer` interfaces for sending and receiving HTTP requests.
3. **Performance Monitoring**: Calculates and displays the costs (latency) of the request-response cycle.

## How to Run

Assuming you have built the project and are in the `build` directory:

### 1. Start the Server

Run the server in a terminal window. It will listen on `127.0.0.1:8883`.

```bash
./bin/hello_world_server
```

### 2. Run the Client

Run the client in a separate terminal window. It will connect to the server, send a request, print the response details (status, headers, body, latency), and then exit.

```bash
./bin/hello_world_client
```

### Expected Output

The client should output something similar to:

```text
status: 200
content-length: 11
response: hello world
cost time: 5 ms
```
