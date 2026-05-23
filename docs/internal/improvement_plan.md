# quicX 工业级成熟度完善计划

> 文档版本：v1.2（根据仓库实情校准）  
> 创建时间：2026-04-30  
> 最近更新：2026-04-30（QPack decoder stream 语义修复后）  
> 目标：将 quicX 从"协议核心基本完成"推进至"工业级开源 QUIC 库"  
> 对标对象：Cloudflare quiche / Microsoft msquic / LiteSpeed lsquic / quic-go

## 更新说明（v1.2）

相对 v1.1 的增量：
- **QPack decoder stream 语义修复（RFC 9204 §4.4.1 / §4.4.2）**：修复 6 个失败单测（`QpackDecoderFramesTest.*` 2 个 + `QpackDecoderStreamTest.*` 4 个）。
  - 根因：Section Acknowledgment / Stream Cancellation 的 wire format **只携带 Stream ID**，不携带 section number；但接收端曾以 `(stream_id << 32) | section_number` 拼 key 去 `BlockedRegistry::Ack()`，导致 ack 永远未命中注册项。
  - 修复：`QpackBlockedRegistry` 新增 `AckByStreamId` / `RemoveByStreamId`，按 RFC §4.4.1"按顺序 ack"的语义匹配该 stream 下 section_number 最小的 pending 条目；接收端改走新接口。
  - 影响：全量单测 1181/1187 → **1187/1187**；QPack 子集 30/36 → **36/36**。
  - 教训提炼：新增 [§2.8 跨层一致性债](#28-跨层一致性债已识别的易错模式)，把"wire 字段 ≠ 本地记账 key"沉淀为需要系统排查的模式。
- **HTTP/3 + QPACK 完成度**从 8/10 微调至 9/10（§1.1）；附录 A 新增对应"已完成"记录。

相对 v1.0 的修正要点（基于对源码的二次核验）：
- **互通矩阵已就位**：仓库已实现 `interop_runner.py`，通过 `--matrix` 可对 12 种实现（quiche/ngtcp2/quic-go/mvfst/msquic/lsquic/picoquic/aioquic/quinn/neqo/s2n-quic + quicX）做 N×N 交叉测试；local 自测 14/14，Docker 自测 13/14（唯一失败为早期 HTTP/3 集成阶段，现已修复）。缺的是**推镜像到 GHCR + 向官方 [quic-interop-runner](https://github.com/quic-interop/quic-interop-runner) 仓库提 PR 上榜**，而非"从 0 建设 interop"。
- **零拷贝已部分落地**：`BufferReader` 的 Contiguous 模式支持 zero-copy 读（TODO.md 第 9 行已 checked），包解码路径在内部直接在原始缓冲上游标推进。待补的是**socket 层批量化（`sendmmsg`/`recvmmsg` 已封装但未真正调用）、GSO/`UDP_SEGMENT`、`SO_REUSEPORT`、`MSG_ZEROCOPY`**。
- **ECN 已实现**：`io_handle` 提供 `EnableUdpEcn/EnableUdpEcnMarking/RecvFromWithEcn`，主循环已集成。
- **aioquic/ngtcp2/quic-go 跨实现已发现并修复 3 个互通 Bug**（合并包解码、`initial_source_connection_id`、`original_destination_connection_id`）。剩余跨实现失败项属于需继续排查的待办。

---

## 目录

- [一、现状评估](#一现状评估)
- [二、差距分析](#二差距分析)
  - [2.8 跨层一致性债](#28-跨层一致性债已识别的易错模式)
- [三、Phase A：生产可用底线（4-6 周，P0）](#三phase-a生产可用底线4-6-周p0)
- [四、Phase B：协议完整与性能对齐（6-8 周，P1）](#四phase-b协议完整与性能对齐6-8-周p1)
- [五、Phase C：生态与高级能力（持续，P2）](#五phase-c生态与高级能力持续p2)
- [六、里程碑与版本规划](#六里程碑与版本规划)
- [七、质量门禁（Definition of Done）](#七质量门禁definition-of-done)
- [八、风险与缓解](#八风险与缓解)
- [附录 A：关键 TODO 清单](#附录-a关键-todo-清单)
- [附录 B：参考资料](#附录-b参考资料)
- [附录 C：下一步行动（滚动清单）](#附录-c下一步行动滚动清单)

---

## 一、现状评估

### 1.1 完成度雷达图（满分 10）

| 维度 | 得分 | 状态 |
|---|---|---|
| 协议核心完整性（RFC 9000 / 9001 / 9002） | 9/10 | ✅ 优秀 |
| HTTP/3 + QPACK（RFC 9114 / 9204） | 9/10 | ✅ 优秀（decoder stream 语义已对齐 §4.4.1/§4.4.2） |
| TLS 1.3 / 握手 | 8/10 | ✅ 良好 |
| 拥塞控制（Cubic / BBR / Reno） | 8/10 | ✅ 良好 |
| 丢包检测与恢复（RFC 9002） | 8/10 | ✅ 良好 |
| ECN / qlog | 7/10 | ✅ 已实现，需端到端 qvis 验证 |
| 协议扩展（DATAGRAM / ACK-Frequency / Priorities 等） | 2/10 | ❌ 严重缺失 |
| 测试体系（单测 / fuzz / benchmark） | 8/10 | ✅ 良好 |
| 互通性：自测 | 8/10 | ✅ local 14/14，Docker 13/14 |
| 互通性：跨实现矩阵 | 5/10 | ⚠️ 矩阵框架就绪，多数跨实现仍 fail |
| 互通性：官方榜单 | 1/10 | ❌ 未推镜像 / 未提 PR |
| 工程化 / CI/CD | 2/10 | ❌ 无 `.github/workflows/` |
| 跨平台交付（包管理 / 二进制） | 2/10 | ❌ 严重缺失 |
| API 稳定性 / ABI 承诺 | 3/10 | ❌ 缺失 C ABI |
| 生态对接（curl / nginx / 语言绑定） | 1/10 | ❌ 缺失 |
| 性能：用户态零拷贝 | 6/10 | ⚠️ BufferReader 已 zero-copy；socket 层未做 |
| 性能：批量 syscall（`sendmmsg`/`recvmmsg`） | 3/10 | ⚠️ 已封装但未调用 |
| 性能：GSO / REUSEPORT / io_uring | 1/10 | ❌ 完全缺失 |
| 文档（用户 / API / 架构 / 治理） | 5/10 | ⚠️ 缺治理与 API 参考 |

**综合：~5.5/10。距"工业级"差距主要在"工程化、生态、性能极致、协议扩展"四象限，不在协议核心本身。**

### 1.2 三大核心短板

1. **工程化空洞**：无 `.github/workflows/`、无 release、无包管理发行、无跨平台矩阵验证；互通矩阵虽已实现但镜像未推 GHCR、未提 PR 上官方榜单。
2. **生态孤立**：无 C ABI、无语言绑定、无 curl/nginx 集成。
3. **socket 层性能未对齐现代 QUIC**：用户态零拷贝已有，但 socket 层仍是 per-packet `SendTo`/`RecvFrom`；`sendmmsg`/`recvmmsg` 系统封装存在却未被 worker 调用；`UDP_SEGMENT`(GSO) / `SO_REUSEPORT` / `MSG_ZEROCOPY` / `io_uring` 全部缺失。

---

## 二、差距分析

### 2.1 协议层缺失项（相对 RFC / IETF 标准）

| 扩展 / 功能 | RFC / Draft | 现状 | 影响面 | 优先级 |
|---|---|---|---|---|
| DATAGRAM Frame | RFC 9221 | ❌ 零实现 | WebTransport / MASQUE / 实时音视频 | P1 |
| ACK Frequency | draft-ietf-quic-ack-frequency | ❌ 未实现 | 高 BDP + 移动端耗电 | P1 |
| Path MTU Discovery (DPLPMTUD) | RFC 8899 | ❌ 未实现 | IPv6 / PPPoE 链路不可用 | P1 |
| GREASE 版本 / 帧类型 / 传输参数 | RFC 8701 / 9287 | ❓ 未显式注入 | 协议僵化对抗 | P2 |
| Stateless Reset | RFC 9000 §10.3 | ❓ token/key 机制不完整 | 服务端重启优雅通知 | P1 |
| HTTP/3 GOAWAY | RFC 9114 §5.2 | ⚠️ TODO | 滚动发布不可行 | P0 |
| HTTP/3 Extensible Priorities | RFC 9218 | ❌ 未实现 | HTTP/3 调度必选 | P1 |
| WebTransport over HTTP/3 | draft-ietf-webtrans-http3 | ❌ 未实现 | 现代应用场景 | P2 |
| qlog schema v2 端到端验证 | RFC 9307 | ⚠️ 有代码未验 | 调试能力 | P1 |
| 0-RTT replay 保护 | RFC 9001 §9.2 | ❓ 无专门语料 / 审计 | 安全 | P1 |

### 2.3 互通性现状（与 v1.0 评审的核心修正）

| 维度 | 现状 | 说明 |
|---|---|---|
| 互通测试框架 | ✅ 已实现 | `test/interop/interop_runner.py` 支持 local / docker / matrix 三种模式 |
| 注册实现数 | ✅ 12 | quicX + quiche / ngtcp2 / quic-go / mvfst / msquic / lsquic / picoquic / aioquic / quinn / neqo / s2n-quic |
| 测试场景数 | ✅ 14 | handshake / transfer / retry / resumption / zerortt / multiconnect / versionnegotiation / chacha20 / keyupdate / v2 / rebind-port / rebind-addr / connectionmigration / http3 |
| 本地自测 | ✅ 14/14 | 全通 |
| Docker 自测 | ✅ 13/14 | 含 http3，早期的失败项已修复 |
| 跨实现（部分完成） | ⚠️ | aioquic 已过 handshake/multiconnect/v2；其余 transfer/retry/resumption/zerortt/http3/chacha20/keyupdate 在 aioquic 跨实现下仍 fail；ngtcp2/quic-go 握手仍有问题 |
| 镜像推送到 GHCR | ❌ | manifest 声称 `ghcr.io/quicx/quicx-interop:latest` 但尚未推送 |
| 官方 [interop.seemann.io](https://interop.seemann.io) 上榜 | ❌ | 未向官方 runner 仓库提 PR |

**⇒ 本项不再是"从零建设"，而是：① 把本地已能跑的跨实现矩阵加固、② 推镜像、③ 提官方 PR。**

### 2.4 性能现状（与 v1.0 评审的核心修正）

| 能力 | 封装层 | 主路径调用点 | 状态 | 备注 |
|---|---|---|---|---|
| 用户态零拷贝读 | `BufferReader`（Contiguous 模式） | 包/帧 decode 路径 | ✅ 使用中 | TODO.md `[x] zero memory copy` |
| ECN 标记/读取 | `io_handle::EnableUdpEcn*` / `RecvFromWithEcn` | recv 主循环 | ✅ 使用中 | 支持 IPv4 `IP_TOS` + IPv6 `IPV6_TCLASS` |
| `sendmmsg` 批量发 | `io_handle::SendmMsg` | ❌ 未调用 | ⚠️ 仅封装 | Linux/Windows 实现存在；macOS 是 stub `{0,0}` |
| `recvmmsg` 批量收 | `io_handle::RecvmMsg` | ❌ 未调用 | ⚠️ 仅封装 | macOS 用循环 `recvmsg` 模拟 |
| `UDP_SEGMENT` (GSO) | 无 | — | ❌ | 现代 Linux QUIC 提升 3-5x 吞吐的关键 |
| `SO_REUSEPORT` + 多 worker | 无 | — | ❌ | 多核扩展性关键 |
| `MSG_ZEROCOPY`（内核零拷贝发包） | 无 | — | ❌ | `SOCK_ZEROCOPY` + `MSG_ERRQUEUE` 回收 |
| `io_uring` 后端 | 无 | — | ❌ | Linux 6+ 的 ns 级事件 |
| AF_XDP | 无 | — | ❌ | 专业 CDN/LB 场景 |
| CPU 亲和性 / Pinning | 无 | — | ❌ | 多核部署必需 |

**⇒ 本项的关键工作不是"实现零拷贝"，而是：① 让 `sendmmsg`/`recvmmsg` 真正上线（Phase B.2.2）、② 新增 GSO/REUSEPORT/MSG_ZEROCOPY（Phase B.2.1/B.2.3）。**

### 2.5 工程化缺失项

| 项目 | 现状 | 参考 |
|---|---|---|
| `.github/workflows/` CI | ❌ 无 | quiche 有 ~25 个 job |
| 多平台构建矩阵 | ❌ 无 | Linux×{gcc,clang} / macOS / Windows×MSVC |
| Sanitizer 构建（ASan/TSan/UBSan/MSan） | ❌ 无 | 标配 |
| 覆盖率（codecov） | ❌ 无 | 标配 |
| OSS-Fuzz 接入 | ❌ 无 | msquic / quiche 均有 |
| Release 流程（tag → artifact → changelog） | ❌ 无 | 标配 |
| SemVer 版本号 | ❌ 无 tag | 标配 |
| 预编译二进制（.deb/.rpm/.msi/.dmg） | ❌ 无 | 成熟库基本都有 |
| 包管理器（vcpkg / Conan / Homebrew / apt） | ❌ 无 | 标配 |
| 镜像发布到 GHCR | ⚠️ manifest 声明未推送 | 必需 |
| 官方 Interop Runner 上榜 | ❌ 未 PR | 必需 |
| API 参考（doxygen） | ❌ 未生成 | 标配 |

### 2.6 治理 & 社区文件缺失

| 文件 | 现状 |
|---|---|
| `CONTRIBUTING.md` | ❌ |
| `CODE_OF_CONDUCT.md` | ❌ |
| `SECURITY.md`（漏洞披露流程） | ❌ |
| `CHANGELOG.md` | ❌ |
| `GOVERNANCE.md`（维护者 / 决策流程） | ❌ |
| PR / Issue 模板（`.github/ISSUE_TEMPLATE/`） | ❌ |
| `MAINTAINERS` | ❌ |
| `CODEOWNERS` | ❌ |

### 2.7 关键代码遗留缺陷

见[附录 A](#附录-a关键-todo-清单)。

### 2.8 跨层一致性债（已识别的易错模式）

由近期 QPack decoder stream 修复（v1.2）提炼。核心错误模式：**"wire format 上不存在的字段"被当作可在端到端往返的 key**，导致本地数据结构与协议实际语义错位。

**已识别案例**：

| # | 位置 | 症状 | 状态 |
|---|---|---|---|
| 1 | `qpack_decoder_receiver_stream` + `blocked_registry` | Section Ack / Stream Cancellation wire 上仅有 Stream ID，接收端却按 `(stream_id<<32)\|section_number` 查表 | ✅ 已修（v1.2） |

**应系统排查的同类风险点**（待逐一确认，纳入 Phase A 行动项）：

| # | 疑似位置 | 可能的"wire 不存在但被用作 key"字段 | 优先级 |
|---|---|---|---|
| S1 | HTTP/3 stream 优先级 / 取消路径 | `QUIC_STREAM_RESET` 携带 stream_id + error_code + final_size，但不携带任何"逻辑请求编号"；若应用层用复合 key，需核对 | 中 |
| S2 | QPack encoder stream（Insert Count Increment） | wire 上只有 `Increment` 值；任何"按 section 匹配"的逻辑都必须纯本地 | 中 |
| S3 | QUIC `NEW_CONNECTION_ID` / `RETIRE_CONNECTION_ID` | `sequence_number` 是 wire 字段但在 retire 路径的**哪一端**为权威？需核对 `connection_id_manager` | 中 |
| S4 | PATH_RESPONSE 与 PATH_CHALLENGE 匹配 | 必须字节级比对 `data`，不能引入本地序号辅助判断 | 低（主要是审查） |
| S5 | 0-RTT session ticket / token | 只有 server 可以解读；client 端不能用其内部字段做"匹配"，需核对 `crypto/ticket*` | 中 |

**排查动作**：新增一个 `docs/AUDIT_wire_vs_local_key.md`（Phase A.2 末期产出），对上述 5 项逐一交叉 check 源码 + 引用对应 RFC 条款，每项要么修复、要么加注释说明为何安全。

---

## 三、Phase A：生产可用底线（4-6 周，P0）

**目标**：发布 `v0.1.0`，对外宣布"alpha 可试用、功能完整、CI 全绿、有官方互通榜位置"。

### A.1 CI/CD 基础设施（Week 1-2）

#### A.1.1 GitHub Actions 工作流设计

在 `.github/workflows/` 下新增：

| 工作流 | 触发 | 任务 | 预期耗时 |
|---|---|---|---|
| `ci.yml` | push / PR | 多平台编译 + 单测 | ~15 min |
| `sanitizer.yml` | push / nightly | ASan / TSan / UBSan / MSan | ~30 min |
| `coverage.yml` | push | gcov + 上传 codecov | ~20 min |
| `fuzz-smoke.yml` | PR | 每个 fuzzer 跑 60s | ~10 min |
| `interop.yml` | nightly | 本地 interop 矩阵 | ~45 min |
| `release.yml` | tag `v*` | 构建 artifact + 发 release | ~30 min |
| `docker.yml` | push main | 构建并推送 `ghcr.io/quicx/quicx-interop` | ~20 min |
| `docs.yml` | push main | doxygen + mkdocs → Pages | ~10 min |

#### A.1.2 构建矩阵

```
os:       [ubuntu-22.04, ubuntu-24.04, macos-13, macos-14, windows-2022]
compiler: [gcc-11, gcc-13, clang-15, clang-17, msvc-2022]
build:    [Debug, Release, RelWithDebInfo]
```

排除不合理组合（例如 Windows 上不跑 gcc）。

#### A.1.3 质量门禁

- 单测 100% 通过
- Sanitizer 0 report
- 覆盖率 ≥ 75%（初期），目标 85%
- Fuzz smoke 无 crash
- Clang-format / Clang-tidy 无 violation

### A.2 清理关键 TODO（Week 1-3）

#### A.2.1 `send_stream.cpp` 硬编码 1300

**问题**：
```246:250:quicX/src/quic/stream/send_stream.cpp
send_size = send_size > 1300 ? 1300 : send_size;  // TODO
```

**修复方案**：
1. 将 Path MTU 从 `Path` 对象贯穿到 `SendStream::TryGenerateFrame`。
2. 减去 QUIC/IP/UDP 头部开销（对应 short header + packet number + 认证 tag）。
3. 在 DPLPMTUD 支持前，默认 1200 / IPv6 最小 1232，通过握手 max_udp_payload_size 传输参数协商。

**验收**：
- 新增单测：MTU=1232、MTU=1500、MTU=9000（jumbo）三档。
- 抓包验证 UDP payload 不超过协商值。

#### A.2.2 HTTP/3 GOAWAY

**问题**：
```308:quicX/src/http3/connection/connection_server.cpp
// TODO: implement goaway
```

**修复方案**：
1. 实现 `GoawayFrame` 编/解码（已存在骨架需补全）。
2. `Connection::Shutdown(uint64_t last_stream_id)`：
   - 发送 GOAWAY（控制流）
   - 拒绝新流
   - 等待在途流完成或超时
3. 客户端收到 GOAWAY 后停止开新流，迁移到新连接。

**验收**：
- 新增 `http3_goaway_test.cpp`：客户端/服务端双向 GOAWAY 场景。
- interop Runner 的 `goodput` 测试在 server 滚动重启下通过。

#### A.2.3 HTTP/3 FrameDecoder latent bug

**问题**（TODO.md 第 24 行）：
> unknown frame type state corruption when length varint is incomplete across two OnData calls.

**修复方案**：
1. 引入显式状态机：`kExpectType → kExpectLength → kExpectPayload → kSkipUnknown`。
2. varint 解析跨 buffer 时保存中间状态（已消费字节数 + 累计值）。
3. 新增单测：将任意 3-byte varint 在所有位置切分、fuzz 随机切分点。

#### A.2.4 CRYPTO stream 重传

**问题**：
```20:quicX/src/quic/stream/if_stream.cpp
// return; // TODO crypto stream need resend
```

**修复方案**：
1. 将 CRYPTO 帧纳入 `LossDetection` 统一重传路径。
2. 握手态 CRYPTO 丢失触发 PTO 立即重传（RFC 9002 §6.2）。

**验收**：单测模拟 Initial/Handshake CRYPTO 丢包，验证握手仍在 1-RTT 内完成。

#### A.2.5 SETTINGS 帧 DoS

**问题**：
```70:quicX/src/http3/frame/settings_frame.cpp
while (len > 0) {  // TODO: check max loop times
```

**修复方案**：
- 限制 SETTINGS 项数 ≤ 32，重复项 / 未知项走 RFC 9114 §7.2.4.1 规则（部分必须拒绝、部分忽略）。
- 超限返回 `H3_EXCESSIVE_LOAD` 并关闭连接。

#### A.2.6 DCID 轮换（连接迁移）

**问题**（TODO.md 第 20 行）：连接迁移后未轮换 DCID。

**修复方案**：
1. `connection_path_manager.cpp` 在 `OnPathValidated` 后调用 `connection_id_manager_->RetireAndIssueNew()`。
2. 检查 `active_connection_id_limit` 满足 RFC 9000 §5.1.1 最小值 2。

**验收**：新增迁移测试，抓包验证新路径首包携带新 DCID。

#### A.2.7 HTTP/3 事件循环固定 100ms

**问题**：
```142:quicX/src/http3/connection/if_connection.cpp
100);  // 100ms TODO, do not use fix time
```

**修复方案**：改为 `timer_queue_->NextExpireTimeMs()`，无任务时阻塞到下个 timer。

### A.3 互通矩阵上榜（Week 3）

> **现状**：`test/interop/interop_runner.py` 已支持 12 种实现的 N×N 矩阵，`Dockerfile` / `run_endpoint.sh` / `manifest.json` 符合 quic-interop-runner 规范。local 14/14、Docker 13/14 通过。本节目标是**把已有的互通能力推向公开榜单**，并把跨实现失败项逐条修复。

#### A.3.1 镜像推送（已具备 Dockerfile，缺 CI）

1. 新增 `.github/workflows/interop-image.yml`：main 分支每次合并触发
   - `docker buildx build -f test/interop/Dockerfile`
   - 同时打 `ghcr.io/quicx/quicx-interop:latest` 与 `ghcr.io/quicx/quicx-interop:{git-sha}`
   - 推送到 GHCR（需 `GITHUB_TOKEN` 写 packages 权限）
2. 镜像 README 写清楚支持的测试列表（与 `manifest.json` 保持一致）。

#### A.3.2 提交官方 PR 上榜

1. Fork [quic-interop/quic-interop-runner](https://github.com/quic-interop/quic-interop-runner)。
2. 在 `implementations.json` 加入 quicX entry，指向 `ghcr.io/quicx/quicx-interop:latest`。
3. 本地先用官方 Runner 跑一遍矩阵（`./run.py --client quicx --server quiche` 等）。
4. 提 PR，附上本地矩阵结果截图。
5. 合并后，在 [interop.seemann.io](https://interop.seemann.io) 出现后，给 README 加徽章。

#### A.3.3 跨实现失败项逐条修复

按 `test/interop/INTEROP_improvement_plan.md` 第 6 阶段记录，当前跨实现仍有明显失败项：

| 方向 | 场景 | 当前状态 | 排查方向 |
|---|---|---|---|
| aioquic ↔ quicX | transfer | ❌ 超时 | aioquic 大文件流控 / MAX_STREAM_DATA 节奏 |
| aioquic ↔ quicX | retry | ❌ | Retry 包解析、token 验证 |
| aioquic ↔ quicX | resumption | ❌ | session ticket 互通 |
| aioquic ↔ quicX | zerortt | ❌ | 0-RTT 早期数据 |
| aioquic ↔ quicX | chacha20 / keyupdate | ❌ 超时 | TLS 密码套件、key phase 协商 |
| aioquic ↔ quicX | http3 | ❌ | HTTP/3 跨实现 |
| ngtcp2 ↔ quicX | handshake | ❌ | Connection timeout |
| quic-go ↔ quicX | handshake | ❌ | `SSL_do_handshake` 失败，怀疑 20-byte CID |

**验收门槛**（Phase A 结束时）：
- 与 quiche / ngtcp2 / quic-go 三个主流实现在 `handshake` / `transfer` / `retry` / `resumption` / `multiconnect` 场景至少双向 ≥ 80% 通过；
- 官方榜单 [interop.seemann.io](https://interop.seemann.io) 上 quicX 列可见。

### A.4 治理与发布（Week 4-5）

#### A.4.1 必需的社区文件

| 文件 | 内容要点 |
|---|---|
| `CONTRIBUTING.md` | 代码风格、提交规范（Conventional Commits）、DCO / CLA、PR 流程 |
| `CODE_OF_CONDUCT.md` | 采用 Contributor Covenant v2.1 |
| `SECURITY.md` | 披露邮箱 `security@quicx.org`、PGP key、90 天协调披露期 |
| `CHANGELOG.md` | Keep-a-Changelog 格式，初始 `v0.1.0` |
| `GOVERNANCE.md` | Maintainer / Reviewer / Contributor 角色定义 |
| `MAINTAINERS` | 初始维护者列表 |
| `CODEOWNERS` | 按目录划分 review 责任 |
| `.github/ISSUE_TEMPLATE/` | bug / feature / question 三模板 |
| `.github/PULL_REQUEST_TEMPLATE.md` | 清单式 checklist |

#### A.4.2 SemVer & 版本化

- 新增 `include/quicx/version.h`：`QUICX_VERSION_MAJOR/MINOR/PATCH`。
- 打 tag `v0.1.0`，遵循 SemVer。
- `release.yml` 自动：
  - 从 `CHANGELOG.md` 截取段落作为 release notes
  - 构建 Linux(x64/arm64) / macOS(x64/arm64) / Windows(x64) 静态库 + 头文件 tarball
  - 生成 SBOM（CycloneDX）与 sigstore 签名
  - 发布 GitHub Release

#### A.4.3 README 徽章与展示

- CI 状态、覆盖率、CodeQL、互通榜链接、SLSA level、License、Release 版本。

### A.5 Phase A 验收清单

- [ ] 8 个 GitHub Actions workflow 全部上线且稳定绿
- [ ] 覆盖率 ≥ 75%
- [ ] A.2 的 7 个 TODO 清零
- [ ] 互通镜像推送到 `ghcr.io/quicx/quicx-interop:latest`
- [ ] 官方 interop 榜出现 quicX 列
- [ ] 跨实现失败项修复进度 ≥ 80%（与 quiche / ngtcp2 / quic-go 的核心 5 场景）
- [ ] 社区治理文件齐全
- [ ] `v0.1.0` Release 发布
- [ ] README / 文档首页更新

---

## 四、Phase B：协议完整与性能对齐（6-8 周，P1）

**目标**：发布 `v0.2.0`，协议扩展补齐、单核性能对标 quiche、提供稳定 C ABI。

### B.1 协议扩展实现（Week 7-10）

#### B.1.1 DATAGRAM（RFC 9221）

**模块新增**：
- `src/quic/frame/datagram_frame.h/cpp`（类型 `0x30` / `0x31`）
- `src/quic/stream/datagram_stream.h/cpp`（逻辑"伪流"）
- 握手传输参数 `max_datagram_frame_size`（0x0020）
- 公共 API：
  ```cpp
  bool SendDatagram(std::span<const uint8_t> data);
  void SetDatagramHandler(std::function<void(std::span<const uint8_t>)>);
  ```
- 与拥塞控制 / 流控协同（DATAGRAM 计入 cwnd 但不可重传）。

**验收**：interop Runner `datagram` 测试通过；吞吐 benchmark 报告。

#### B.1.2 ACK Frequency（draft-ietf-quic-ack-frequency）

- 新增 `AckFrequencyFrame` / `ImmediateAckFrame`。
- 新增传输参数 `min_ack_delay`（0xff04de1b）。
- 发送端根据 BDP 动态调整 `ack_eliciting_threshold` / `request_max_ack_delay`。

#### B.1.3 Path MTU Discovery（RFC 8899）

- 实现 DPLPMTUD 状态机：`Base → Search → SearchComplete → Error`。
- 用 PING + PADDING 探测，步长 1200 → 1400 → 1500。
- 与 send_stream 动态 MTU 联动。

#### B.1.4 HTTP/3 Extensible Priorities（RFC 9218）

- 新增 `PriorityUpdateFrame`（control stream 0xf0700 / 0xf0701）。
- 解析 `Priority:` HTTP 头。
- 调度器按 `urgency` + `incremental` 排队。

#### B.1.5 GREASE

- 传输参数随机注入保留区间（`0x1a2a3a4a5a6a7a8a` 模式）。
- 可选发送 reserved frame type（与 RFC 9000 §12.4 兼容）。
- Version Negotiation 中混入 GREASE version。

#### B.1.6 Stateless Reset 完善

- `stateless_reset_token` 派生（HMAC-SHA256(server_key, connection_id)）。
- Key 支持在线轮换（不影响已建连接）。
- 收到无法关联的包时发送 Stateless Reset。

### B.2 性能攻关（Week 8-12）

> **基线**：`BufferReader` 用户态零拷贝已经开启；ECN 全链路已通；`sendmmsg`/`recvmmsg` 已在 `io_handle` 封装但**主路径尚未调用**。本节重心是"把封装接到主路径上 + 新增 GSO/REUSEPORT/MSG_ZEROCOPY"。

#### B.2.1 让 `sendmmsg` / `recvmmsg` 真正上线（低风险，高收益，先做）

**现状**：
- `io_handle::SendmMsg` / `RecvmMsg` 已在 Linux 封装 `sendmmsg(2)`/`recvmmsg(2)`，Windows 以循环 `WSASendMsg`/`WSARecvMsg` 实现，macOS 以循环 `sendmsg`/`recvmsg` 兼容（`SendmMsg` 是空 stub，需补上）。
- 但 `src/quic/quicx/msg_parser.cpp`、`worker*.cpp` 主路径仍然是 per-packet `SendTo` / `RecvFromWithEcn`。

**改造**：
1. `msg_parser` 收包改用 `RecvmMsg`，一次最多 64 包，按源地址分发到 connection。
2. 发送路径聚合同 path 的待发包，调用 `SendmMsg`。
3. ECN 场景合并到 `recvmmsg + cmsg` 解析（按 packet 解析 `IP_TOS` / `IPV6_TCLASS`）。
4. 补齐 macOS `SendmMsg` stub（循环 `sendmsg`），保持接口一致。
5. 运行时特性探测（老内核 fallback 到 `RecvFromWithEcn`）。

**验收**：
- 新增 `benchmark/syscall_batching_bench`：对比 per-packet 与 mmsg 的 syscall 次数和吞吐。
- 多连接高 QPS 场景 syscall 次数下降 ≥ 80%。

#### B.2.2 Linux GSO（`UDP_SEGMENT`）

- `io_handle` 新增 `SendBatchGso(sockfd, buf, total_len, gso_size, dest)`：
  - 使用 `sendmsg` + `cmsg(SOL_UDP, UDP_SEGMENT)` 发送 N 个等长分段。
- 发送聚合器（`src/quic/quicx/*send*`）按 path MTU 聚合同 path / 同对端的待发包。
- 运行时探测内核支持（`setsockopt(SOL_UDP, UDP_SEGMENT)` 是否成功），不支持则 fallback 到 B.2.1 的 `sendmmsg` 路径。
- 预期吞吐：+3~5x（与 quiche / msquic 基准一致）。

#### B.2.3 SO_REUSEPORT + 多 worker + CPU 亲和

- 服务端支持 `NumWorkers = N`，每个 worker 绑定独立 `SO_REUSEPORT` socket + `pthread_setaffinity_np`。
- Connection ID 生成规则把 worker-id 编入前 N 位，保证后续包由同一 worker 处理（必要时配 eBPF `SO_ATTACH_REUSEPORT_EBPF`）。
- 与已有的 `worker` 架构对齐：`worker_server.cpp` 已有多 worker 骨架，需接 socket 分流。

#### B.2.4 `MSG_ZEROCOPY` 发包（大包场景）

- 开启 `SO_ZEROCOPY`，`sendmsg(flags=MSG_ZEROCOPY)`。
- 通过 `recvmsg(MSG_ERRQUEUE)` 异步回收缓冲所有权。
- 仅对 ≥ 某阈值的包启用（默认 1400B 以上），避免小包 overhead。
- 与 GSO 叠加使用时注意路径对齐。

#### B.2.5 io_uring 后端（实验）

- `src/common/network/linux_uring/`：基于 liburing 实现 receiver/sender。
- 要求内核 ≥ 6.0；运行时选择后端（环境变量 `QUICX_IO_BACKEND=uring|epoll`）。
- 先作为 `--enable-experimental` 选项。

#### B.2.6 性能 benchmark 套件

- 新增 `benchmark/throughput/` 使用 `iperf3`-style 自建基准。
- 对比矩阵：
  - quicX（per-packet / mmsg / mmsg+GSO / mmsg+GSO+ZEROCOPY / io_uring）
  - quiche / msquic / lsquic 基准
- 输出 JSON 报告，CI 每夜跑，与 baseline 比对回归（> 5% 下跌报警）。

### B.3 稳定 C ABI（Week 10-12）

#### B.3.1 头文件设计

```c
// include/quicx/quicx.h
typedef struct quicx_engine quicx_engine_t;
typedef struct quicx_conn   quicx_conn_t;
typedef struct quicx_stream quicx_stream_t;

quicx_engine_t* quicx_engine_new(const quicx_config_t* cfg);
void            quicx_engine_free(quicx_engine_t*);
int             quicx_engine_poll(quicx_engine_t*, int timeout_ms);

quicx_conn_t*   quicx_connect(quicx_engine_t*, const char* host, uint16_t port);
int             quicx_conn_close(quicx_conn_t*, uint64_t app_err, const char* reason);

quicx_stream_t* quicx_stream_open(quicx_conn_t*, int bidi);
ssize_t         quicx_stream_write(quicx_stream_t*, const uint8_t*, size_t, int fin);
ssize_t         quicx_stream_read (quicx_stream_t*, uint8_t*, size_t, int* fin);
```

- opaque handle，不暴露 C++ 类型。
- 错误码统一：`quicx_error_t` 枚举。
- 版本宏 `QUICX_ABI_VERSION`，破坏性变更 bump major。

#### B.3.2 交付物

- 共享库 `libquicx.so.0` / `libquicx.dylib` / `quicx.dll`。
- `quicx.pc` pkg-config 文件。
- CMake package config（`quicxConfig.cmake`）。
- vcpkg port + Conan recipe。

### B.4 qlog 端到端验证

- 每个 interop 测试用例产出 qlog，经 [qvis](https://qvis.quictools.info/) 加载无错。
- 新增 schema conformance 测试（qlog JSON Schema 校验）。

### B.5 API 参考文档

- doxygen 配置 + mkdocs-material 主题。
- 发布到 `https://quicx.github.io/quicx/api/`。
- 每个公开 API 附带完整 `@param` / `@return` / `@throws` / Example。

### B.6 Phase B 验收清单

- [ ] DATAGRAM / ACK-Frequency / DPLPMTUD / Extensible Priorities / GREASE / Stateless Reset 全部实现并过 interop
- [ ] `sendmmsg` / `recvmmsg` 上线主路径，syscall 次数下降 ≥ 80%
- [ ] Linux GSO（`UDP_SEGMENT`）上线，同路径吞吐 ≥ +3x
- [ ] `SO_REUSEPORT` 多 worker 扩展到 N 核，近线性
- [ ] 单核吞吐 ≥ quiche 90%（对同等硬件）
- [ ] C ABI 发布并通过 `cpp → c → rust` 最小 demo
- [ ] vcpkg / Conan port 合并
- [ ] qvis 可视化无错
- [ ] doxygen 站点上线
- [ ] `v0.2.0` Release 发布

---

## 五、Phase C：生态与高级能力（持续，P2）

**目标**：进入主流生态，形成正向循环。

### C.1 WebTransport 支持

- HTTP/3 CONNECT extended（`:protocol: webtransport`）。
- Capsule 协议（draft-ietf-masque-h3-datagram）。
- JS 端以 Chromium 自带 WebTransport API 对接。
- Demo：基于 quicX 的 WebTransport 聊天 / 文件传输。

### C.2 生态集成

#### C.2.1 curl 集成

- 实现 `vquic/curl_quicx.c` 后端，对齐 `curl_vquic_*` 接口。
- 向 curl 上游提交 PR（需要长期维护承诺）。
- 构建选项 `./configure --with-quicx=/usr`。

#### C.2.2 nginx 模块

- 实现 `ngx_http_v3_quicx_module`，替换 boringSSL-quiche 组合。
- 提供 quay/docker `nginx-quicx` 镜像。

#### C.2.3 Envoy / gRPC

- 作为 Envoy transport socket 插件（需 C ABI 稳定）。
- gRPC-Core 的 QUIC transport（目前 gRPC 主用 msquic，可作为备选）。

### C.3 语言绑定

| 语言 | 技术方案 | 交付 |
|---|---|---|
| Rust | `quicx-rs` via `bindgen` + safe wrapper | crates.io 发布 |
| Go | cgo + channel 化 API | `go get` 可用 |
| Python | cffi + asyncio wrapper | PyPI `quicx` |
| Node.js | N-API + stream 接入 | npm `@quicx/node` |
| Java/Kotlin | JNI + Project Panama（JDK 22+） | Maven Central |

### C.4 性能极致

#### C.4.1 io_uring 后端

- `src/common/network/linux_uring/`，要求内核 ≥ 6.0。
- 与传统 epoll 后端并存，运行时选择。

#### C.4.2 AF_XDP 后端

- 绕过内核协议栈，ns 级延迟。
- 定位于 CDN / LB 等专业部署。

#### C.4.3 公开性能对比报告

- 固定硬件（AWS c7i.4xlarge / GCP c3-standard-8）。
- 对比 quicX / quiche / msquic / lsquic / ngtcp2。
- 指标：握手 RTT、吞吐、CPU 利用率、内存占用、连接数。
- 报告托管于 `https://quicx.github.io/quicx/bench/`。

### C.5 长期治理

- 加入 [Linux Foundation](https://www.linuxfoundation.org/) / [CNCF](https://www.cncf.io/) Sandbox（时机合适时）。
- 每季度发布 Roadmap 更新。
- 建立 Discord / Slack 社区。
- 年度贡献者大会（线上）。

### C.6 Phase C 验收清单

- [ ] WebTransport demo 运行
- [ ] curl / nginx / Envoy 至少一项 upstream 合并
- [ ] Rust / Go / Python 三语言绑定在对应包管理器可用
- [ ] io_uring 后端进入 experimental
- [ ] 公开 benchmark 报告上线
- [ ] `v1.0.0` Release（ABI 稳定承诺）

---

## 六、里程碑与版本规划

| 版本 | 目标日期 | Phase | 主题 |
|---|---|---|---|
| `v0.1.0` | 2026-06 | A 完成 | alpha 可试用、CI 全绿、官方互通榜可见 |
| `v0.2.0` | 2026-08 | B 完成 | 协议扩展齐、性能对齐、C ABI |
| `v0.3.0` | 2026-10 | C 部分 | WebTransport + 2 个语言绑定 |
| `v0.9.0-rc` | 2026-12 | C 部分 | curl / nginx 集成合并 |
| `v1.0.0` | 2027-Q1 | C 完成 | ABI 稳定承诺、长期维护版 |

---

## 七、质量门禁（Definition of Done）

任意 PR 合并前必须满足：

1. **编译**：全矩阵 0 warning（`-Werror` 开启）。
2. **测试**：相关单测覆盖率 ≥ 90%，全量单测 ≥ 75%。
3. **Sanitizer**：ASan / TSan / UBSan 0 report。
4. **Fuzz**：相关 fuzzer 跑 60s 无 crash。
5. **Interop**：若改动协议层，本地 interop 全绿。
6. **文档**：公开 API 变更 → `CHANGELOG.md` + doxygen 注释。
7. **Review**：至少 1 个 maintainer approve；协议层改动需 2 个。
8. **格式**：clang-format / clang-tidy pass。

---

## 八、风险与缓解

| 风险 | 可能性 | 影响 | 缓解 |
|---|---|---|---|
| 维护者精力不足 | 高 | 高 | 优先 Phase A，吸引外部 Contributor |
| 协议扩展不断演进（如 QUIC v2） | 中 | 中 | 保留 version 协商骨架，扩展 per-version 行为 |
| 性能优化引入不稳定 | 中 | 高 | 功能开关（runtime + 编译时），nightly 灰度 |
| curl / nginx PR 迟迟未合 | 高 | 中 | 先提供 out-of-tree patch，Docker 镜像独立分发 |
| OSS-Fuzz 发现大量漏洞 | 中 | 中 | 严格执行 90 天披露窗口，修复优先级高于新 feature |
| C ABI 设计失误需破坏 | 中 | 高 | v0.x 阶段明示不稳定；v1.0 前第三方 review（邀请 lsquic/msquic 维护者） |
| 跨平台编译问题长尾 | 高 | 低 | 逐步启用，Windows / FreeBSD / musl 各一独立 CI job |

---

## 附录 A：关键 TODO 清单

按严重度排序：

| # | 位置 | 问题 | 严重度 | 修复 Phase |
|---|---|---|---|---|
| 0 | `src/http3/qpack/blocked_registry.*` + `src/http3/stream/qpack_decoder_receiver_stream.cpp` | ✅ **已修（v1.2）** QPack Section Ack / Stream Cancellation 按 wire 上实际携带的 Stream ID 匹配注册项，符合 RFC 9204 §4.4.1/§4.4.2 | — | 已完成 |
| 1 | `src/http3/connection/connection_server.cpp:308` | HTTP/3 GOAWAY 未实现 | 高 | A |
| 2 | `src/quic/stream/send_stream.cpp:246` | STREAM 帧大小硬编码 1300 | 高 | A |
| 3 | `src/http3/frame/settings_frame.cpp:70` | SETTINGS 循环无上限（DoS） | 高 | A |
| 4 | TODO.md 第 24 行 | HTTP/3 FrameDecoder varint 跨包状态腐蚀 | 高 | A |
| 5 | TODO.md 第 20 行 | 连接迁移未轮换 DCID | 中高 | A |
| 6 | `src/quic/stream/if_stream.cpp:20` | CRYPTO stream 重传被注释 | 中 | A |
| 7 | `src/http3/connection/if_connection.cpp:142` | 事件循环固定 100ms 轮询 | 中 | A |
| 7b | §2.8 S1~S5 | 跨层一致性债审计（wire 字段 vs. 本地 key） | 中 | A |
| 8 | `src/quic/quicx/worker*.cpp` | `SendmMsg`/`RecvmMsg` 已封装但主路径未调用 | 中 | B |
| 9 | `src/common/network/macos/io_handle.cpp:216` | macOS `SendmMsg` 是空 stub | 低 | B |
| 10 | 无 | DATAGRAM 完全缺失 | 中 | B |
| 11 | 无 | ACK Frequency 缺失 | 中 | B |
| 12 | 无 | DPLPMTUD 缺失 | 中 | B |
| 13 | 无 | HTTP/3 Extensible Priorities 缺失 | 中 | B |
| 14 | 无 | GREASE 未显式注入 | 低 | B |
| 15 | 无 | Stateless Reset token/key 机制不完整 | 中 | B |
| 16 | 无 | Linux GSO (`UDP_SEGMENT`) 缺失 | 中 | B |
| 17 | 无 | `SO_REUSEPORT` 多 worker 分流缺失 | 中 | B |
| 18 | 无 | `MSG_ZEROCOPY` 发包路径缺失 | 低 | B |

---

## 附录 B：参考资料

### B.1 IETF 标准

- [RFC 9000 - QUIC Transport](https://datatracker.ietf.org/doc/html/rfc9000)
- [RFC 9001 - Using TLS for QUIC](https://datatracker.ietf.org/doc/html/rfc9001)
- [RFC 9002 - QUIC Loss Detection and Congestion Control](https://datatracker.ietf.org/doc/html/rfc9002)
- [RFC 9114 - HTTP/3](https://datatracker.ietf.org/doc/html/rfc9114)
- [RFC 9204 - QPACK](https://datatracker.ietf.org/doc/html/rfc9204)
- [RFC 9218 - Extensible Prioritization Scheme for HTTP](https://datatracker.ietf.org/doc/html/rfc9218)
- [RFC 9221 - QUIC Datagram](https://datatracker.ietf.org/doc/html/rfc9221)
- [RFC 9287 - GREASE](https://datatracker.ietf.org/doc/html/rfc9287)
- [RFC 9307 - qlog main schema](https://datatracker.ietf.org/doc/html/rfc9307)
- [RFC 8899 - DPLPMTUD](https://datatracker.ietf.org/doc/html/rfc8899)
- [draft-ietf-quic-ack-frequency](https://datatracker.ietf.org/doc/draft-ietf-quic-ack-frequency/)
- [draft-ietf-webtrans-http3](https://datatracker.ietf.org/doc/draft-ietf-webtrans-http3/)

### B.2 对标实现

- [Cloudflare quiche](https://github.com/cloudflare/quiche)
- [Microsoft msquic](https://github.com/microsoft/msquic)
- [LiteSpeed lsquic](https://github.com/litespeedtech/lsquic)
- [quic-go](https://github.com/quic-go/quic-go)
- [ngtcp2](https://github.com/ngtcp2/ngtcp2)
- [picoquic](https://github.com/private-octopus/picoquic)

### B.3 测试基础设施

- [quic-interop-runner](https://github.com/quic-interop/quic-interop-runner)
- [interop.seemann.io](https://interop.seemann.io/)
- [qvis](https://qvis.quictools.info/)
- [OSS-Fuzz](https://github.com/google/oss-fuzz)

### B.4 工程最佳实践

- [Keep a Changelog](https://keepachangelog.com/)
- [Semantic Versioning](https://semver.org/)
- [Conventional Commits](https://www.conventionalcommits.org/)
- [Contributor Covenant](https://www.contributor-covenant.org/)
- [SLSA](https://slsa.dev/)

---

## 附录 C：下一步行动（滚动清单）

> 本节是"接下来做什么"的可执行清单，按当前时间点排序。完成一项就划掉，并在 v1.x 版本备注。每月评审时重排。

### C.1 本周（Week 1，立刻可开工，低风险）

1. **[治理基础] 落地 4 个必备治理文件**（对应 A.4.1）：`CONTRIBUTING.md` / `CODE_OF_CONDUCT.md` / `SECURITY.md` / `CHANGELOG.md`。其中 `CHANGELOG.md` 第一条就写入 v1.2 的 QPack 修复。无代码风险，纯文档，是后续 CI 和 Release 的依赖项。
2. **[CI 底盘] 上线最小 `ci.yml`**（对应 A.1.1）：Linux x86_64 / gcc-13 / Debug + Release 两档，跑 `quicx_utest` + CI 本地脚本。先不铺全矩阵，跑通即可。这是后续所有 workflow 的模板。
3. **[跨层一致性审计 step-1]**（对应 §2.8）：新建 `docs/AUDIT_wire_vs_local_key.md`，对 S1~S5 五项逐一读源码 + 贴 RFC 引文做自审。产出中如有修复点，降级为具体 issue。

### C.2 两周内（Week 2-3，Phase A 主攻）

4. **[TODO #4] HTTP/3 FrameDecoder varint 跨包状态腐蚀**（A.2.3）。"高"严重度，修一次就再也不回来；新增 fuzz 切分测试。
5. **[TODO #2] `send_stream.cpp` 硬编码 1300 → 路径 MTU 贯穿**（A.2.1）。握手 `max_udp_payload_size` 传输参数接上，为 Phase B DPLPMTUD 铺路。
6. **[TODO #3] SETTINGS 帧 DoS**（A.2.5）。一次性修掉。
7. **[TODO #1] HTTP/3 GOAWAY**（A.2.2）。依赖 #5 的 MTU 贯穿完成后再做，避免 merge 冲突。

### C.3 一个月内（Week 3-5）

8. **[CI 扩展] Sanitizer + 覆盖率 workflow**（A.1.1 的 `sanitizer.yml` + `coverage.yml`）。ASan/TSan/UBSan 任一冒出 report，优先于 feature 修。
9. **[TODO #5] DCID 轮换 + 连接迁移闭环**（A.2.6）。迁移抓包测试作验收。
10. **[TODO #6] CRYPTO stream 重传纳入 LossDetection**（A.2.4）。
11. **[TODO #7] 事件循环去 100ms 固定轮询**（A.2.7）。
12. **[互通] 镜像推送到 GHCR + 提官方 interop-runner PR**（A.3.1/A.3.2）。这是 `v0.1.0` 的"对外可见"里程碑，完成后 README 挂徽章。
13. **[互通] aioquic/ngtcp2/quic-go 的 `handshake` / `transfer` / `retry` 三项跨实现双向打通**（A.3.3）。

### C.4 Phase A 收尾（Week 5-6）

14. **[Release] `v0.1.0` tag + GitHub Release + SBOM**（A.4.2）。
15. **[README] 徽章 + Getting Started**（A.4.3）。
16. 本文档同步推到 v1.3：记录 Phase A 完成回顾、校准 Phase B 顺序。

### C.5 决策待办（需维护者确认）

以下事项不在本助手判断范围内，列出等决策：
- `security@quicx.org` 邮箱是否已注册？未注册则先用个人邮箱占位。
- `ghcr.io/quicx/` org 是否已创建？影响 A.3.1 的镜像路径。
- C ABI（B.3）vs WebTransport（C.1）的相对优先级：v0.3 带谁、v0.4 带谁。
- 是否接受 DCO / 是否需要 CLA（`CONTRIBUTING.md` 要求先定）。

---

**文档维护者**：quicX maintainers  
**最近更新**：2026-04-30（v1.2：QPack decoder stream 语义修复后）  
**下次评审**：2026-05-31（每月滚动）
