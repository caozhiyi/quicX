# quicX 互操作性测试使用指导

本文档只描述"如何运行 quicX 的互操作性（interop）测试"。

测试进度与结果矩阵见 [`reports/interop_status.md`](../reports/interop_status.md)。

框架原理与官方协议细节见 [`guide/interop_overview.md`](./interop_overview.md)。

---

## 1. 总体方式

互操作测试**直接使用官方 [quic-interop-runner](https://github.com/quic-interop/quic-interop-runner)** 执行。

quicX 仓库只负责提供官方 runner 需要的构建产物，不再维护自建 runner：

```
test/interop/
├── CMakeLists.txt      # interop 二进制的构建规则
├── interop_server.cpp  # interop 服务端源码
├── interop_client.cpp  # interop 客户端源码
├── Dockerfile          # 构建 quicx-interop 镜像
└── run_endpoint.sh     # 容器入口脚本（实现官方 runner 的环境变量协议）
```

---

## 2. 构建 quicX Docker 镜像

官方 runner 通过**镜像名**调度所有实现，需要先在 quicX 仓库根目录构建镜像：

```bash
docker build -t quicx-interop:latest -f test/interop/Dockerfile .
```

要点：

- 镜像基于官方 `martenseemann/quic-network-simulator-endpoint`，`ENTRYPOINT` 为 `/run_endpoint.sh`，完全遵循 [quic-network-simulator](https://github.com/quic-interop/quic-network-simulator) 的端点约定
- 镜像内会从源码编译 `interop_server` / `interop_client`，无需预建本地二进制
- 改动 C++ 代码后需**重新构建镜像**再跑测试（镜像名带 `:latest`，runner 每次用本地缓存中的同名镜像）

如需推送到镜像仓库（如 GHCR）：

```bash
docker tag quicx-interop:latest ghcr.io/<owner>/quicx-interop:latest
docker push ghcr.io/<owner>/quicx-interop:latest
```

---

## 3. 准备官方 runner

```bash
git clone https://github.com/quic-interop/quic-interop-runner
cd quic-interop-runner
pip3 install -r requirements.txt
```

### 3.1 依赖

| 依赖 | 说明 |
|------|------|
| Docker + docker compose | 拉起 client / server / sim 三个容器 |
| Python 3 | 运行 runner |
| Wireshark **4.5.0+** | runner 用 tshark 解析 sim 抓包判定结果（resumption / zerortt / keyupdate 等场景） |
| Linux 宿主 | 跑 IPv6 场景前需 `sudo modprobe ip6table_filter` |

### 3.2 注册 quicx

确认 `implementations_quic.json` 中包含 quicx 条目（未包含则添加）：

```json
"quicx": {
  "image": "quicx-interop:latest",
  "url": "https://github.com/caozhiyi/quicX",
  "role": "both"
}
```

`role: both` 表示 quicx 可作为 server 也可作为 client 参与矩阵。

---

## 4. 标准测试命令

所有命令在官方 runner 仓库根目录执行。

### 4.1 quicX 作为 client

```bash
# 与指定 server 互通
python3 run.py -c quicx -s quiche,ngtcp2,quic-go

# 与全部 server 互通（不含仅支持 client 的实现）
python3 run.py -c quicx
```

### 4.2 quicX 作为 server

```bash
# 与指定 client 互通
python3 run.py -s quicx -c ngtcp2,picoquic,aioquic

# 与全部 client 互通
python3 run.py -s quicx
```

### 4.3 指定场景 / 单点复现

```bash
# 只跑 handshake、transfer、v2 三个场景
python3 run.py -s quicx -c ngtcp2 -t handshake,transfer,v2

# 复现单个失败项，带调试日志
python3 run.py -d -s quicx -c ngtcp2 -t v2
```

### 4.4 全矩阵

```bash
# 所有 server × 所有 client × 所有场景（非常耗时，慎用）
python3 run.py
```

### 4.5 常用参数

| 参数 | 含义 |
|------|------|
| `-s LIST` | server 实现（逗号分隔） |
| `-c LIST` | client 实现（逗号分隔） |
| `-t LIST` | 测试场景（逗号分隔） |
| `-d` | 调试日志 |
| `-j FILE` | 结果矩阵输出为 JSON |
| `-m` | 结果矩阵输出为 Markdown |
| `-l DIR` | 日志目录（默认 `logs/`） |
| `-f` | 测试失败时保留下载文件，便于比对 |
| `-p PROTOCOL` | `quic`（默认） / `webtransport` |

完整参数见 `python3 run.py --help`。

---

## 5. 测试场景

官方 runner 当前定义 **22 个协议合规场景 + 2 个性能测算场景**。quicX 通过 `run_endpoint.sh` 的场景白名单声明支持项，不支持的场景按官方约定以退出码 **127** 退出，runner 记为 `UNSUPPORTED`。

| 场景 | 说明 | quicX |
|------|------|-------|
| `handshake` | 基础握手，下载小文件 | ✅ |
| `transfer` | 大文件传输 | ✅ |
| `retry` | 服务端强制 Stateless Retry | ✅ |
| `resumption` | 1-RTT 会话恢复（两次连接） | ✅ |
| `zerortt` | 0-RTT Early Data | ✅ |
| `http3` | HTTP/3 交互 | ✅ |
| `chacha20` | 强制使用 ChaCha20-Poly1305 | ✅ |
| `keyupdate` | 客户端触发 Key Update | ✅ |
| `v2` | QUIC v2 (RFC 9369)，版本 `0x6b3343cf` | ✅ |
| `rebind-port` | 客户端 NAT 端口重绑 | ✅ |
| `rebind-addr` | 客户端 NAT 地址重绑 | ✅ |
| `connectionmigration` | 客户端主动连接迁移 | ✅ |
| `ecn` | ECN 标记与回显 | ✅ |
| `longrtt` | 高 RTT 场景 | ❌ |
| `multiplexing` | 多路并发流 | ❌ |
| `blackhole` | 瞬时网络黑洞 | ❌ |
| `amplificationlimit` | 放大限制 | ❌ |
| `handshakeloss` / `transferloss` | 握手 / 传输丢包 | ❌ |
| `handshakecorruption` / `transfercorruption` | 握手 / 传输包损坏 | ❌ |
| `ipv6` | IPv6 连通性 | ❌ |
| `goodput` / `crosstraffic` | 吞吐 / 交叉流量测算 | ❌ |

> `run_endpoint.sh` 白名单另含 `multiconnect` / `versionnegotiation`，两者为早期自建 runner 场景，官方矩阵中不存在，保留仅为兼容。
> 场景通过率与失败根因见 [`reports/interop_status.md`](../reports/interop_status.md)。

---

## 6. 日志与排障

每次运行后，日志保存在 runner 目录的 `logs/`（可用 `-l` 覆盖）：

```
logs/
└── <server>_<client>/          # 例: quicx_ngtcp2
    └── <testcase>/             # 例: v2
        ├── output.txt          # runner 控制台输出（含失败原因）
        ├── server/             # server 端日志（stdout/stderr、qlog）
        ├── client/             # client 端日志（stdout/stderr、qlog）
        └── sim/                # 模拟器抓包 pcap
```

排障流程：

1. 看 `output.txt` 的失败原因（超时 / 文件校验失败 / 退出码 127）
2. 看 `client/` 或 `server/` 下 quicX 的日志，确认卡在握手还是传输阶段
3. 用 [qvis](https://qvis.quictools.info/) 载入 `qlog` 文件可视化
4. 用 `sim/` 的 pcap 结合 `SSLKEYLOGFILE`（NSS Key Log 格式）在 Wireshark 中解密分析

---

## 7. 相关文档

- [`reports/interop_status.md`](../reports/interop_status.md) — 当前测试进度与连通性矩阵
- [`guide/interop_overview.md`](./interop_overview.md) — 官方 interop-runner 框架原理
- [`../../internal/quic_interop_sim_issues.md`](../../internal/quic_interop_sim_issues.md) — 各对端排障笔记
- `docs/internal/improvement_plan.md` — 互操作改进计划
- `test/interop/run_endpoint.sh` — quicX 容器内启动脚本（场景允许清单）
- [quic-interop-runner](https://github.com/quic-interop/quic-interop-runner) — 官方 runner 仓库
