# RESTful API 示例

本示例演示如何使用 QuicX HTTP/3 库构建完整的 RESTful API 服务。

## 功能演示

### 服务器端功能
- ✅ **完整的 CRUD 操作** - GET, POST, PUT, DELETE 方法
- ✅ **JSON 请求/响应** - JSON 数据解析和格式化
- ✅ **路径参数** - 从 URL 路径中提取参数（如 `/users/:id`）
- ✅ **自定义头部** - 设置和读取 HTTP 头部
- ✅ **状态码** - 正确使用 HTTP 状态码（200, 201, 204, 400, 404）
- ✅ **中间件** - 请求日志和响应处理
- ✅ **CORS 支持** - 跨域资源共享头部
- ✅ **线程安全** - 使用互斥锁保护的内存数据库

### 客户端功能
- ✅ **多种 HTTP 方法** - GET, POST, PUT, DELETE 请求
- ✅ **自定义头部** - 为请求添加头部
- ✅ **异步回调** - 非阻塞请求处理
- ✅ **错误处理** - 处理不同的状态码和错误
- ✅ **顺序测试** - 全面的 API 测试工作流

## API 端点

| 方法 | 路径 | 描述 |
|------|------|------|
| GET | `/users` | 获取所有用户 |
| GET | `/users/:id` | 根据 ID 获取单个用户 |
| POST | `/users` | 创建新用户 |
| PUT | `/users/:id` | 更新现有用户 |
| DELETE | `/users/:id` | 删除用户 |
| GET | `/stats` | 获取服务器统计信息 |

## 数据模型

```json
{
  "id": 1,
  "name": "Alice",
  "email": "alice@example.com",
  "age": 25
}
```

## 构建

### 使用 CMake（从项目根目录）

```bash
# 配置
mkdir -p build && cd build
cmake ..

# 构建
make restful_api_server restful_api_client

# 或构建所有示例
make
```

可执行文件将位于 `build/bin/` 目录：
- `restful_api_server`
- `restful_api_client`

### 使用 Make（从示例目录）

```bash
cd example/restful_api
make
```

## 使用方法

### 1. 启动服务器

```bash
./build/bin/restful_api_server
```

输出：
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

### 2. 运行客户端（在另一个终端）

```bash
./build/bin/restful_api_client
```

客户端将运行一系列测试：

1. **GET 所有用户** - 检索初始用户列表
2. **GET 单个用户** - 获取 ID 为 1 的用户
3. **POST 新用户** - 创建名为 "David" 的新用户
4. **PUT 更新用户** - 更新 ID 为 2 的用户
5. **GET 更新后的用户** - 验证更新
6. **GET 统计信息** - 检查服务器统计
7. **DELETE 用户** - 删除 ID 为 3 的用户
8. **GET 已删除用户** - 尝试获取已删除的用户（预期 404）
9. **GET 所有用户** - 查看最终状态
10. **错误处理** - 测试无效 ID 处理

### 3. 使用 curl 手动测试

你也可以使用 curl 手动测试 API（注意：如果你的 curl 支持，可能需要使用 `--http3` 标志）：

```bash
# 获取所有用户
curl -k https://127.0.0.1:8883/users

# 获取单个用户
curl -k https://127.0.0.1:8883/users/1

# 创建新用户
curl -k -X POST https://127.0.0.1:8883/users \
  -H "Content-Type: application/json" \
  -d '{"name":"Eve","email":"eve@example.com","age":27}'

# 更新用户
curl -k -X PUT https://127.0.0.1:8883/users/1 \
  -H "Content-Type: application/json" \
  -d '{"name":"Alice Smith","email":"alice.smith@example.com","age":26}'

# 删除用户
curl -k -X DELETE https://127.0.0.1:8883/users/2

# 获取统计信息
curl -k https://127.0.0.1:8883/stats
```

## 代码亮点

### 服务器：路由处理器

```cpp
// GET /users - 获取所有用户
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

### 服务器：中间件

```cpp
// 日志中间件 - 在所有处理器之前运行
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

### 客户端：发起请求

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

## 实现细节

### 服务器架构

1. **UserDatabase 类**：使用 `std::map` 和 `std::mutex` 的线程安全内存存储
2. **JSON 辅助函数**：简单的 JSON 序列化/反序列化（生产环境中使用专业的 JSON 库，如 nlohmann/json）
3. **中间件链**：请求日志（之前）和 CORS 头部（之后）
4. **路径参数提取**：自定义函数从 URL 中提取 ID

### 客户端架构

1. **原子计数器**：跟踪待处理请求以实现同步
2. **顺序测试**：每个测试等待前一个完成
3. **全面覆盖**：测试所有 CRUD 操作和错误情况

## 生产环境注意事项

这是一个演示示例。在生产环境中，请考虑：

1. **JSON 库**：使用专业的 JSON 库（如 nlohmann/json、RapidJSON）
2. **数据库**：用真实数据库替换内存存储
3. **验证**：添加输入验证和清理
4. **认证**：实现适当的认证（JWT、OAuth 等）
5. **错误处理**：更健壮的错误处理和日志记录
6. **速率限制**：防止滥用
7. **监控**：添加指标和健康检查端点
8. **TLS 证书**：使用正式证书（非自签名）

## 学习资源

### HTTP/3 概念
- 多路复用：单个连接上的多个请求
- QPACK：头部压缩
- 服务器推送：主动资源传递

### RESTful API 设计
- 使用适当的 HTTP 方法
- 正确的状态码
- 基于资源的 URL
- 无状态通信

## 故障排除

### 服务器无法启动
- 检查端口 8883 是否已被占用：`lsof -i :8883`
- 确保你有适当的权限

### 客户端连接错误
- 确保服务器正在运行
- 示例使用自签名证书，因此 SSL 验证被禁用
- 检查防火墙设置

### 构建错误
- 确保 QuicX 库已构建：`make quicx http3`
- 检查 CMake 配置

## 下一步

理解本示例后，可以探索：
- **文件传输** - 上传/下载大文件
- **流式传输** - 实时数据流
- **HTTP/3 上的 WebSocket** - 双向通信
- **负载均衡** - 多个服务器实例

## 许可证

本示例是 QuicX 项目的一部分。详见主 LICENSE 文件。

