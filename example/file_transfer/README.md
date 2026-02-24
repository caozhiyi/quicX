# File Transfer Example

This example demonstrates how to transfer large files over HTTP/3 using **streaming APIs** for efficient memory usage.

## Features

- **Streaming Download** - Uses `IAsyncClientHandler` to process response body chunks as they arrive
- **Streaming Upload** - Uses `SetRequestBodyProvider` to send request body in chunks
- **Large file support** - Transfer files of any size without buffering entire content in memory
- **Resume support** - Continue interrupted downloads (HTTP Range requests)
- **Progress tracking** - Real-time progress bar with speed and ETA
- **Checksum verification** - Verify file integrity after transfer

## Building

```bash
cd build
cmake ..
make file_transfer_server file_transfer_client
```

## Usage

### Server

Start the file server:

```bash
./bin/file_transfer_server [directory]

# Example:
./bin/file_transfer_server ./files
```

The server listens on port `7006` and supports:
- `GET /*` - Download files (streaming response)
- `POST /upload/*` - Upload files (streaming request)

### Client

#### Download a file

```bash
./bin/file_transfer_client download <url> [output_file]

# Example - simple download:
./bin/file_transfer_client download https://127.0.0.1:7006/largefile.bin

# Example - specify output file:
./bin/file_transfer_client download https://127.0.0.1:7006/largefile.bin ./downloads/myfile.bin

# Example - resume interrupted download:
# Just run the same command again, it will automatically resume
./bin/file_transfer_client download https://127.0.0.1:7006/largefile.bin ./partial.bin
```

#### Upload a file

```bash
./bin/file_transfer_client upload <url> <input_file>

# Example:
./bin/file_transfer_client upload https://127.0.0.1:7006/upload/myfile.bin ./localfile.bin
```

## Streaming API Usage

### Client-side Download (IAsyncClientHandler)

```cpp
class FileDownloadHandler : public quicx::IAsyncClientHandler {
    std::ofstream file_;
    
    void OnHeaders(std::shared_ptr<IResponse> response) override {
        // Called when response headers are received
        if (response->GetStatusCode() == 200) {
            file_.open("download.bin", std::ios::binary);
        }
    }
    
    void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) override {
        // Called for each chunk of response body
        if (file_.is_open()) {
            file_.write(reinterpret_cast<const char*>(data), length);
        }
        if (is_last) {
            file_.close();
            std::cout << "Download completed!" << std::endl;
        }
    }
};

// Use the handler
auto request = IRequest::Create();
auto handler = std::make_shared<FileDownloadHandler>();
client->DoRequest(url, HttpMethod::kGet, request, handler);
```

### Client-side Upload (SetRequestBodyProvider)

```cpp
auto request = IRequest::Create();
auto file = std::make_shared<std::ifstream>("upload.bin", std::ios::binary);

request->SetRequestBodyProvider([file](uint8_t* buf, size_t size) -> size_t {
    if (!file->is_open() || file->eof()) {
        return 0;  // End of body
    }
    file->read(reinterpret_cast<char*>(buf), size);
    return file->gcount();
});

client->DoRequest(url, HttpMethod::kPost, request, response_handler);
```

### Server-side Upload (IAsyncServerHandler)

```cpp
class FileUploadHandler : public quicx::IAsyncServerHandler {
    std::ofstream file_;
    
    void OnHeaders(std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) override {
        // Called when request headers are received
        file_.open("uploaded.bin", std::ios::binary);
    }
    
    void OnBodyChunk(const uint8_t* data, size_t length, bool is_last) override {
        // Called for each chunk of request body
        if (file_.is_open()) {
            file_.write(reinterpret_cast<const char*>(data), length);
        }
        if (is_last) {
            file_.close();
            // Response is sent automatically after handler completes
        }
    }
};

// Register handler
server->AddHandler(HttpMethod::kPost, "/upload/*", 
                   std::make_shared<FileUploadHandler>());
```

### Server-side Download (SetResponseBodyProvider)

```cpp
server->AddHandler(HttpMethod::kGet, "/*",
    [](std::shared_ptr<IRequest> req, std::shared_ptr<IResponse> resp) {
        auto file = std::make_shared<std::ifstream>("file.bin", std::ios::binary);
        
        resp->SetStatusCode(200);
        resp->SetResponseBodyProvider([file](uint8_t* buf, size_t size) -> size_t {
            if (!file->is_open() || file->eof()) {
                return 0;  // End of body
            }
            file->read(reinterpret_cast<char*>(buf), size);
            return file->gcount();
        });
    });
```

## Demo

### 1. Large File Download (Streaming)

```bash
# Generate a 100MB test file
dd if=/dev/urandom of=files/100MB.bin bs=1M count=100

# Download it using streaming
./bin/file_transfer_client download https://127.0.0.1:7006/100MB.bin
```

Output:
```
Downloading: https://127.0.0.1:7006/100MB.bin
File size: 104857600 bytes (100.00 MB)
Download: [========================================>] 100.0% | 100.00 MB/100.00 MB | 45.2 MB/s | ETA: 0s
Download completed in 2.21s
Average speed: 45.2 MB/s
Verifying checksum... OK
File saved to: 100MB.bin
```

### 2. Resume Download

```bash
# Start download
./bin/file_transfer_client download https://127.0.0.1:7006/100MB.bin ./partial.bin

# Interrupt it (Ctrl+C)

# Resume download - automatically detects existing file
./bin/file_transfer_client download https://127.0.0.1:7006/100MB.bin ./partial.bin
```

Output:
```
Resuming download from 45678912 bytes (43.56 MB)
Download: [========================================>] 100.0% | 100.00 MB/100.00 MB | 42.1 MB/s | ETA: 0s
Download completed in 1.34s (resumed from 43.56 MB)
```

### 3. File Upload (Streaming)

```bash
# Upload a file
./bin/file_transfer_client upload https://127.0.0.1:7006/upload/myfile.bin ./localfile.bin
```

Output:
```
Uploading: ./localfile.bin
File size: 52428800 bytes (50.00 MB)
Upload: [========================================>] 100.0% | 50.00 MB/50.00 MB | 38.5 MB/s | ETA: 0s
Upload completed in 1.30s
Average speed: 38.5 MB/s
Server checksum: 12345678901234567890
```

## Implementation Details

### Server

- **Streaming Response**: Uses `SetResponseBodyProvider` to stream file content
- **Streaming Request**: Uses `IAsyncServerHandler` to receive upload chunks
- Supports HTTP Range requests (RFC 7233)
- Sends checksum in `X-Checksum` header
- Security: Prevents directory traversal attacks

### Client

- **Streaming Download**: Uses `IAsyncClientHandler` to process response chunks
- **Streaming Upload**: Uses `SetRequestBodyProvider` to send file chunks
- Automatically detects and resumes partial downloads
- Real-time progress bar with speed and ETA
- Verifies checksum after download

## Memory Efficiency

Unlike traditional approaches that buffer entire files in memory, the streaming APIs allow:

| Operation | Traditional | Streaming |
|-----------|------------|-----------|
| 1GB Download | ~1GB RAM | ~64KB RAM |
| 1GB Upload | ~1GB RAM | ~64KB RAM |

This makes it possible to transfer very large files even on memory-constrained devices.

## Error Handling

The client handles various error scenarios:

- **Network interruption** - Progress is saved, can be resumed
- **Server unavailable** - Clear error message
- **Disk full** - Graceful error handling
- **Checksum mismatch** - Warns user and keeps file for inspection
