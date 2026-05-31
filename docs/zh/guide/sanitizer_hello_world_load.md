# 使用 Hello World + Load Tester 进行 Sanitizer 测试

本文介绍如何使用 `hello_world_server` 配合 `load_tester` 进行 AddressSanitizer (ASan)、ThreadSanitizer (TSan) 和 UndefinedBehaviorSanitizer (UBSan) 的并发与内存安全验证。

## 概述

`hello_world_server` 是项目最小可用的 HTTP/3 服务，监听 `0.0.0.0:7001`，对 `GET /hello` 返回 `hello world`。配合 `load_tester` 可以在多线程、高并发下持续打满请求处理路径——握手、流创建、请求解析、响应发送、流关闭、连接回收——非常适合用 sanitizer 对 QUIC/HTTP3 栈做"高强度回归"。

| Sanitizer | 缩写 | 检测目标 |
|---|---|---|
| AddressSanitizer | ASan | 堆溢出、use-after-free、内存泄漏、栈溢出 |
| ThreadSanitizer | TSan | 数据竞争、死锁、线程安全问题 |
| UndefinedBehaviorSanitizer | UBSan | 未定义行为（整数溢出、空指针解引用、对齐违规等） |

参考的负载命令：

```bash
./load_tester https://localhost:7001/hello --clients 4 --requests 5000
```

> 总请求量 = `clients × requests = 4 × 5000 = 20000`，足够触发并发与生命周期相关的 bug，又不会让 TSan 跑得太久。

---

## 方式一：一键自动化脚本（推荐）

项目提供 `scripts/run_hello_world_load_sanitizer.sh`，自动完成构建、压测、采报告全流程。

### 基本用法

```bash
cd /data/workspace/quicX

# 单个 sanitizer
./scripts/run_hello_world_load_sanitizer.sh asan
./scripts/run_hello_world_load_sanitizer.sh tsan
./scripts/run_hello_world_load_sanitizer.sh ubsan

# 依次跑全部
./scripts/run_hello_world_load_sanitizer.sh all
```

### 可调参数（环境变量）

```bash
# 修改并发与请求量（默认 4 / 5000，与文档示例一致）
LOAD_CLIENTS=8 LOAD_REQUESTS=10000 \
    ./scripts/run_hello_world_load_sanitizer.sh tsan

# 修改服务端口
SERVER_PORT=7011 ./scripts/run_hello_world_load_sanitizer.sh asan

# 修改每请求超时（毫秒，默认 10000）
LOAD_TIMEOUT_MS=20000 ./scripts/run_hello_world_load_sanitizer.sh ubsan

# 修改整轮负载的硬超时（秒，默认 600）
LOAD_RUN_TIMEOUT=1200 ./scripts/run_hello_world_load_sanitizer.sh tsan
```

### 脚本工作流程

```
┌───────────────────────────────────────────────────────────┐
│  1. cmake 配置 (-DSANITIZER=xxx)                          │
│  2. 构建 hello_world_server / load_tester                 │
│  3. 启动 hello_world_server (监听 0.0.0.0:7001)           │
│  4. 运行：                                                │
│     load_tester https://localhost:7001/hello              │
│         --clients 4 --requests 5000                       │
│  5. 优雅停止 server (SIGTERM, 等待 sanitizer 落盘)        │
│  6. 收集 server / client / *_sanitizer.* 三类日志          │
│  7. 用精确正则筛 sanitizer 关键字，生成汇总报告            │
└───────────────────────────────────────────────────────────┘
```

### 输出结果

报告保存在 `sanitizer-results-helloworld/`：

```
sanitizer-results-helloworld/
├── asan_build_config.log       # ASan cmake 配置日志
├── asan_build.log              # ASan 编译日志
├── asan_server.log             # server 运行日志（stdout+stderr）
├── asan_load_tester.log        # load_tester 运行日志
├── asan_sanitizer.<pid>...     # ASan 写入的原始报告（如有）
├── asan_report.log             # ★ ASan 最终汇总报告
├── tsan_report.log             # ★ TSan 最终汇总报告
└── ubsan_report.log            # ★ UBSan 最终汇总报告
```

若某个 sanitizer **未检测到问题**，对应 `*_report.log` 内容为 `No issues detected by xxx.`。

---

## 方式二：手动构建与测试

需要更细粒度控制（自定义并发数、复现某条竞争、调试特定连接）时按下面流程操作。

### 第一步：构建

```bash
cd /data/workspace/quicX

# ASan
cmake -S . -B build-asan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSANITIZER=asan \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_TESTING=OFF \
    -G "Unix Makefiles"
cmake --build build-asan --target hello_world_server load_tester -j$(nproc)

# TSan
cmake -S . -B build-tsan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSANITIZER=tsan \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_TESTING=OFF \
    -G "Unix Makefiles"
cmake --build build-tsan --target hello_world_server load_tester -j$(nproc)

# UBSan
cmake -S . -B build-ubsan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSANITIZER=ubsan \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_TESTING=OFF \
    -G "Unix Makefiles"
cmake --build build-ubsan --target hello_world_server load_tester -j$(nproc)
```

### 第二步：运行测试

#### ASan

```bash
mkdir -p ./sanitizer-results-helloworld
export ASAN_OPTIONS="detect_leaks=1:halt_on_error=0:log_path=./sanitizer-results-helloworld/asan_san:print_stats=1"
export LSAN_OPTIONS="suppressions=/dev/null"

# 如使用 GCC 构建，部分发行版需要预加载
# export LD_PRELOAD=$(gcc -print-file-name=libasan.so)

./build-asan/bin/hello_world_server &
SERVER_PID=$!
sleep 3

./build-asan/bin/load_tester https://localhost:7001/hello --clients 4 --requests 5000

kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null
unset ASAN_OPTIONS LSAN_OPTIONS LD_PRELOAD
```

#### TSan

```bash
export TSAN_OPTIONS="halt_on_error=0:second_deadlock_stack=1:log_path=./sanitizer-results-helloworld/tsan_san:history_size=7"

# export LD_PRELOAD=$(gcc -print-file-name=libtsan.so)

./build-tsan/bin/hello_world_server &
SERVER_PID=$!
sleep 3

./build-tsan/bin/load_tester https://localhost:7001/hello --clients 4 --requests 5000

kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null
unset TSAN_OPTIONS LD_PRELOAD
```

#### UBSan

```bash
export UBSAN_OPTIONS="halt_on_error=0:print_stacktrace=1:log_path=./sanitizer-results-helloworld/ubsan_san"

./build-ubsan/bin/hello_world_server &
SERVER_PID=$!
sleep 3

./build-ubsan/bin/load_tester https://localhost:7001/hello --clients 4 --requests 5000

kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null
unset UBSAN_OPTIONS
```

### 第三步：分析报告

```bash
ls -la sanitizer-results-helloworld/

cat sanitizer-results-helloworld/asan_san.*   2>/dev/null
cat sanitizer-results-helloworld/tsan_san.*   2>/dev/null
cat sanitizer-results-helloworld/ubsan_san.*  2>/dev/null
```

无 `*_san.*` 文件 = 该 sanitizer 未发现问题。

---

## 关于 `--clients` 上限

`load_tester` 内部对 `--clients` 做了核数上限保护（每个 client 都会启动独立的 master EventLoop 线程），超过 `hardware_concurrency()` 时会自动按比例缩放并提示。如果就是想压测调度行为，加 `--force`：

```bash
./load_tester https://localhost:7001/hello --clients 32 --requests 1000 --force
```

> 在 sanitizer 模式下不建议过度超订：TSan 自身已经把每条访存放慢 5–15 倍，CPU 抢占会把握手 RTT 拉到秒级，反而容易因为 `connection_timeout_ms_` 触发误报。**保持示例命令的 4 × 5000 是个稳妥起点。**

---

## 常见问题排查

### Q: server 启动失败或端口被占用

```bash
ss -tlnp | grep 7001
# 或者换个端口
SERVER_PORT=7011 ./scripts/run_hello_world_load_sanitizer.sh asan
```

注意：换端口仅影响脚本启动的 server 监听口；`hello_world_server` 内部端口固定为 7001。如要真正切换，需要改 `example/hello_world/server.cpp` 里的 `server->Start("0.0.0.0", 7001)`。

### Q: TSan 报数据竞争，但栈在第三方库里

- 先确认 `LD_PRELOAD` 是否正确指向 `libtsan.so`（GCC 构建时常见）
- 第三方库（BoringSSL）若未带 sanitizer 重新编译，部分栈帧会缺失符号，但 race 报告本身仍然有效

### Q: ASan 报内存泄漏，但都来自 server 退出路径

QUIC server `Stop()` -> `Destroy()` 涉及大量异步资源回收，必须给 server **足够时间** flush。脚本里 `sleep 3` 后再 `wait`，手动跑请保留同样的等待。

### Q: UBSan 报有符号整数溢出

QUIC 协议的 RTT/拥塞窗口/包号计算非常密集，建议：

- 用 `uint64_t` 替代 `int64_t`
- 时间差用 `std::chrono::duration` 而不是裸整数

### Q: load_tester 大量 timeout

- 检查 `--timeout` 是否过小（sanitizer 下默认 10s 通常够）
- 检查 `/tmp/h3_server_logs`、`/tmp/h3_client_logs` 看是否有握手失败、流控阻塞
- 减小 `--clients` 或 `--requests` 重新跑一次定位

### Q: ASan 与 TSan 能否同时启用？

**不能。** 两者内存布局不兼容。UBSan 可与 ASan 同时使用，但本项目当前各自单独运行，便于定位。

---

## 与其它 sanitizer 测试的关系

| 场景 | 推荐脚本 |
|---|---|
| 单元测试覆盖 | `./scripts/ci-local.sh sanitize {asan,tsan,ubsan}` |
| 文件传输 / 大流场景 | `./scripts/run_file_transfer_sanitizer.sh {asan,tsan,ubsan,all}` |
| **HTTP/3 高并发短请求** | `./scripts/run_hello_world_load_sanitizer.sh {asan,tsan,ubsan,all}` |
| 发版前全量回归 | 三条脚本各跑一次 |

`hello_world + load_tester` 与 `file_transfer` 互补：前者侧重**短请求 × 高并发**（连接/流生命周期），后者侧重**长流 × 大数据**（流控/重组/拷贝路径）。建议两者都纳入回归。

---

## 总结

```bash
# 最常用命令
./scripts/run_hello_world_load_sanitizer.sh all

# 快速验证当前改动是否引入并发 bug
./scripts/run_hello_world_load_sanitizer.sh tsan

# 自定义压力
LOAD_CLIENTS=8 LOAD_REQUESTS=10000 \
    ./scripts/run_hello_world_load_sanitizer.sh tsan
```

保持 ASan / TSan / UBSan clean 是项目质量底线——hello_world + load_tester 链路上任何一处报告都意味着 QUIC 栈在最常见的"短请求 × 多客户端"场景里存在隐患，必须修。
