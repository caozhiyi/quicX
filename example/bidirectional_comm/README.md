# Bidirectional Communication Example

WebSocket-like bidirectional communication over HTTP/3.

## Features

- **Real-time messaging** - Send and receive messages asynchronously
- **Heartbeat mechanism** - Keep connection alive
- **Auto-reconnect** - Automatic reconnection on failure
- **Message queue** - Buffer messages during disconnection

## Building

```bash
cd build && cmake .. && make bidirectional_server bidirectional_client
```

## Usage

### Server

```bash
./bin/bidirectional_server [port]
```

### Client

```bash
./bin/bidirectional_client <server_url>

# Example:
./bin/bidirectional_client https://localhost:8443
```

## Use Cases

- Real-time chat applications
- Game servers
- Live data feeds
- Collaborative editing
- IoT device communication

## Example Output

```
Bidirectional Client
====================
Connected to: https://localhost:8443
Heartbeat started (interval: 5s)

> Hello Server!
[Server]: Message received: Hello Server!
[Server]: Echo: Hello Server!

> How are you?
[Server]: Message received: How are you?
[Server]: Echo: How are you?

Connection lost! Reconnecting...
Reconnected successfully!
```
