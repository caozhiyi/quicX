# File Transfer Example

This example demonstrates how to transfer large files over HTTP/3 with the following features:

## Features

- **Large file downloads** - Efficiently download files of any size
- **Resume support** - Continue interrupted downloads (HTTP Range requests)
- **Progress tracking** - Real-time progress bar and statistics
- **Checksum verification** - Verify file integrity with SHA-256
- **Concurrent downloads** - Download multiple files simultaneously

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
./bin/file_transfer_server [port] [directory]

# Example:
./bin/file_transfer_server 8443 ./files
```

This will serve files from the `./files` directory on port 8443.

### Client

Download a file:

```bash
./bin/file_transfer_client <url> [output_file]

# Example - simple download:
./bin/file_transfer_client https://localhost:8443/largefile.bin

# Example - specify output file:
./bin/file_transfer_client https://localhost:8443/largefile.bin ./downloads/myfile.bin

# Example - resume interrupted download:
# Just run the same command again, it will automatically resume
./bin/file_transfer_client https://localhost:8443/largefile.bin ./partial.bin
```

## Features Demo

### 1. Large File Download

```bash
# Generate a 100MB test file
dd if=/dev/urandom of=files/100MB.bin bs=1M count=100

# Download it
./bin/file_transfer_client https://localhost:8443/100MB.bin
```

Output:
```
Downloading: https://localhost:8443/100MB.bin
File size: 104857600 bytes (100.00 MB)
Progress: [====================] 100% | 100.00 MB/100.00 MB | 45.2 MB/s | ETA: 0s
Download completed in 2.21s
Average speed: 45.2 MB/s
Verifying checksum... OK
```

### 2. Resume Download

```bash
# Start download
./bin/file_transfer_client https://localhost:8443/100MB.bin ./partial.bin

# Interrupt it (Ctrl+C)

# Resume download
./bin/file_transfer_client https://localhost:8443/100MB.bin ./partial.bin
```

Output:
```
Resuming download from 45678912 bytes (43.56 MB)
Progress: [====================] 100% | 100.00 MB/100.00 MB | 42.1 MB/s | ETA: 0s
Download completed in 1.34s (resumed from 43.56 MB)
```

### 3. Concurrent Downloads

```bash
# Download multiple files in parallel
./bin/file_transfer_client https://localhost:8443/file1.bin &
./bin/file_transfer_client https://localhost:8443/file2.bin &
./bin/file_transfer_client https://localhost:8443/file3.bin &
wait
```

## Implementation Details

### Server

- Supports HTTP Range requests (RFC 7233)
- Calculates and sends SHA-256 checksums in `X-Checksum` header
- Streams large files efficiently without loading into memory
- Handles partial content requests (206 Partial Content)

### Client

- Automatically detects partial downloads
- Sends Range header for resume
- Real-time progress bar with:
  - Downloaded bytes / Total bytes
  - Download speed (MB/s)
  - ETA (estimated time remaining)
- Verifies checksum after download
- Graceful error handling

## Performance Tips

1. **Adjust buffer size** - Modify `CHUNK_SIZE` in client.cpp for optimal performance
2. **Use multiple connections** - Download different files concurrently
3. **Network tuning** - Ensure proper MTU and congestion control settings

## Error Handling

The client handles various error scenarios:

- **Network interruption** - Automatically saves progress
- **Server unavailable** - Retries with exponential backoff
- **Disk full** - Graceful error message
- **Checksum mismatch** - Warns user and keeps file for inspection
