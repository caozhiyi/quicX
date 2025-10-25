# QuicX

<p align="left"><img width="500" src="./doc/image/logo.png" alt="cppnet logo"></p>

<p align="left">
    <a href="https://opensource.org/licenses/BSD-3-Clause"><img src="https://img.shields.io/badge/license-bsd-orange.svg" alt="Licenses"></a>
</p> 


QuicX 是一个基于 QUIC 协议（RFC 9000）的高性能 HTTP/3 库，使用 C++11 实现。该项目提供了一个完整的 QUIC 和 HTTP/3 网络传输解决方案，支持高并发、低延迟的网络通信需求。

## 功能特性

### QUIC 协议实现
- **连接管理**
  - 基于 BoringSSL 的 TLS 1.3 加密和安全握手
  - 支持 0-RTT 和 1-RTT 连接建立
  - 连接迁移支持
  - 优雅的连接关闭处理

- **流量控制**
  - 基于窗口的流量控制
  - Stream 级别和 Connection 级别的并发控制
  - 支持 STREAM_DATA_BLOCKED 和 DATA_BLOCKED 帧

- **拥塞控制**
  - 实现多种拥塞控制算法：
    - BBR
    - Cubic
    - Reno
  - 智能的包重传策略

- **数据传输**
  - 可靠的包传输和确认机制
  - 高效的帧打包和解析
  - 支持 PING/PONG 心跳检测
  - PATH_CHALLENGE/PATH_RESPONSE 路径验证

### HTTP/3 实现
- **头部压缩**
  - 实现 QPACK 动态表和静态表
  - 高效的 Huffman 编码
  - 头部字段验证和处理

- **流管理**
  - 双向流和单向流支持
  - 流优先级处理
  - 流量控制和背压处理

- **请求处理**
  - 支持所有 HTTP 方法
  - 灵活的路由系统
  - 中间件机制
  - 请求和响应的完整生命周期管理

### 核心组件

- **内存管理**
  - 高效的内存池实现
  - 智能的缓冲区管理
  - 零拷贝数据传输优化

- **并发控制**
  - 线程安全的数据结构
  - 无锁队列实现
  - 高效的事件循环

- **日志系统**
  - 多级别日志支持
  - 可配置的日志输出
  - 性能统计和监控

## 快速开始

### 构建要求
- C++11 或更高版本
- BoringSSL
- GTest（可选，用于单元测试）

### 构建步骤
```bash
git clone https://github.com/caozhiyi/quicX.git
cd quicX
make
```


## 性能优化

### 内存管理
- 使用内存池减少内存分配开销
- 智能的缓冲区管理避免频繁的内存拷贝
- 实现零拷贝机制提升数据传输效率

### 并发处理
- 多线程事件循环处理网络 I/O
- 无锁数据结构减少线程竞争
- 高效的任务调度机制

### 网络优化
- 智能的包重传策略
- 自适应的拥塞控制
- 高效的流量控制算法

## 许可证
MIT License - 查看 [LICENSE](LICENSE) 文件了解更多信息。
