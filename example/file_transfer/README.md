# File Transfer Example

This example demonstrates how to implement file upload and download functionality using the QuicX HTTP/3 library. It showcases large file transfer capabilities, multipart form data handling, and streaming operations.

## Features Demonstrated

### Server-Side Features
- ✅ **File Upload** - Handle multipart/form-data and direct binary uploads
- ✅ **File Download** - Stream files to clients with proper headers
- ✅ **File Management** - List, delete files with metadata
- ✅ **Multipart Form Data** - Parse multipart/form-data requests
- ✅ **Content-Type Handling** - Proper MIME type detection and handling
- ✅ **File Storage** - Persistent file storage with metadata tracking
- ✅ **Statistics** - Track total files, storage size
- ✅ **Thread-Safe** - Concurrent file operations with mutex protection

### Client-Side Features
- ✅ **File Upload** - Upload files with multipart/form-data encoding
- ✅ **File Download** - Download and save files locally
- ✅ **Command-Line Interface** - Easy-to-use CLI for file operations
- ✅ **Progress Tracking** - Display file sizes in human-readable format
- ✅ **Automated Testing** - Demo mode with comprehensive tests
- ✅ **File List** - View all available files on server
- ✅ **Statistics** - Query server storage statistics

## API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | Welcome page with API documentation |
| GET | `/files` | List all uploaded files with metadata |
| GET | `/files/:filename` | Download a specific file |
| POST | `/upload` | Upload a file (multipart or binary) |
| DELETE | `/files/:filename` | Delete a file |
| GET | `/stats` | Get server statistics (file count, total size) |

## Build

### Using CMake (from project root)

```bash
# Configure
mkdir -p build && cd build
cmake ..

# Build
make file_transfer_server file_transfer_client

# Or build all examples
make
```

The executables will be in `build/bin/`:
- `file_transfer_server`
- `file_transfer_client`

### Using Make (from example directory)

```bash
cd example/file_transfer
make
```

## Usage

### 1. Start the Server

```bash
./build/bin/file_transfer_server
```

Output:
```
==================================
File Transfer Server Starting...
==================================
Listen on: https://0.0.0.0:8884
Storage directory: ./file_storage/

Available endpoints:
  GET    /              - Welcome page
  GET    /files         - List all files
  GET    /files/:name   - Download file
  POST   /upload        - Upload file
  DELETE /files/:name   - Delete file
  GET    /stats         - Server statistics
==================================
```

The server will create a `file_storage/` directory for uploaded files.

### 2. Run the Client

#### Demo Mode (Automated Tests)

Run without arguments to execute the automated demo:

```bash
./build/bin/file_transfer_client
```

This will:
1. Create test files (small, medium, large)
2. Upload all test files
3. List files on server
4. Get server statistics
5. Download a file
6. Delete a file
7. Verify final state

#### Command-Line Mode

Upload a file:
```bash
./build/bin/file_transfer_client upload myfile.txt
```

Download a file:
```bash
./build/bin/file_transfer_client download myfile.txt
# Or specify custom name
./build/bin/file_transfer_client download myfile.txt saved_file.txt
```

List all files:
```bash
./build/bin/file_transfer_client list
```

Delete a file:
```bash
./build/bin/file_transfer_client delete myfile.txt
```

Get statistics:
```bash
./build/bin/file_transfer_client stats
```

### 3. Testing with curl

Upload a file (multipart):
```bash
curl -k -X POST https://127.0.0.1:8884/upload \
  -F "file=@myfile.txt"
```

Upload a file (binary with custom name):
```bash
curl -k -X POST https://127.0.0.1:8884/upload \
  -H "X-Filename: myfile.txt" \
  -H "Content-Type: text/plain" \
  --data-binary @myfile.txt
```

Download a file:
```bash
curl -k https://127.0.0.1:8884/files/myfile.txt -o downloaded.txt
```

List files:
```bash
curl -k https://127.0.0.1:8884/files
```

Delete a file:
```bash
curl -k -X DELETE https://127.0.0.1:8884/files/myfile.txt
```

Get statistics:
```bash
curl -k https://127.0.0.1:8884/stats
```

## Code Highlights

### Server: Multipart Form Data Parsing

```cpp
bool ParseMultipartFormData(const std::string& body, 
                           const std::string& boundary, 
                           std::vector<MultipartPart>& parts) {
    // Custom multipart parser for file uploads
    // Extracts filename, content-type, and file content
}
```

### Server: File Upload Handler

```cpp
server->AddHandler(
    quicx::http3::HttpMethod::kPost,
    "/upload",
    [storage](std::shared_ptr<quicx::http3::IRequest> req, 
              std::shared_ptr<quicx::http3::IResponse> resp) {
        // Parse multipart/form-data
        // Save file to storage
        // Return upload confirmation with metadata
    }
);
```

### Server: File Download Handler

```cpp
server->AddHandler(
    quicx::http3::HttpMethod::kGet,
    "/files/:filename",
    [storage](std::shared_ptr<quicx::http3::IRequest> req, 
              std::shared_ptr<quicx::http3::IResponse> resp) {
        // Load file from storage
        // Set Content-Disposition header
        // Stream file content to client
    }
);
```

### Client: File Upload

```cpp
std::string boundary = GenerateMultipartBoundary();
std::string body = CreateMultipartBody(filename, content, boundary);

auto request = quicx::http3::IRequest::Create();
request->AddHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
request->SetBody(body);

client->DoRequest(
    "https://127.0.0.1:8884/upload",
    quicx::http3::HttpMethod::kPost,
    request,
    [](std::shared_ptr<quicx::http3::IResponse> response, uint32_t error) {
        // Handle upload response
    }
);
```

### Client: File Download

```cpp
client->DoRequest(
    "https://127.0.0.1:8884/files/" + filename,
    quicx::http3::HttpMethod::kGet,
    request,
    [](std::shared_ptr<quicx::http3::IResponse> response, uint32_t error) {
        if (response->GetStatusCode() == 200) {
            WriteLocalFile(output_file, response->GetBody());
        }
    }
);
```

## Implementation Details

### File Storage

The server uses a simple file storage system:
- **Storage Directory**: `./file_storage/` (created automatically)
- **Metadata Tracking**: In-memory map with file info (name, size, upload time, content type)
- **Thread Safety**: All operations protected by mutex
- **Persistence**: Files stored on disk, metadata in memory

### Multipart Form Data

The example includes a custom multipart/form-data parser that:
- Extracts boundary from Content-Type header
- Parses multiple parts from request body
- Extracts filename and content-type from headers
- Supports binary file content

### File Size Formatting

Human-readable file size display:
- Automatically converts bytes to KB, MB, GB
- Two decimal precision
- Used in both client and server output

## Performance Considerations

### Large File Handling

Currently, the entire file is loaded into memory. For very large files in production:
- Implement chunked transfer encoding
- Use streaming I/O
- Set appropriate buffer sizes
- Consider memory-mapped files for very large files

### Concurrent Uploads

The server supports concurrent file uploads:
- Thread-safe file storage operations
- Multiple worker threads configured
- Mutex protection for shared data structures

## Production Considerations

For production use, enhance the example with:

1. **Security**
   - Validate file types and sizes
   - Sanitize filenames (prevent path traversal)
   - Implement authentication/authorization
   - Virus scanning for uploaded files
   - Rate limiting

2. **Storage**
   - Use a database for metadata
   - Implement file chunking for large files
   - Add compression support
   - Implement file deduplication
   - Quota management per user

3. **Features**
   - Resume interrupted uploads/downloads
   - Progress callbacks for large files
   - File preview/thumbnail generation
   - Batch operations
   - Search and filtering

4. **Reliability**
   - Persistent metadata storage
   - Transaction support for file operations
   - Error recovery
   - Backup and restore

5. **Performance**
   - Caching frequently accessed files
   - CDN integration
   - Load balancing
   - Compression (gzip, brotli)

## Example Output

### Server Output

```
[POST] /upload
  -> Uploaded: test_medium.txt (100.00 KB)
[GET] /files
  -> Returned 3 files
[GET] /files/test_medium.txt
  -> Downloaded: test_medium.txt (100.00 KB)
[DELETE] /files/test_small.txt
  -> Deleted: test_small.txt
```

### Client Output (Demo Mode)

```
Step 2: Upload small file
----------------------------------
Uploading: test_small.txt (10.00 KB)
  Status: 201
  Response: {"message":"File uploaded successfully","filename":"test_small.txt","size":10240,"size_formatted":"10.00 KB"}

Step 5: List all files
----------------------------------
Listing files...
  Status: 200
  Files: [{"name":"test_small.txt","size":10240,"size_formatted":"10.00 KB","upload_time":"2025-10-09 20:30:15","content_type":"application/octet-stream"}]
```

## Troubleshooting

### Upload Fails
- Check file permissions
- Verify storage directory exists and is writable
- Check file size limits
- Review Content-Type header

### Download Fails
- Ensure file exists on server
- Check filename (case-sensitive)
- Verify network connectivity

### Build Errors
- Ensure QuicX library is built
- Check C++14 or later compiler support
- Verify CMake configuration

## Next Steps

After understanding this example, explore:
- **Streaming** - Real-time data streaming
- **Chunked Transfer** - Large file handling with progress
- **File Encryption** - Encrypt files before storage
- **Cloud Storage Integration** - S3, Google Cloud Storage, etc.

## License

This example is part of the QuicX project. See the main LICENSE file for details.

