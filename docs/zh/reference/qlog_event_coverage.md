# Qlog 事件覆盖报告

## 概览

| 类别 | 规范定义事件数 | 已实现 | 覆盖率 |
|------|---------------|--------|--------|
| Connectivity | 5 | 5 | 100% |
| Transport | 6 | 5 | 83% |
| Recovery | 4 | 4 | 100% |
| Security | 2 | 2 | 100% |
| HTTP/3 | 2 | 2 | 100% |
| **合计** | **19** | **18** | **94.7%** |

> **说明**：19 个事件中 18 个已有宏定义和实际埋点调用。唯一未调用的 `QLOG_PACKET_BUFFERED` 因当前架构没有真正的"包先缓冲后处理"场景，标记为 N/A。

## 详细事件覆盖

### ✅ Connectivity 事件（5/5）—— 100%

| 事件 | 状态 | 宏 | 源文件 | 调用次数 | 备注 |
|------|------|----|--------|----------|------|
| `quic:connection_started` | ✅ | `QLOG_CONNECTION_STARTED` | `connection_client.cpp`、`connection_server.cpp` | 2 | 客户端 / 服务端连接建立 |
| `quic:connection_closed` | ✅ | `QLOG_CONNECTION_CLOSED` | `connection_base.cpp` | 1 | 携带 error_code、reason、trigger |
| `quic:connection_id_updated` | ✅ | `QLOG_CONNECTION_ID_UPDATED` | `connection_frame_processor.cpp`、`connection_id_coordinator.cpp` | 4 | NEW_CONNECTION_ID、RETIRE_CONNECTION_ID、连接 ID 池补充、CID 轮换 |
| `quic:server_listening` | ✅ | `QLOG_SERVER_LISTENING` | `quic_server.cpp` | 1 | 服务器开始监听（使用 `QlogManager&` 而非 trace） |
| `quic:connection_state_updated` | ✅ | `QLOG_EVENT`（通用） | `connection_base.cpp` | 3 | handshake → connected、connected → closing、closing → draining |

### ✅ Transport 事件（5/6）—— 83%

| 事件 | 状态 | 宏 | 源文件 | 调用次数 | 备注 |
|------|------|----|--------|----------|------|
| `quic:packet_sent` | ✅ | `QLOG_PACKET_SENT` | `send_control.cpp` | 1 | 携带 packet_number、type、size、frames |
| `quic:packet_received` | ✅ | `QLOG_PACKET_RECEIVED` | `connection_base.cpp` | 1 | 携带 packet_number、type、size、frames |
| `quic:packets_acked` | ✅ | `QLOG_EVENT`（通用） | `send_control.cpp` | 1 | 携带 ack_ranges、ack_delay |
| `quic:packet_dropped` | ✅ | `QLOG_PACKET_DROPPED` | `connection_base.cpp` | 5 | 关闭状态下解密失败、密钥不可用、draining 状态、版本协商降级、解密失败 |
| `quic:stream_state_updated` | ✅ | `QLOG_STREAM_STATE_UPDATED` | `connection_stream_manager.cpp` | 1 | 流状态变更 |
| `quic:packet_buffered` | ⬜ N/A | `QLOG_PACKET_BUFFERED` | — | 0 | 宏已定义；当前架构无真正的包缓冲场景 |

### ✅ Recovery 事件（4/4）—— 100%

| 事件 | 状态 | 宏 | 源文件 | 调用次数 | 备注 |
|------|------|----|--------|----------|------|
| `recovery:metrics_updated` | ✅ | `QLOG_METRICS_UPDATED` | `send_control.cpp` | 1 | RTT、cwnd、bytes_in_flight、ssthresh、pacing_rate |
| `recovery:congestion_state_updated` | ✅ | `QLOG_CONGESTION_STATE_UPDATED` | 5 个拥塞控制算法文件 | 22 | Reno(3)、CUBIC(6)、BBRv1(4)、BBRv2(4)、BBRv3(5) |
| `recovery:packet_lost` | ✅ | `QLOG_PACKET_LOST` | `send_control.cpp` | 1 | 检测到丢包 |
| `recovery:marked_for_retransmit` | ✅ | `QLOG_MARKED_FOR_RETRANSMIT` | `send_control.cpp` | 2 | loss_detected 与 pto_expired 触发的重传标记 |

### ✅ Security 事件（2/2）—— 100%

| 事件 | 状态 | 宏 | 源文件 | 调用次数 | 备注 |
|------|------|----|--------|----------|------|
| `security:key_updated` | ✅ | `QLOG_KEY_UPDATED` | `connection_crypto.cpp` | 6 | 安装读 / 写密钥（initial / handshake / 1-RTT / key_update） |
| `security:key_discarded` | ✅ | `QLOG_KEY_DISCARDED` | `connection_server.cpp`、`connection_client.cpp` | 4 | handshake_done 后丢弃 initial 与 handshake 密钥 |

### ✅ HTTP/3 事件（2/2）—— 100%

| 事件 | 状态 | 宏 | 源文件 | 调用次数 | 备注 |
|------|------|----|--------|----------|------|
| `http3:frame_created` | ✅ | `QLOG_HTTP3_FRAME_CREATED` | `req_resp_base_stream.cpp` | 3 | DATA 帧、HEADERS 帧、批量 DATA 帧发送 |
| `http3:frame_parsed` | ✅ | `QLOG_HTTP3_FRAME_PARSED` | `frame_decoder.cpp` | 1 | H3 帧解码完成 |

## 拥塞状态跟踪覆盖

### 各算法的 Qlog 接入情况

| 算法 | `slow_start → congestion_avoidance` | `→ recovery` | `recovery → congestion_avoidance` | `→ application_limited` | 事件总数 |
|------|--------------------------------------|--------------|-----------------------------------|--------------------------|----------|
| **Reno** | ✅（ssthresh 触发） | ✅（loss / ECN） | ✅（recovery 后收到 ack） | ❌ N/A | 3 |
| **CUBIC** | ✅（ssthresh + HyStart） | ✅（loss + ECN） | ✅（recovery 后收到 ack） | ❌ N/A | 6 |
| **BBR v1** | ✅（startup 退出） | ✅（startup 期间丢包） | ❌ N/A | ✅（ProbeRtt 进 / 出） | 4 |
| **BBR v2** | ✅（startup 退出） | ✅（startup 期间丢包） | ❌ N/A | ✅（ProbeRtt 进 / 出） | 4 |
| **BBR v3** | ✅（startup 退出 + ECN） | ✅（startup 期间丢包） | ❌ N/A | ✅（ProbeRtt 进 / 出） | 5 |

**BBR 状态映射（qlog 标准 → BBR 模式）**：

- `slow_start` → `Mode::kStartup`
- `congestion_avoidance` → `Mode::kProbeBw`（稳态探测）
- `recovery` → `Mode::kDrain`（startup 期间丢包后）
- `application_limited` → `Mode::kProbeRtt`（最小 RTT 探测）

## Qlog 宏调用汇总

| 宏 | 调用次数 | 文件 |
|----|----------|------|
| `QLOG_CONGESTION_STATE_UPDATED` | 22 | reno(3)、cubic(6)、bbr_v1(4)、bbr_v2(4)、bbr_v3(5) |
| `QLOG_KEY_UPDATED` | 6 | `connection_crypto.cpp` |
| `QLOG_PACKET_DROPPED` | 5 | `connection_base.cpp` |
| `QLOG_CONNECTION_ID_UPDATED` | 4 | `connection_frame_processor.cpp`(2)、`connection_id_coordinator.cpp`(2) |
| `QLOG_KEY_DISCARDED` | 4 | `connection_server.cpp`(2)、`connection_client.cpp`(2) |
| `QLOG_EVENT`（通用） | 4 | `send_control.cpp`(1)、`connection_base.cpp`(3) |
| `QLOG_HTTP3_FRAME_CREATED` | 3 | `req_resp_base_stream.cpp` |
| `QLOG_CONNECTION_STARTED` | 2 | `connection_client.cpp`、`connection_server.cpp` |
| `QLOG_MARKED_FOR_RETRANSMIT` | 2 | `send_control.cpp` |
| `QLOG_PACKET_SENT` | 1 | `send_control.cpp` |
| `QLOG_PACKET_RECEIVED` | 1 | `connection_base.cpp` |
| `QLOG_METRICS_UPDATED` | 1 | `send_control.cpp` |
| `QLOG_CONNECTION_CLOSED` | 1 | `connection_base.cpp` |
| `QLOG_PACKET_LOST` | 1 | `send_control.cpp` |
| `QLOG_STREAM_STATE_UPDATED` | 1 | `connection_stream_manager.cpp` |
| `QLOG_HTTP3_FRAME_PARSED` | 1 | `frame_decoder.cpp` |
| `QLOG_SERVER_LISTENING` | 1 | `quic_server.cpp` |
| `QLOG_PACKET_BUFFERED` | 0 | —（N/A：暂无适用场景） |
| **合计** | **57** | **13 个文件** |

## 性能影响

数据来源：基准测试 `test/benchmarks/qlog_overhead_bench.cpp`（442 行，16+ 个用例）。

| 指标 | 数值 |
|------|------|
| 单事件耗时（PacketSent） | **~1.5 μs** |
| 单事件耗时（RecoveryMetrics） | **~1.8 μs** |
| 持续吞吐 | **~690K events/sec** |
| 空 trace 开销 | **< 1 ns**（关闭时近乎零成本） |
| 仅序列化耗时（PacketSent） | **~485 ns** |
| 事件白名单过滤 | **~101 ns**（快路径拒绝） |
| 多连接扩展性 | **线性**（最多 50 条连接无降级） |
| AsyncWriter 队列压力 | 在持续高量写入下通过测试 |

### 基准测试覆盖维度

| 维度 | 用例数 | 说明 |
|------|--------|------|
| 单事件延迟 | 3 | PacketSent、RecoveryMetrics、ConnectionStarted |
| 吞吐 | 1 | 持续事件写入速率 |
| AsyncWriter 队列 | 1 | 持续写入下的队列压力 |
| 采样率 | 1 | 不同采样率下的性能影响 |
| 序列化 | 2 | JSON-SEQ 序列化开销、白名单过滤 |
| Manager 生命周期 | 1 | trace 创建 / 销毁开销 |
| 多连接 | 1 | 50 连接并发写入 |
| 关闭态 | 1 | qlog 关闭时的零成本验证 |

## 测试覆盖

### 单元测试（位于 `test/unit_test/common/qlog/`，共 10 个文件）

| 测试文件 | 大小 | 主要用例 |
|----------|------|----------|
| `qlog_e2e_output_test.cpp` | 20.3 KB | 7 个用例：完整生命周期、字段正确性、多连接隔离、事件顺序、白名单过滤、大量事件、vantage point |
| `qlog_event_test.cpp` | 24.0 KB | 各事件类型的数据构造与序列化 |
| `qlog_serializer_test.cpp` | 22.3 KB | JSON-SEQ 格式合规性、时间戳精度、字段完整性 |
| `qlog_async_writer_test.cpp` | 15.5 KB | 异步写队列、flush、背压 |
| `qlog_trace_test.cpp` | 14.2 KB | trace 生命周期、白 / 黑名单过滤 |
| `qlog_manager_test.cpp` | 13.0 KB | manager 单例、trace 管理、配置传递 |
| `qlog_integration_test.cpp` | 12.1 KB | 跨组件集成场景 |
| `qlog_types_test.cpp` | 9.8 KB | 类型转换（PacketType、FrameType、VantagePoint） |
| `qlog_config_test.cpp` | 8.4 KB | 配置校验与默认值 |
| `qlog_sampling_test.cpp` | 7.0 KB | 采样率行为 |

### 校验脚本

| 脚本 | 大小 | 说明 |
|------|------|------|
| `scripts/verify_qlog_format.py` | 379 行 | RFC 7464 格式校验、字段检查、时间戳单调性 |
| `scripts/verify_qlog_qvis.py` | — | qvis.quictools.info 兼容性校验 |

## 架构概览

```
QlogManager（单例）
  ├── QlogConfig（全局：enabled、output_dir、sampling_rate、白 / 黑名单）
  ├── AsyncWriter（异步写线程）
  └── traces_ 映射：connection_id -> QlogTrace
        ├── IQlogSerializer（JsonSeqSerializer）
        ├── ShouldLogEvent() 白 / 黑名单过滤
        └── WriteEvent() -> AsyncWriter
              └── 输出 .sqlog 文件（JSON-SEQ 格式）

宏调用链：
  QLOG_PACKET_SENT(trace, data)
    -> trace->LogPacketSent(time_us, data)
      -> LogEvent(time_us, "quic:packet_sent", unique_ptr<PacketSentData>)
        -> ShouldLogEvent() 过滤
        -> SerializeEvent() JSON 序列化
        -> AsyncWriter::WriteEvent() 异步写
```

## 变更记录

| 日期 | 变更 | 影响 |
|------|------|------|
| 2026-03-19 | 首次发布 | 12/19 事件（63%） |
| 2026-03-20 | **重大更新**：覆盖率从 63% 修正为 94.7%（18/19） | 新增：connection_id_updated、server_listening、stream_state_updated、packet_dropped、key_updated、key_discarded、marked_for_retransmit、http3:frame_created、http3:frame_parsed。`src/` 下宏调用合计：13 个文件、57 处 |
