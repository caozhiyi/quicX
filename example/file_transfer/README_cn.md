# 文件传输示例

本示例演示如何使用 QuicX HTTP/3 库实现文件上传和下载功能。展示了大文件传输能力、multipart 表单数据处理和流式操作。

## 功能演示

### 服务器端功能
- ✅ **文件上传** - 处理 multipart/form-data 和直接二进制上传
- ✅ **文件下载** - 向客户端流式传输文件，带有适当的头部
- ✅ **文件管理** - 列出、删除带有元数据的文件
- ✅ **Multipart 表单数据** - 解析 multipart/form-data 请求
- ✅ **Content-Type 处理** - 正确的 MIME 类型检测和处理
- ✅ **文件存储** - 带元数据跟踪的持久化文件存储
- ✅ **统计信息** - 跟踪文件总数、存储大小
- ✅ **线程安全** - 使用互斥锁保护的并发文件操作

### 客户端功能
- ✅ **文件上传** - 使用 multipart/form-data 编码上传文件
- ✅ **文件下载** - 下载并保存文件到本地
- ✅ **命令行界面** - 易于使用的文件操作 CLI
- ✅ **进度跟踪** - 以人类可读格式显示文件大小
- ✅ **自动化测试** - 带有全面测试的演示模式
- ✅ **文件列表** - 查看服务器上所有可用文件
- ✅ **统计信息** - 查询服务器存储统计

## API 端点

| 方法 | 路径 | 描述 |
|------|------|------|
| GET | `/` | 带有 API 文档的欢迎页面 |
| GET | `/files` | 列出所有上传的文件及元数据 |
| GET | `/files/:filename` | 下载指定文件 |
| POST | `/upload` | 上传文件（multipart 或二进制） |
| DELETE | `/files/:filename` | 删除文件 |
| GET | `/stats` | 获取服务器统计信息（文件数、总大小） |

## 构建

### 使用 CMake（从项目根目录）

```bash
# 配置
mkdir -p build && cd build
cmake ..

# 构建
make file_transfer_server file_transfer_client

# 或构建所有示例
make
```

可执行文件将位于 `build/bin/`：
- `file_transfer_server`
- `file_transfer_client`

### 使用 Make（从示例目录）

```bash
cd example/file_transfer
make
```

## 使用方法

### 1. 启动服务器

```bash
./build/bin/file_transfer_server
```

输出：
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

服务器将为上传的文件创建 `file_storage/` 目录。

### 2. 运行客户端

#### 演示模式（自动化测试）

不带参数运行以执行自动化演示：

```bash
./build/bin/file_transfer_client
```

这将：
1. 创建测试文件（小、中、大）
2. 上传所有测试文件
3. 列出服务器上的文件
4. 获取服务器统计信息
5. 下载一个文件
6. 删除一个文件
7. 验证最终状态

#### 命令行模式

上传文件：
```bash
./build/bin/file_transfer_client upload myfile.txt
```

下载文件：
```bash
./build/bin/file_transfer_client download myfile.txt
# 或指定自定义名称
./build/bin/file_transfer_client download myfile.txt saved_file.txt
```

列出所有文件：
```bash
./build/bin/file_transfer_client list
```

删除文件：
```bash
./build/bin/file_transfer_client delete myfile.txt
```

获取统计信息：
```bash
./build/bin/file_transfer_client stats
```

### 3. 使用 curl 测试

上传文件（multipart）：
```bash
curl -k -X POST https://127.0.0.1:8884/upload \
  -F "file=@myfile.txt"
```

上传文件（二进制，自定义名称）：
```bash
curl -k -X POST https://127.0.0.1:8884/upload \
  -H "X-Filename: myfile.txt" \
  -H "Content-Type: text/plain" \
  --data-binary @myfile.txt
```

下载文件：
```bash
curl -k https://127.0.0.1:8884/files/myfile.txt -o downloaded.txt
```

列出文件：
```bash
curl -k https://127.0.0.1:8884/files
```

删除文件：
```bash
curl -k -X DELETE https://127.0.0.1:8884/files/myfile.txt
```

获取统计信息：
```bash
curl -k https://127.0.0.1:8884/stats
```

## 代码亮点

### 服务器：Multipart 表单数据解析

```cpp
bool ParseMultipartFormData(const std::string& body, 
                           const std::string& boundary, 
                           std::vector<MultipartPart>& parts) {
    // 用于文件上传的自定义 multipart 解析器
    // 提取文件名、content-type 和文件内容
}
```

### 服务器：文件上传处理器

```cpp
server->AddHandler(
    quicx::http3::HttpMethod::kPost,
    "/upload",
    [storage](std::shared_ptr<quicx::http3::IRequest> req, 
              std::shared_ptr<quicx::http3::IResponse> resp) {
        // 解析 multipart/form-data
        // 保存文件到存储
        // 返回带有元数据的上传确认
    }
);
```

### 服务器：文件下载处理器

```cpp
server->AddHandler(
    quicx::http3::HttpMethod::kGet,
    "/files/:filename",
    [storage](std::shared_ptr<quicx::http3::IRequest> req, 
              std::shared_ptr<quicx::http3::IResponse> resp) {
        // 从存储加载文件
        // 设置 Content-Disposition 头部
        // 向客户端流式传输文件内容
    }
);
```

### 客户端：文件上传

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
        // 处理上传响应
    }
);
```

### 客户端：文件下载

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

## 实现细节

### 文件存储

服务器使用简单的文件存储系统：
- **存储目录**：`./file_storage/`（自动创建）
- **元数据跟踪**：内存中的映射，包含文件信息（名称、大小、上传时间、内容类型）
- **线程安全**：所有操作都受互斥锁保护
- **持久化**：文件存储在磁盘上，元数据在内存中

### Multipart 表单数据

示例包含自定义 multipart/form-data 解析器，可以：
- 从 Content-Type 头部提取边界
- 从请求体解析多个部分
- 从头部提取文件名和 content-type
- 支持二进制文件内容

### 文件大小格式化

人类可读的文件大小显示：
- 自动将字节转换为 KB、MB、GB
- 两位小数精度
- 在客户端和服务器输出中使用

## 性能考虑

### 大文件处理

目前，整个文件被加载到内存中。对于生产环境中的非常大的文件：
- 实现分块传输编码
- 使用流式 I/O
- 设置适当的缓冲区大小
- 考虑对非常大的文件使用内存映射文件

### 并发上传

服务器支持并发文件上传：
- 线程安全的文件存储操作
- 配置多个工作线程
- 共享数据结构的互斥锁保护

## 生产环境注意事项

在生产环境中使用时，应增强示例：

1. **安全性**
   - 验证文件类型和大小
   - 清理文件名（防止路径遍历）
   - 实现认证/授权
   - 对上传的文件进行病毒扫描
   - 速率限制

2. **存储**
   - 使用数据库存储元数据
   - 对大文件实现文件分块
   - 添加压缩支持
   - 实现文件去重
   - 每用户配额管理

3. **功能**
   - 恢复中断的上传/下载
   - 大文件的进度回调
   - 文件预览/缩略图生成
   - 批量操作
   - 搜索和过滤

4. **可靠性**
   - 持久化元数据存储
   - 文件操作的事务支持
   - 错误恢复
   - 备份和恢复

5. **性能**
   - 缓存常访问的文件
   - CDN 集成
   - 负载均衡
   - 压缩（gzip、brotli）

## 示例输出

### 服务器输出

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

### 客户端输出（演示模式）

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

## 故障排除

### 上传失败
- 检查文件权限
- 验证存储目录存在且可写
- 检查文件大小限制
- 查看 Content-Type 头部

### 下载失败
- 确保文件在服务器上存在
- 检查文件名（区分大小写）
- 验证网络连接

### 构建错误
- 确保 QuicX 库已构建
- 检查 C++14 或更高版本编译器支持
- 验证 CMake 配置

## 下一步

理解本示例后，可以探索：
- **流式传输** - 实时数据流
- **分块传输** - 带有进度的大文件处理
- **文件加密** - 存储前加密文件
- **云存储集成** - S3、Google Cloud Storage 等

## 许可证

本示例是 QuicX 项目的一部分。详见主 LICENSE 文件。

