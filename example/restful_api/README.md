# RESTful API Example

This example demonstrates how to build a complete RESTful API service using the QuicX HTTP/3 library.

## Features Demonstrated

### Server-Side Features
- ✅ **Full CRUD Operations** - GET, POST, PUT, DELETE methods
- ✅ **JSON Request/Response** - JSON data parsing and formatting
- ✅ **Path Parameters** - Extract parameters from URL paths (e.g., `/users/:id`)
- ✅ **Custom Headers** - Set and read HTTP headers
- ✅ **Status Codes** - Proper HTTP status code usage (200, 201, 204, 400, 404)
- ✅ **Middleware** - Request logging and response processing
- ✅ **CORS Support** - Cross-Origin Resource Sharing headers
- ✅ **Thread-Safe** - In-memory database with mutex protection

### Client-Side Features
- ✅ **Multiple HTTP Methods** - GET, POST, PUT, DELETE requests
- ✅ **Custom Headers** - Add headers to requests
- ✅ **Async Callbacks** - Non-blocking request handling
- ✅ **Error Handling** - Handle different status codes and errors
- ✅ **Sequential Testing** - Comprehensive API testing workflow

## API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/users` | Get all users |
| GET | `/users/:id` | Get single user by ID |
| POST | `/users` | Create a new user |
| PUT | `/users/:id` | Update existing user |
| DELETE | `/users/:id` | Delete user |
| GET | `/stats` | Get server statistics |

## Data Model

```json
{
  "id": 1,
  "name": "Alice",
  "email": "alice@example.com",
  "age": 25
}
```

## Build

### Using CMake (from project root)

```bash
# Configure
mkdir -p build && cd build
cmake ..

# Build
make restful_api_server restful_api_client

# Or build all examples
make
```

The executables will be in `build/bin/`:
- `restful_api_server`
- `restful_api_client`

### Using Make (from example directory)

```bash
cd example/restful_api
make
```

## Usage

### 1. Start the Server

```bash
./build/bin/restful_api_server
```

Output:
```
==================================
RESTful API Server Starting...
==================================
Listen on: https://0.0.0.0:8883

Available endpoints:
  GET    /users       - Get all users
  GET    /users/:id   - Get single user
  POST   /users       - Create new user
  PUT    /users/:id   - Update user
  DELETE /users/:id   - Delete user
  GET    /stats       - Get statistics
==================================
```

### 2. Run the Client (in another terminal)

```bash
./build/bin/restful_api_client
```

The client will run through a series of tests:

1. **GET all users** - Retrieve the initial user list
2. **GET single user** - Get user with ID 1
3. **POST new user** - Create a new user named "David"
4. **PUT update user** - Update user with ID 2
5. **GET updated user** - Verify the update
6. **GET statistics** - Check server stats
7. **DELETE user** - Delete user with ID 3
8. **GET deleted user** - Try to get deleted user (404 expected)
9. **GET all users** - View final state
10. **Error handling** - Test invalid ID handling

### 3. Manual Testing with curl

You can also test the API manually using curl (note: you may need to use `--http3` flag if your curl supports it):

```bash
# Get all users
curl -k https://127.0.0.1:8883/users

# Get single user
curl -k https://127.0.0.1:8883/users/1

# Create new user
curl -k -X POST https://127.0.0.1:8883/users \
  -H "Content-Type: application/json" \
  -d '{"name":"Eve","email":"eve@example.com","age":27}'

# Update user
curl -k -X PUT https://127.0.0.1:8883/users/1 \
  -H "Content-Type: application/json" \
  -d '{"name":"Alice Smith","email":"alice.smith@example.com","age":26}'

# Delete user
curl -k -X DELETE https://127.0.0.1:8883/users/2

# Get statistics
curl -k https://127.0.0.1:8883/stats
```

## Code Highlights

### Server: Route Handler

```cpp
// GET /users - Get all users
server->AddHandler(
    quicx::http3::HttpMethod::kGet,
    "/users",
    [db](std::shared_ptr<quicx::http3::IRequest> req, 
         std::shared_ptr<quicx::http3::IResponse> resp) {
        auto users = db->GetAllUsers();
        std::string json = UsersToJson(users);
        
        resp->AddHeader("Content-Type", "application/json");
        resp->SetBody(json);
        resp->SetStatusCode(200);
    }
);
```

### Server: Middleware

```cpp
// Logging middleware - runs before all handlers
server->AddMiddleware(
    quicx::http3::HttpMethod::kAny,
    quicx::http3::MiddlewarePosition::kBefore,
    [](std::shared_ptr<quicx::http3::IRequest> req, 
       std::shared_ptr<quicx::http3::IResponse> resp) {
        std::cout << "[" << req->GetMethodString() << "] " 
                  << req->GetPath() << std::endl;
    }
);
```

### Client: Making Requests

```cpp
auto request = quicx::http3::IRequest::Create();
request->AddHeader("Content-Type", "application/json");
request->SetBody("{\"name\":\"David\",\"email\":\"david@example.com\",\"age\":28}");

client->DoRequest(
    "https://127.0.0.1:8883/users",
    quicx::http3::HttpMethod::kPost,
    request,
    [](std::shared_ptr<quicx::http3::IResponse> response, uint32_t error) {
        if (error == 0) {
            std::cout << "Status: " << response->GetStatusCode() << std::endl;
            std::cout << "Response: " << response->GetBody() << std::endl;
        }
    }
);
```

## Implementation Details

### Server Architecture

1. **UserDatabase Class**: Thread-safe in-memory storage using `std::map` and `std::mutex`
2. **JSON Helpers**: Simple JSON serialization/deserialization (for production, use a proper JSON library like nlohmann/json)
3. **Middleware Chain**: Request logging (before) and CORS headers (after)
4. **Path Parameter Extraction**: Custom function to extract IDs from URLs

### Client Architecture

1. **Atomic Counter**: Track pending requests for synchronization
2. **Sequential Testing**: Each test waits for previous one to complete
3. **Comprehensive Coverage**: Tests all CRUD operations and error cases

## Production Considerations

This is a demonstration example. For production use, consider:

1. **JSON Library**: Use a proper JSON library (e.g., nlohmann/json, RapidJSON)
2. **Database**: Replace in-memory storage with a real database
3. **Validation**: Add input validation and sanitization
4. **Authentication**: Implement proper authentication (JWT, OAuth, etc.)
5. **Error Handling**: More robust error handling and logging
6. **Rate Limiting**: Protect against abuse
7. **Monitoring**: Add metrics and health check endpoints
8. **TLS Certificates**: Use proper certificates (not self-signed)

## Learning Resources

### HTTP/3 Concepts
- Multiplexing: Multiple requests over single connection
- QPACK: Header compression
- Server Push: Proactive resource delivery

### RESTful API Design
- Use appropriate HTTP methods
- Proper status codes
- Resource-based URLs
- Stateless communication

## Troubleshooting

### Server won't start
- Check if port 8883 is already in use: `lsof -i :8883`
- Ensure you have proper permissions

### Client connection errors
- Make sure the server is running
- The example uses self-signed certificates, so SSL verification is disabled
- Check firewall settings

### Build errors
- Ensure QuicX library is built: `make quicx http3`
- Check CMake configuration

## Next Steps

After understanding this example, explore:
- **File Transfer** - Upload/download large files
- **Streaming** - Real-time data streaming
- **WebSocket over HTTP/3** - Bidirectional communication
- **Load Balancing** - Multiple server instances

## License

This example is part of the QuicX project. See the main LICENSE file for details.

