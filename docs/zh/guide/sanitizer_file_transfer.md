# 使用 File Transfer 进行 Sanitizer 测试

本文介绍如何使用 `file_transfer` 示例程序配合 AddressSanitizer (ASan)、ThreadSanitizer (TSan) 和 UndefinedBehaviorSanitizer (UBSan) 进行内存安全和并发正确性验证。

## 概述

`file_transfer` 是一个基于 HTTP/3 流式 API 的文件传输示例，涉及多线程 EventLoop、网络 I/O、内存池分配等关键路径，是检测 QUIC 协议栈潜在问题的理想集成测试载体。

| Sanitizer | 缩写 | 检测目标 |
|---|---|---|
| AddressSanitizer | ASan | 堆溢出、use-after-free、内存泄漏、栈溢出 |
| ThreadSanitizer | TSan | 数据竞争、死锁、线程安全问题 |
| UndefinedBehaviorSanitizer | UBSan | 未定义行为（整数溢出、空指针解引用、对齐违规等） |

---

## 方式一：一键自动化脚本（推荐）

项目提供了 `scripts/run_file_transfer_sanitizer.sh`，可自动完成构建、测试、收集报告的全流程。

### 基本用法

```bash
cd /data/workspace/quicX

# 运行单个 sanitizer
./scripts/run_file_transfer_sanitizer.sh asan
./scripts/run_file_transfer_sanitizer.sh tsan
./scripts/run_file_transfer_sanitizer.sh ubsan

# 依次运行所有 sanitizer
./scripts/run_file_transfer_sanitizer.sh all
```

### 脚本工作流程

```
┌─────────────────────────────────────────────────┐
│  1. cmake 配置 (-DSANITIZER=xxx)                │
│  2. 构建 file_transfer_server / client          │
│  3. 生成 10MB 随机测试文件                       │
│  4. 启动 server                                  │
│  5. 执行三项测试：                               │
│     • 上传小文本文件                             │
│     • 上传 10MB 二进制文件                       │
│     • 下载文件                                   │
│  6. 优雅停止 server (SIGTERM)                    │
│  7. 收集并分析 sanitizer 报告                    │
│  8. 输出汇总结果                                 │
└─────────────────────────────────────────────────┘
```

### 输出结果

报告保存在 `sanitizer-results/` 目录：

```
sanitizer-results/
├── asan_build_config.log   # ASan 构建配置日志
├── asan_build.log          # ASan 编译日志
├── asan_server.log         # server 运行日志
├── asan_client.log.*       # client 各测试步骤日志
├── asan_report.log         # ★ ASan 最终汇总报告
├── tsan_report.log         # ★ TSan 最终汇总报告
└── ubsan_report.log        # ★ UBSan 最终汇总报告
```

如果某个 sanitizer **未检测到问题**，报告内容为 `No issues detected by xxx.`。

---

## 方式二：手动构建与测试

当需要更灵活的控制（如自定义测试文件大小、调整超时、调试特定场景）时，可手动操作。

### 第一步：构建

```bash
cd /data/workspace/quicX

# ASan 构建
cmake -S . -B build-asan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSANITIZER=asan \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_TESTING=OFF \
    -G "Unix Makefiles"
cmake --build build-asan --target file_transfer_server file_transfer_client -j$(nproc)

# TSan 构建
cmake -S . -B build-tsan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSANITIZER=tsan \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_TESTING=OFF \
    -G "Unix Makefiles"
cmake --build build-tsan --target file_transfer_server file_transfer_client -j$(nproc)

# UBSan 构建
cmake -S . -B build-ubsan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSANITIZER=ubsan \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_TESTING=OFF \
    -G "Unix Makefiles"
cmake --build build-ubsan --target file_transfer_server file_transfer_client -j$(nproc)
```

### 第二步：准备测试数据

```bash
# 创建测试文件目录
mkdir -p /tmp/ft_test_data /tmp/ft_upload

# 生成测试文件
dd if=/dev/urandom of=/tmp/ft_test_data/large.bin bs=1M count=10 2>/dev/null
echo "hello sanitizer test $(date)" > /tmp/ft_test_data/small.txt
```

### 第三步：运行测试

#### ASan 测试

```bash
# 设置 ASan 环境变量
export ASAN_OPTIONS="detect_leaks=1:halt_on_error=0:log_path=./sanitizer-results/asan_san:print_stats=1"
export LSAN_OPTIONS="suppressions=/dev/null"

# 如果使用 GCC 构建，可能需要 LD_PRELOAD
# export LD_PRELOAD=$(gcc -print-file-name=libasan.so)

# 启动 server
./build-asan/bin/file_transfer_server /tmp/ft_upload &
SERVER_PID=$!
sleep 3

# 上传测试
./build-asan/bin/file_transfer_client upload "https://127.0.0.1:7006/upload/small.txt" /tmp/ft_test_data/small.txt
./build-asan/bin/file_transfer_client upload "https://127.0.0.1:7006/upload/large.bin" /tmp/ft_test_data/large.bin

# 下载测试
./build-asan/bin/file_transfer_client download "https://127.0.0.1:7006/large.bin" /tmp/ft_test_data/downloaded.bin

# 停止 server
kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null
unset ASAN_OPTIONS LSAN_OPTIONS LD_PRELOAD
```

#### TSan 测试

```bash
export TSAN_OPTIONS="halt_on_error=0:second_deadlock_stack=1:log_path=./sanitizer-results/tsan_san:history_size=7"

# 如果使用 GCC 构建，可能需要 LD_PRELOAD
# export LD_PRELOAD=$(gcc -print-file-name=libtsan.so)

./build-tsan/bin/file_transfer_server /tmp/ft_upload &
SERVER_PID=$!
sleep 3

./build-tsan/bin/file_transfer_client upload "https://127.0.0.1:7006/upload/small.txt" /tmp/ft_test_data/small.txt
./build-tsan/bin/file_transfer_client upload "https://127.0.0.1:7006/upload/large.bin" /tmp/ft_test_data/large.bin
./build-tsan/bin/file_transfer_client download "https://127.0.0.1:7006/large.bin" /tmp/ft_test_data/downloaded.bin

kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null
unset TSAN_OPTIONS LD_PRELOAD
```

#### UBSan 测试

```bash
export UBSAN_OPTIONS="halt_on_error=0:print_stacktrace=1:log_path=./sanitizer-results/ubsan_san"

./build-ubsan/bin/file_transfer_server /tmp/ft_upload &
SERVER_PID=$!
sleep 3

./build-ubsan/bin/file_transfer_client upload "https://127.0.0.1:7006/upload/small.txt" /tmp/ft_test_data/small.txt
./build-ubsan/bin/file_transfer_client upload "https://127.0.0.1:7006/upload/large.bin" /tmp/ft_test_data/large.bin
./build-ubsan/bin/file_transfer_client download "https://127.0.0.1:7006/large.bin" /tmp/ft_test_data/downloaded.bin

kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null
unset UBSAN_OPTIONS
```

### 第四步：分析报告

```bash
# 查看是否生成了 sanitizer 报告文件
ls -la sanitizer-results/

# 如果存在报告文件，说明检测到了问题
cat sanitizer-results/asan_san.*   2>/dev/null  # ASan 问题
cat sanitizer-results/tsan_san.*   2>/dev/null  # TSan 问题
cat sanitizer-results/ubsan_san.*  2>/dev/null  # UBSan 问题

# 无报告文件 = 该 sanitizer 未发现问题 ✓
```

---

## 方式三：配合 CI 使用

项目的 `scripts/ci-local.sh` 也支持 sanitizer 测试（仅运行单元测试，不含 file_transfer）：

```bash
# 运行 sanitizer + 单元测试
./scripts/ci-local.sh sanitize asan
./scripts/ci-local.sh sanitize tsan
./scripts/ci-local.sh sanitize ubsan

# 单元测试 + file_transfer 集成测试完整流程：
./scripts/ci-local.sh sanitize tsan && ./scripts/run_file_transfer_sanitizer.sh tsan
```

---

## Sanitizer 运行时选项参考

### ASan (ASAN_OPTIONS)

| 选项 | 推荐值 | 说明 |
|---|---|---|
| `detect_leaks` | `1` | 启用内存泄漏检测 |
| `halt_on_error` | `0` | 发现问题后继续运行（收集更多信息） |
| `log_path` | `路径前缀` | 将报告写入文件（后缀自动追加 PID） |
| `print_stats` | `1` | 退出时打印内存统计 |
| `check_initialization_order` | `1` | 检查全局变量初始化顺序 |

### TSan (TSAN_OPTIONS)

| 选项 | 推荐值 | 说明 |
|---|---|---|
| `halt_on_error` | `0` | 发现竞争后继续运行 |
| `second_deadlock_stack` | `1` | 死锁报告中输出第二个线程的调用栈 |
| `log_path` | `路径前缀` | 将报告写入文件 |
| `history_size` | `4`~`7` | 内存访问历史深度（越大越精确，也越慢越耗内存） |

### UBSan (UBSAN_OPTIONS)

| 选项 | 推荐值 | 说明 |
|---|---|---|
| `halt_on_error` | `0` | 发现 UB 后继续运行 |
| `print_stacktrace` | `1` | 输出完整调用栈 |
| `log_path` | `路径前缀` | 将报告写入文件 |

---

## 常见问题排查

### Q: ASan 报告 `LeakSanitizer: detected memory leaks`

这通常是真正的内存泄漏。检查报告中的调用栈，定位分配但未释放的对象。常见原因：
- `shared_ptr` 循环引用
- 忘记在析构中释放手动分配的资源
- 异常路径中的 early return 没有清理资源

### Q: TSan 报告 `data race`

典型场景：
- 跨线程访问未加锁的成员变量
- EventLoop 初始化未完成时被其他线程使用（已通过 `WaitUntilReady()` 修复）
- 信号处理器中调用非 async-signal-safe 函数（已修复）

修复方法：确保共享数据通过 `mutex`、`atomic` 或线程安全队列保护。

### Q: UBSan 报告 `runtime error: signed integer overflow`

QUIC 协议中涉及大量计算（RTT、拥塞窗口、时间戳），需要注意：
- 使用 `uint64_t` 替代 `int64_t` 避免有符号溢出
- 时间差计算使用 `std::chrono::duration` 而非裸整数

### Q: 构建时 BoringSSL 产生大量警告

这是预期行为。CMakeLists.txt 已对第三方目标关闭 `-Werror`，不影响 sanitizer 检测。

### Q: TSan 与 ASan 能否同时使用？

**不能。** ASan 和 TSan 的内存布局不兼容，必须分别构建和测试。UBSan 可以与 ASan 同时启用（项目当前未组合使用，建议分开测试以便定位问题）。

### Q: 测试时 server 启动失败？

1. 检查端口 7006 是否被占用：`ss -tlnp | grep 7006`
2. 确认 TLS 证书文件可访问（server 默认使用内置的测试证书）
3. 查看构建日志确认编译成功：`cat sanitizer-results/xxx_build.log`

### Q: 如何增大测试文件或并发连接？

手动模式下可随意调整：

```bash
# 生成 100MB 测试文件
dd if=/dev/urandom of=/tmp/ft_test_data/huge.bin bs=1M count=100

# 并发多个 client（更容易触发竞争）
for i in $(seq 1 5); do
    ./build-tsan/bin/file_transfer_client upload \
        "https://127.0.0.1:7006/upload/file_${i}.bin" \
        /tmp/ft_test_data/large.bin &
done
wait
```

---

## 总结

| 需求 | 推荐方法 |
|---|---|
| 日常开发快速验证 | `./scripts/run_file_transfer_sanitizer.sh tsan` |
| 发版前全面检查 | `./scripts/run_file_transfer_sanitizer.sh all` |
| 调试特定竞争场景 | 手动构建 + 调整环境变量 + 并发 client |
| CI 集成 | `ci-local.sh sanitize xxx` + `run_file_transfer_sanitizer.sh xxx` |

保持 **ASan / TSan / UBSan clean** 是本项目的质量底线——每次提交前至少跑一轮 `tsan`（多线程是最容易出 bug 的领域），发版前必须三者全绿。
