# QLog Integration Example

This example demonstrates how to enable and use qlog (QUIC Log) in quicX applications.

## Overview

qlog is a standardized logging format for QUIC and HTTP/3. It allows developers to visualize connection flow, packet exchanges, and congestion control states using tools like [qvis](https://qvis.quictools.info/).

This example includes:
- `server.cpp`: A minimal HTTP/3 server with QLog enabled.
- `client.cpp`: A minimal HTTP/3 client with QLog enabled.

## Code Explanation

To enable QLog, you simply configure it through the `Http3Config` (or `Http3ServerConfig`) when initializing your client or server.

### Server Side

```cpp
#include "http3/include/if_server.h"

// ...

quicx::Http3ServerConfig config;
// ... other config ...

// Enable QLog
config.config_.qlog_config_.enabled = true;
config.config_.qlog_config_.output_dir = "./qlog_output";
config.config_.qlog_config_.flush_interval_ms = 100;

server->Init(config);
```

### Client Side

```cpp
#include "http3/include/if_client.h"

// ...

quicx::Http3Config config;
// ... other config ...

// Enable QLog
config.qlog_config_.enabled = true;
config.qlog_config_.output_dir = "./qlog_output_client";

client->Init(config);
```

This streamlined approach ensures qlog is initialized as part of the standard server/client lifecycle.

## Running the Example

1. **Build the example**:
   ```bash
   cd build
   make qlog_server qlog_client
   ```

2. **Start the server**:
   ```bash
   ./bin/qlog_server
   ```
   The server will indicate that QLog is enabled and listening on port 7011.

3. **Run the client** (in a new terminal):
   ```bash
   ./bin/qlog_client
   ```

4. **View Logs**:
   - Server logs will be in `qlog_output/`
   - Client logs will be in `qlog_output_client/`
   
   The file names will be `<GroupID>_<ConnectionID>.sqlog`.

5. **Analyze**:
   Upload the generated `.sqlog` files to [qvis.quictools.info](https://qvis.quictools.info/) to visualize the connection.
