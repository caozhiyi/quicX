# quicX 互操作性测试使用指导

> 本文档只描述"如何运行 quicX 的互操作性（interop）测试"。
> 测试进度与结果矩阵见 [`reports/interop_status.md`](../reports/interop_status.md)。
> 框架原理与官方协议细节见 [`guide/interop_overview.md`](./interop_overview.md)。

---

## 1. 测试脚本位置

```
test/interop/
├── interop_runner.py       # 入口脚本
├── testcases.py            # 14 个场景定义
├── implementations.json    # 已接入的第三方实现清单
├── run_endpoint.sh         # quicX 容器内启动脚本
├── certs/                  # 自签证书（首次运行自动生成）
├── www/                    # 随机测试文件（首次运行自动生成）
└── logs/                   # 每次运行的 qlog / keylog / stdout / stderr
```

---

## 2. 运行前置条件

### 2.1 宿主依赖

| 依赖 | 版本 | 说明 |
|------|------|------|
| Docker | 任意新版 | 用于拉起第三方实现的镜像 |
| Docker Compose | **v2.24+**，推荐 v2.36+ | 需要 `priority` / `interface_name` 支持 |
| Python | 3.8+ | 运行 `interop_runner.py` |
| openssl | - | 自动生成自签证书 |
| CMake / C++17 编译器 | - | 仅 `--use-local-bin` 或 `--local` 需要 |

### 2.2 首次运行会自动完成

- 生成随机测试文件到 `test/interop/www/`（1KB / 1MB / 5MB 等）
- 生成自签 TLS 证书到 `test/interop/certs/`
- 拉取/构建所需 Docker 镜像（首次可能耗时数分钟，后续走本地缓存）

### 2.3 网络模式

| 模式 | 开关 | 说明 |
|------|------|------|
| ns-3 仿真网络 | 默认 | 使用官方 `quic-network-simulator`，可注入延迟/丢包/乱序，需 Linux 宿主 |
| 直连桥接 | `--no-sim` | 跳过 ns-3，client/server 直接通过 `quicnet` 桥接网段通信。macOS Docker Desktop 必选；Linux 下也更稳更快 |

### 2.4 quicX 二进制来源

| 模式 | 开关 | 说明 |
|------|------|------|
| 镜像内二进制 | 默认 | 使用 `quicx-interop:latest` 镜像中已构建好的二进制 |
| 本地二进制挂载 | `--use-local-bin` | 将宿主 `build/bin/interop_{server,client}` 挂到 quicx 容器，**改 C++ 后最快的验证方式** |
| 纯本地进程 | `--local` | 不走 Docker，直接在宿主跑两个本地二进制（仅用于 quicX ↔ quicX 自测） |

---

## 3. 构建本地 interop 二进制

当使用 `--use-local-bin` 或 `--local` 时，需要先构建：

```bash
cd /data/workspace/quicX

cmake -S . -B build -DQUICX_BUILD_INTEROP=ON
cmake --build build --target interop_server interop_client -j
```

构建产物：
- `build/bin/interop_server`
- `build/bin/interop_client`

---

## 4. 标准测试命令

### 4.1 全矩阵（推荐）

一条命令运行 quicX 与所有已接入实现的双向互操作测试：

```bash
cd /data/workspace/quicX/test/interop

python3 interop_runner.py \
    --matrix \
    --implementations all
```

参数说明：

| 参数 | 含义 |
|------|------|
| `--matrix` | 矩阵模式，遍历所有（server, client）组合 |
| `--implementations all` | 覆盖 `implementations.json` 中所有实现 |
| `--no-sim` | 跳过 ns-3 仿真，使用 Docker 桥接网络 |
| `--use-local-bin` | quicX 使用宿主本地二进制，无需重建 Docker 镜像 |

默认情况下矩阵只跑 quicX ↔ 第三方，不跑第三方 ↔ 第三方（加 `--full-matrix` 才会跑，耗时大幅增加）。

### 4.2 指定一组实现

```bash
python3 interop_runner.py --matrix \
    --implementations quicx,quiche,ngtcp2,quic-go \
    --no-sim --use-local-bin
```

### 4.3 指定单个场景

```bash
# 全矩阵中只跑 v2
python3 interop_runner.py --matrix --implementations all \
    --scenario v2 --no-sim --use-local-bin

# 非矩阵，只跑 handshake（默认 quicx ↔ quicx）
python3 interop_runner.py --scenario handshake --no-sim --use-local-bin
```

### 4.4 指定单个（server, client）组合

```bash
# quicX 服务端 ↔ ngtcp2 客户端
python3 interop_runner.py --server quicx --client ngtcp2 \
    --no-sim --use-local-bin

# ngtcp2 服务端 ↔ quicX 客户端
python3 interop_runner.py --server ngtcp2 --client quicx \
    --no-sim --use-local-bin
```

### 4.5 quicX 自测

```bash
# 本地进程模式（不依赖 Docker）
python3 interop_runner.py --local

# Docker 模式，quicX ↔ quicX
python3 interop_runner.py --no-sim --use-local-bin
```

### 4.6 输出结果到文件

```bash
# Markdown
python3 interop_runner.py --matrix --implementations all \
    --no-sim --use-local-bin \
    --output markdown --output-file logs/latest_matrix.md

# JSON
python3 interop_runner.py --matrix --implementations all \
    --no-sim --use-local-bin \
    --output json --output-file logs/latest_matrix.json
```

---

## 5. 全部 CLI 参数

| 参数 | 默认 | 含义 |
|------|------|------|
| `--matrix` | 否 | 矩阵模式 |
| `--full-matrix` | 否 | 矩阵模式下也跑第三方 ↔ 第三方 |
| `--implementations LIST` | `quicx` | 逗号分隔的实现名或 `all` |
| `--server NAME` | quicx | 非矩阵模式下的服务端实现 |
| `--client NAME` | quicx | 非矩阵模式下的客户端实现 |
| `--scenario NAME` | 全部 14 个 | 仅运行指定场景 |
| `--no-sim` | 否 | 跳过 ns-3 仿真 |
| `--use-local-bin` | 否 | quicX 使用宿主本地二进制 |
| `--local` | 否 | 纯本地进程模式（不用 Docker） |
| `--build-dir PATH` | `./build` | `--use-local-bin` 使用的 build 目录 |
| `--rebuild` | 否 | 强制重建 quicX 镜像 |
| `--timeout SEC` | 60 | 单个测试的超时 |
| `--host HOST` | localhost | 客户端目标主机名 |
| `--port N` | 443（本地 4433） | 服务器监听端口 |
| `--output FMT` | text | `text` / `markdown` / `json` |
| `--output-file PATH` | 无 | 结果写入文件 |
| `-v`, `--verbose` | 否 | debug 级别日志 |

---

## 6. 测试场景定义（14 个）

定义见 `test/interop/testcases.py`，与官方 quic-interop-runner 对齐。

| 场景 | 描述 |
|------|------|
| `handshake` | 基础握手，下载 1KB |
| `transfer` | 大文件传输（1MB + 5MB） |
| `retry` | 服务端强制 Stateless Retry |
| `resumption` | 1-RTT 会话恢复（两次连接） |
| `zerortt` | 0-RTT Early Data |
| `http3` | HTTP/3，下载 10KB + 100KB + 1MB |
| `multiconnect` | 并发 5 个客户端连接 |
| `versionnegotiation` | 版本协商（客户端发不支持版本） |
| `chacha20` | 强制使用 ChaCha20-Poly1305 |
| `keyupdate` | 客户端触发 Key Update |
| `v2` | QUIC v2 (RFC 9369)，版本 `0x6b3343cf` |
| `rebind-port` | 客户端 NAT 端口重绑 |
| `rebind-addr` | 客户端 NAT 地址重绑 |
| `connectionmigration` | 客户端主动连接迁移 |

### 结果判定

| 结果 | 含义 |
|------|------|
| `PASSED` | 客户端退出码 0，且下载文件与源文件 byte-by-byte 一致 |
| `FAILED` | 进程异常 / 文件校验失败 / 超时 |
| `UNSUPPORTED` | 任一方声明不支持该场景（退出码 127），或当前网络模式无法提供该场景（如 `rebind-*` 需要 ns-3） |
| `SKIPPED` | 实现在 `implementations.json` 中声明不作为 server/client |

---

## 7. 已接入的第三方实现

见 `test/interop/implementations.json`。

| 实现 | 镜像 | 角色 |
|------|------|------|
| quicx | `quicx-interop:latest`（本地构建） | both |
| quiche | `cloudflare/quiche-qns:latest` | both |
| ngtcp2 | `ghcr.io/ngtcp2/ngtcp2-interop:latest` | both |
| quic-go | `martenseemann/quic-go-interop:latest` | both |
| mvfst | `ghcr.io/facebook/proxygen/mvfst-interop:latest` | both |
| quinn | `stammw/quinn-interop:latest` | both |
| aioquic | `aiortc/aioquic-qns:latest` | both |
| picoquic | `privateoctopus/picoquic:latest` | both |
| neqo | `ghcr.io/mozilla/neqo-qns:latest` | both |
| lsquic | `litespeedtech/lsquic-qir:latest` | both |
| msquic | `ghcr.io/microsoft/msquic/qns:main` | both |
| s2n-quic | `ghcr.io/aws/s2n-quic/s2n-quic-qns:latest` | both |

---

## 8. 日志目录结构

```
test/interop/logs/
├── handshake/
│   ├── quicx_ngtcp2/                 # server=quicx, client=ngtcp2
│   │   ├── server/
│   │   │   ├── log.txt
│   │   │   ├── container_stdout.log
│   │   │   ├── container_stderr.log
│   │   │   └── qlog/*.qlog
│   │   └── client/
│   │       ├── container_stdout.log
│   │       ├── container_stderr.log
│   │       └── qlog/*.qlog
│   └── ngtcp2_quicx/...
├── transfer/...
├── v2/...
└── ...
```

排障流程：
1. 看 `container_stderr.log` 找异常栈
2. 看 `log.txt`（quicX 业务日志）确认握手/传输阶段
3. 用 [qvis](https://qvis.quictools.info/) 载入 `qlog/*.qlog` 可视化
4. 需要抓包时，结合 `keys.log` 用 Wireshark 解密 UDP 流

---

## 9. 复现单条结果

```bash
# 例：复现 v2 场景下 quicX server ↔ ngtcp2 client
python3 interop_runner.py --scenario v2 \
    --server quicx --client ngtcp2 \
    --no-sim --use-local-bin -v
```

`-v` 打开 debug 日志，所有请求的详细容器命令、挂载、环境变量都会打印。

---

## 10. 相关文档

- [`reports/interop_status.md`](../reports/interop_status.md) — 当前测试进度与连通性矩阵
- [`guide/interop_overview.md`](./interop_overview.md) — 官方 interop-runner 框架原理
