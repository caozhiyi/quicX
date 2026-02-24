# HTTP/3 Streaming API Example

This example demonstrates how to implement high-performance file streaming (upload and download) using the QuicX HTTP/3 library.

## Features Demonstrated

- **Streaming Request Body**: Uploading large files incrementally without loading the entire file into memory.
- **Streaming Response Body**: Downloading large files incrementally using a body provider callback.
- **Async Handlers**: Utilizing `IAsyncServerHandler` and `IAsyncClientHandler` for non-blocking data processing.
- **Flow Control**: Efficiently managing data flow between network and disk.

## Architecture

### Server
- **GET /status**: Returns server status (Complete Mode, simple request/response).
- **POST /upload/:filename**: Handles file uploads via `FileUploadHandler` (Async Mode). Writes received chunks directly to disk.
- **GET /download/:filename**: Handles file downloads via `body_provider` (Streaming Response). Reads chunks from disk and sends them incrementally.

### Client
- **Status Check**: Simple GET request.
- **Upload**: Uses a request body provider to read from a local file and send it to the server.
- **Download**: Uses `FileDownloadHandler` to receive response body chunks and write them directly to disk.

## Build

### Using CMake (from project root)

```bash
# Configure
mkdir -p build && cd build
cmake ..

# Build
make streaming_server streaming_client

# Or build all examples
make
```

The executables will be in `build/bin/`:
- `streaming_server`
- `streaming_client`

## Usage

### 1. Start the Server

```bash
./build/bin/streaming_server
```

The server listens on port 7009 by default.

Output:
```
========================================
HTTP/3 Streaming API Server
========================================
Listening on: 0.0.0.0:7009

Endpoints:
  GET  /status                - Server status
  POST /upload/:filename      - Upload file (async mode)
  GET  /download/:filename    - Download file (complete mode with provider)

Press Ctrl+C to stop
========================================
```

### 2. Run the Client (in another terminal)

The client automatically detects the operation based on the URL.

#### Check Status

```bash
./build/bin/streaming_client https://127.0.0.1:7009/status
```

#### Upload a File

```bash
# Syntax: client <url> <local_file_to_upload>
./build/bin/streaming_client https://127.0.0.1:7009/upload/remote_name.dat local_file.dat
```

#### Download a File

```bash
# Syntax: client <url> <local_destination_file>
./build/bin/streaming_client https://127.0.0.1:7009/download/remote_name.dat output.dat
```

## Automated Testing

You can use the provided Python script to run a full test cycle (Status -> Upload -> Download -> Verify Integrity):

```bash
cd example/streaming_api
python3 run_test.py
```

## Implementation Details

### Server: Async Upload Handler

To handle uploads without buffering the whole file:

```cpp
class FileUploadHandler: public IAsyncServerHandler {
    void OnHeaders(std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) {
        // Open file for writing
        file_ = fopen(filename.c_str(), "wb");
    }

    void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) {
        // Write chunk directly to disk
        fwrite(data, 1, length, file_);
        
        if (is_last) fclose(file_);
    }
};
```

### Server: Streaming Download Provider

To serve downloads without buffering:

```cpp
response->SetResponseBodyProvider([state](uint8_t* buffer, size_t buffer_size) -> size_t {
    // Read directly from disk into the network buffer
    size_t bytes_read = fread(buffer, 1, buffer_size, state->file);
    return bytes_read;
});
```

This ensures that memory usage remains low even when transferring multi-gigabyte files.
