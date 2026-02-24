# Server Push Example

This example demonstrates how to use HTTP/3 Server Push with the QuicX library.

## Overview

HTTP/3 Server Push allows the server to send resources to the client before the client requests them. This is particularly useful for sending associated resources (like CSS, JavaScript, or images) when an HTML page is requested, potentially improving page load times.

This example consists of:
- **Server**: Responds to a request and initiates a push promise for an additional resource.
- **Client**: Sends a request, handles the response, and accepts or cancels push promises.

## Features Demonstrated

### Server-Side
- **Push Initiation**: Triggering a push promise from a request handler.
- **Push Response**: Sending headers and body for the pushed resource.
- **Custom Push Headers**: Adding specific headers to push promises.

### Client-Side
- **Push Promise Handling**: Callback for inspecting and accepting/rejecting push promises.
- **Push Stream Handling**: Callback for receiving the pushed content.
- **Push Cancellation**: Returning `false` from the promise handler to reset the push stream.

## Build

### Using CMake (from project root)

```bash
# Configure
mkdir -p build && cd build
cmake ..

# Build
make server_push client_push

# Or build all examples
make
```

The executables will be in `build/bin/`:
- `server_push`
- `client_push`

## Usage

### 1. Start the Server

```bash
./build/bin/server_push
```

The server listens on port 7008.

### 2. Run the Client (in another terminal)

```bash
./build/bin/client_push
```

The client performs two test scenarios:

1.  **Accept Push**:
    - Sends a GET request to `/hello`.
    - Server responds with "hello world" and pushes a resource.
    - Client accepts the push promise.
    - Client receives the pushed content "hello push".

2.  **Cancel Push**:
    - Waits for 1 second.
    - Sends another GET request to `/hello`.
    - Server initiates the push again.
    - Client rejects the push promise (returns `false` in handler).
    - The push stream is reset.

## Expected Output

### Client Output

```text
status: 200
response: hello world
get push promise. header:push-key1 value:test1
get push promise. header:push-key2 value:test2

push status: 200
push response: hello push
status: 200
response: hello world
get push promise. header:push-key1 value:test1
get push promise. header:push-key2 value:test2
```

(Note: The order of push promise/response logs vs main response logs may vary slightly due to concurrency)

### Server Output

```text
get request method: GET
get request path: /hello
get request body: hello world
get request method: GET
get request path: /hello
get request body: hello world
```

## Automated Testing

You can use the provided Python script to run the test automatically:

```bash
cd example/server_push
python3 run_test.py
```

This script handles starting the server, running the client, and verifying the output matches the expected behavior.

## Code Highlights

### Server: Initiating Push

```cpp
server->AddHandler(quicx::HttpMethod::kGet, "/hello",
    [](std::shared_ptr<quicx::IRequest> req, std::shared_ptr<quicx::IResponse> resp) {
        // ... handle main request ...

        // Create and attach push response
        auto push_resp = quicx::IResponse::Create();
        push_resp->AddHeader("push-key1", "test1");
        push_resp->AppendBody("hello push");
        push_resp->SetStatusCode(200);
        
        // Trigger server push
        resp->AppendPush(push_resp);
    });
```

### Client: Handling Push Promises

```cpp
client->SetPushPromiseHandler([](std::unordered_map<std::string, std::string>& headers) -> bool {
    // Inspect headers
    // Return true to accept, false to reject
    return true; 
});
```

### Client: Receiving Pushed Content

```cpp
client->SetPushHandler([](std::shared_ptr<quicx::IResponse> response, uint32_t error) {
    if (error == 0) {
        // Process pushed content
        std::cout << "push response: " << response->GetBodyAsString() << std::endl;
    }
});
```
