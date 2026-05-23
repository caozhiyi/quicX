# Qlog Event Coverage Report

**Date**: 2026-05-23  
**Qlog Version**: draft-02 (draft-ietf-quic-qlog-main-schema-02)  
**Format**: JSON-SEQ (RFC 7464; record separator `0x1E`, line-feed terminated)  
**File Extension**: `.sqlog`  
**Compatibility**: Directly uploadable to [qvis](https://qvis.quictools.info)

## Summary

| Category | Defined Events | Implemented | Coverage |
|----------|---------------|-------------|----------|
| Connectivity | 5 | 5 | 100% |
| Transport | 6 | 5 | 83% |
| Recovery | 4 | 4 | 100% |
| Security | 2 | 2 | 100% |
| HTTP/3 | 2 | 2 | 100% |
| **Total** | **19** | **18** | **94.7%** |

> **Note**: 19 个事件中 18 个已有宏定义和实际埋点调用。唯一未调用的 `QLOG_PACKET_BUFFERED` 因当前架构无真正的包缓冲场景，标记为 N/A。

## Detailed Event Coverage

### ✅ Connectivity Events (5/5) — 100%

| Event | Status | Macro | Source File(s) | Call Count | Notes |
|-------|--------|-------|----------------|-----------|-------|
| `quic:connection_started` | ✅ | `QLOG_CONNECTION_STARTED` | `connection_client.cpp`, `connection_server.cpp` | 2 | 客户端/服务端连接建立 |
| `quic:connection_closed` | ✅ | `QLOG_CONNECTION_CLOSED` | `connection_base.cpp` | 1 | With error_code, reason, trigger |
| `quic:connection_id_updated` | ✅ | `QLOG_CONNECTION_ID_UPDATED` | `connection_frame_processor.cpp`, `connection_id_coordinator.cpp` | 4 | NEW_CONNECTION_ID、RETIRE_CONNECTION_ID、pool_replenish、cid_rotation |
| `quic:server_listening` | ✅ | `QLOG_SERVER_LISTENING` | `quic_server.cpp` | 1 | 服务器开始监听（使用 QlogManager& 而非 trace） |
| `quic:connection_state_updated` | ✅ | `QLOG_EVENT` (generic) | `connection_base.cpp` | 3 | handshake→connected, connected→closing, closing→draining |

### ✅ Transport Events (5/6) — 83%

| Event | Status | Macro | Source File(s) | Call Count | Notes |
|-------|--------|-------|----------------|-----------|-------|
| `quic:packet_sent` | ✅ | `QLOG_PACKET_SENT` | `send_control.cpp` | 1 | With packet_number, type, size, frames |
| `quic:packet_received` | ✅ | `QLOG_PACKET_RECEIVED` | `connection_base.cpp` | 1 | With packet_number, type, size, frames |
| `quic:packets_acked` | ✅ | `QLOG_EVENT` (generic) | `send_control.cpp` | 1 | With ack_ranges, ack_delay |
| `quic:packet_dropped` | ✅ | `QLOG_PACKET_DROPPED` | `connection_base.cpp` | 5 | 关闭状态解密失败、key 不可用、draining 状态、版本协商降级、解密失败 |
| `quic:stream_state_updated` | ✅ | `QLOG_STREAM_STATE_UPDATED` | `connection_stream_manager.cpp` | 1 | 流状态变更 |
| `quic:packet_buffered` | ⬜ N/A | `QLOG_PACKET_BUFFERED` | — | 0 | 宏已定义，当前架构无真正的包缓冲场景 |

### ✅ Recovery Events (4/4) — 100%

| Event | Status | Macro | Source File(s) | Call Count | Notes |
|-------|--------|-------|----------------|-----------|-------|
| `recovery:metrics_updated` | ✅ | `QLOG_METRICS_UPDATED` | `send_control.cpp` | 1 | RTT, cwnd, bytes_in_flight, ssthresh, pacing_rate |
| `recovery:congestion_state_updated` | ✅ | `QLOG_CONGESTION_STATE_UPDATED` | 5 个 CC 算法文件 | 22 | Reno(3), CUBIC(6), BBRv1(4), BBRv2(4), BBRv3(5) |
| `recovery:packet_lost` | ✅ | `QLOG_PACKET_LOST` | `send_control.cpp` | 1 | 检测到丢包 |
| `recovery:marked_for_retransmit` | ✅ | `QLOG_MARKED_FOR_RETRANSMIT` | `send_control.cpp` | 2 | loss_detected 和 pto_expired 触发重传 |

### ✅ Security Events (2/2) — 100%

| Event | Status | Macro | Source File(s) | Call Count | Notes |
|-------|--------|-------|----------------|-----------|-------|
| `security:key_updated` | ✅ | `QLOG_KEY_UPDATED` | `connection_crypto.cpp` | 6 | 安装读/写密钥 (initial/handshake/1-RTT/key_update) |
| `security:key_discarded` | ✅ | `QLOG_KEY_DISCARDED` | `connection_server.cpp`, `connection_client.cpp` | 4 | handshake_done 后丢弃 initial 和 handshake 密钥 |

### ✅ HTTP/3 Events (2/2) — 100%

| Event | Status | Macro | Source File(s) | Call Count | Notes |
|-------|--------|-------|----------------|-----------|-------|
| `http3:frame_created` | ✅ | `QLOG_HTTP3_FRAME_CREATED` | `req_resp_base_stream.cpp` | 3 | DATA 帧、HEADERS 帧、批量 DATA 帧发送 |
| `http3:frame_parsed` | ✅ | `QLOG_HTTP3_FRAME_PARSED` | `frame_decoder.cpp` | 1 | H3 帧解码完成 |

## Congestion State Tracking Coverage

### Per-Algorithm QLOG Integration

| Algorithm | `slow_start → congestion_avoidance` | `→ recovery` | `recovery → congestion_avoidance` | `→ application_limited` | Total Events |
|-----------|-------------------------------------|--------------|-----------------------------------|------------------------|--------------|
| **Reno** | ✅ (ssthresh) | ✅ (loss/ECN) | ✅ (ack after recovery) | ❌ N/A | 3 |
| **CUBIC** | ✅ (ssthresh + HyStart) | ✅ (loss + ECN) | ✅ (ack after recovery) | ❌ N/A | 6 |
| **BBR v1** | ✅ (startup exit) | ✅ (loss in startup) | ❌ N/A | ✅ (ProbeRtt enter/exit) | 4 |
| **BBR v2** | ✅ (startup exit) | ✅ (loss in startup) | ❌ N/A | ✅ (ProbeRtt enter/exit) | 4 |
| **BBR v3** | ✅ (startup exit + ECN) | ✅ (loss in startup) | ❌ N/A | ✅ (ProbeRtt enter/exit) | 5 |

**State Mapping for BBR (qlog standard → BBR mode)**:
- `slow_start` → `Mode::kStartup`
- `congestion_avoidance` → `Mode::kProbeBw` (steady-state probing)
- `recovery` → `Mode::kDrain` (after loss in startup)
- `application_limited` → `Mode::kProbeRtt` (min_rtt probing)

## QLOG Macro Usage Summary

| Macro | Call Count | Files |
|-------|-----------|-------|
| `QLOG_CONGESTION_STATE_UPDATED` | 22 | reno(3), cubic(6), bbr_v1(4), bbr_v2(4), bbr_v3(5) |
| `QLOG_KEY_UPDATED` | 6 | connection_crypto.cpp |
| `QLOG_PACKET_DROPPED` | 5 | connection_base.cpp |
| `QLOG_CONNECTION_ID_UPDATED` | 4 | connection_frame_processor.cpp(2), connection_id_coordinator.cpp(2) |
| `QLOG_KEY_DISCARDED` | 4 | connection_server.cpp(2), connection_client.cpp(2) |
| `QLOG_EVENT` (generic) | 4 | send_control.cpp(1), connection_base.cpp(3) |
| `QLOG_HTTP3_FRAME_CREATED` | 3 | req_resp_base_stream.cpp |
| `QLOG_CONNECTION_STARTED` | 2 | connection_client.cpp, connection_server.cpp |
| `QLOG_MARKED_FOR_RETRANSMIT` | 2 | send_control.cpp |
| `QLOG_PACKET_SENT` | 1 | send_control.cpp |
| `QLOG_PACKET_RECEIVED` | 1 | connection_base.cpp |
| `QLOG_METRICS_UPDATED` | 1 | send_control.cpp |
| `QLOG_CONNECTION_CLOSED` | 1 | connection_base.cpp |
| `QLOG_PACKET_LOST` | 1 | send_control.cpp |
| `QLOG_STREAM_STATE_UPDATED` | 1 | connection_stream_manager.cpp |
| `QLOG_HTTP3_FRAME_PARSED` | 1 | frame_decoder.cpp |
| `QLOG_SERVER_LISTENING` | 1 | quic_server.cpp |
| `QLOG_PACKET_BUFFERED` | 0 | — (N/A: 无适用场景) |
| **Total** | **57** | **13 files** |

## Performance Impact

Based on benchmark results (`test/benchmarks/qlog_overhead_bench.cpp`, 442 lines, 16+ benchmarks):

| Metric | Value |
|--------|-------|
| Single event latency (PacketSent) | **~1.5 μs** |
| Single event latency (RecoveryMetrics) | **~1.8 μs** |
| Throughput (sustained) | **~690K events/sec** |
| Null trace overhead | **< 1 ns** (zero-cost when disabled) |
| Serialization only (PacketSent) | **~485 ns** |
| Event whitelist filter | **~101 ns** (fast-path rejection) |
| Multi-connection scaling | **Linear** (no degradation up to 50 connections) |
| AsyncWriter queue stress | Tested with sustained high-volume writes |

### Benchmark Coverage

| Dimension | Benchmarks | Description |
|-----------|-----------|-------------|
| Single Event Latency | 3 | PacketSent, RecoveryMetrics, ConnectionStarted |
| Throughput | 1 | Sustained event logging rate |
| AsyncWriter Queue | 1 | Queue pressure under sustained writes |
| Sampling Rate | 1 | Performance impact of different sampling rates |
| Serialization | 2 | JSON-SEQ serialization overhead, whitelist filtering |
| Manager Lifecycle | 1 | Create/destroy trace lifecycle cost |
| Multi-connection | 1 | Concurrent writes from 50 connections |
| Null/Disabled | 1 | Zero-cost verification when qlog disabled |

## Test Coverage

### Unit Tests (10 files in `test/unit_test/common/qlog/`)

| Test File | Size | Key Tests |
|-----------|------|-----------|
| `qlog_e2e_output_test.cpp` | 20.3 KB | 7 tests: complete lifecycle, field correctness, multi-connection isolation, event ordering, whitelist filtering, large volume, vantage point |
| `qlog_event_test.cpp` | 24.0 KB | Event data creation and serialization for all event types |
| `qlog_serializer_test.cpp` | 22.3 KB | JSON-SEQ format compliance, timestamp precision, field completeness |
| `qlog_async_writer_test.cpp` | 15.5 KB | Async write queue, flush, backpressure |
| `qlog_trace_test.cpp` | 14.2 KB | Trace lifecycle, whitelist/blacklist filtering |
| `qlog_manager_test.cpp` | 13.0 KB | Manager singleton, trace management, config propagation |
| `qlog_integration_test.cpp` | 12.1 KB | Cross-component integration scenarios |
| `qlog_types_test.cpp` | 9.8 KB | Type conversion (PacketType, FrameType, VantagePoint) |
| `qlog_config_test.cpp` | 8.4 KB | Configuration validation and defaults |
| `qlog_sampling_test.cpp` | 7.0 KB | Sampling rate behavior |

### Verification Scripts

| Script | Size | Description |
|--------|------|-------------|
| `scripts/verify_qlog_format.py` | 379 lines | RFC 7464 format validation, field checks, timestamp monotonicity |
| `scripts/verify_qlog_qvis.py` | — | qvis.quictools.info compatibility validation |

## Architecture Overview

```
QlogManager (singleton)
  ├── QlogConfig (global: enabled, output_dir, sampling_rate, whitelist/blacklist)
  ├── AsyncWriter (async write thread)
  └── traces_ map: connection_id -> QlogTrace
        ├── IQlogSerializer (JsonSeqSerializer)
        ├── ShouldLogEvent() whitelist/blacklist filtering
        └── WriteEvent() -> AsyncWriter
              └── output .sqlog files (JSON-SEQ format)

Macro call chain:
  QLOG_PACKET_SENT(trace, data)
    -> trace->LogPacketSent(time_us, data)
      -> LogEvent(time_us, "quic:packet_sent", unique_ptr<PacketSentData>)
        -> ShouldLogEvent() filtering
        -> SerializeEvent() JSON serialization
        -> AsyncWriter::WriteEvent() async write
```

## Change Log

| Date | Change | Impact |
|------|--------|--------|
| 2026-03-19 | Initial report | 12/19 events (63%) |
| 2026-03-20 | **Major update**: Corrected coverage from 63% to 94.7% (18/19) | Added: connection_id_updated, server_listening, stream_state_updated, packet_dropped, key_updated, key_discarded, marked_for_retransmit, http3:frame_created, http3:frame_parsed. Total macros in src/: 57 calls in 13 files |
