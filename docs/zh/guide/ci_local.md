# CI 本地调试指南

本仓库不依赖额外的本地包装脚本：`.github/workflows/*.yml` 中执行的所有检查，都可以直接用仓库根目录的 `cmake` + `run_tests.py` 在本地复现。你可以完全在本地闭环，只有推镜像到 GHCR / 发 Release 时才需要 GitHub。

`run_tests.py` 支持的模式：`all` / `utest` / `example` / `integration` / `fuzz` / `benchmark` / `perf` / `cc`。

> 注意：`run_tests.py` 硬编码了 `build/` 路径。如果像 CI 一样使用多个构建目录（`build-asan`、`build-cov`、`build-fuzz` 等），需要像 workflow 里那样把 `build` 软链到对应目录：`rm -rf build && ln -s build-asan build`。

## 快速开始

```bash
cd /data/workspace/quicX

# 构建并运行单测 + 集成测试（对应 ci.yml 的 build-and-test job）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTING=ON
cmake --build build --parallel 4
python3 run_tests.py utest
python3 run_tests.py integration

# 其他常用模式
python3 run_tests.py example       # 示例
python3 run_tests.py benchmark     # 基准测试
python3 run_tests.py perf          # test/perf 性能测试
python3 run_tests.py cc            # 拥塞控制模拟器测试
```

## Workflow 与本地等价命令对照

| Workflow 文件 | 触发时机 | 本地等价命令 | 必须 GitHub？ |
|---|---|---|---|
| `ci.yml` | push / PR | `cmake -S . -B build ... && cmake --build build` + `run_tests.py utest` / `integration` | ❌ |
| `sanitizer.yml` | push / PR / 每日 | clang + `-fsanitize={address,undefined,thread}` 构建 `build-{asan,ubsan,tsan}` 后跑 `run_tests.py utest` | ❌ |
| `coverage.yml` | push / PR | gcc + `--coverage` + lcov（见下文） | ❌ |
| `lint.yml` | push / PR | `clang-format --dry-run --Werror <files>` / `clang-tidy -p build-tidy <files>` | ❌ |
| `fuzz-smoke.yml` | PR / 每夜 | `-DENABLE_FUZZING=ON` 构建 `build-fuzz` 后短跑每个目标 | ❌ |

## 三种本地调试姿势

### 姿势 1：直接用 cmake + run_tests.py（推荐）

这是最接近开发循环的方式 — 直接在宿主机上 build + test，快、无需 Docker。

```bash
# 改代码 → 重建 → 只跑受影响的测试
vim src/quic/stream/send_stream.cpp
cmake --build build --parallel 4
python3 run_tests.py utest
```

CI 的 `build-and-test` job 矩阵为 `{gcc, clang} × {Debug, Release}` 共 4 个配置；本地建议至少覆盖 `gcc Debug` 和 `clang Release` 两档。CI 配置参数为 `-DENABLE_TESTING=ON -DBUILD_EXAMPLES=ON -DENABLE_INTEROP=OFF -DENABLE_FUZZING=OFF`。

### 姿势 2：用 `act` 在本地 Docker 里真跑 workflow

[act](https://github.com/nektos/act) 能在本地 Docker 里模拟 `ubuntu-latest` runner 执行 workflow，几乎和真实 CI 等价。

```bash
# 1. 装 act（一次性）
curl -s https://raw.githubusercontent.com/nektos/act/master/install.sh | sudo bash -s -- -b /usr/local/bin

# 2. 常用命令
act -l                                   # 列出所有 workflow 和 job
act -n                                   # dry-run，只校验语法（不执行）
act -W .github/workflows/ci.yml          # 跑 ci.yml
act -j build-and-test                    # 跑指定 job
```

首次运行会拉取 `catthehacker/ubuntu:full-22.04`（约 1-2 GB），耐心等待。之后会缓存。

**使用 act 的注意事项：**
- `schedule` 触发器、`secrets`、GHCR push 等无法真正生效，但执行路径可观察。
- 不支持 `windows-latest` / `macos-latest`，这部分只能靠真 CI。

### 姿势 3：纯命令行（给 AI Agent / 脚本调用）

完全不依赖脚本：

```bash
# build + test（对应 CI 的 build-and-test job）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTING=ON
cmake --build build --parallel 4
python3 run_tests.py utest
python3 run_tests.py integration
```

## 各 Job 细节

### build (ci.yml)
- 编译所有静态库 + 单测 + 集成测试 + example
- 矩阵：{gcc, clang} × {Debug, Release} = 4 个配置
- 本地建议跑 `gcc Debug` 和 `clang Release` 两档覆盖

### sanitize (sanitizer.yml)
- 只用 clang，构建到 `build-asan` / `build-ubsan` / `build-tsan`
- asan: `-fsanitize=address -fno-omit-frame-pointer -O1 -g`，检查堆溢出 / use-after-free / leak
- ubsan: `-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -O1 -g`，检查 undefined behavior（整数溢出、空指针解引用等）
- tsan: `-fsanitize=thread -fno-omit-frame-pointer -O1 -g`，检查数据竞争
- 构建后把 `build` 软链到对应目录，再跑 `run_tests.py utest`
- **TSAN 特别重要**：quicX 有大量多线程 worker，本地至少跑一次 tsan

```bash
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_C_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -O1 -g" \
    -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -O1 -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
    -DENABLE_TESTING=ON
cmake --build build-tsan --parallel 4
rm -rf build && ln -s build-tsan build
TSAN_OPTIONS=halt_on_error=1 python3 run_tests.py utest
```

### coverage (coverage.yml)
- gcc + `--coverage -O0 -g -fprofile-update=atomic`，构建到 `build-cov`
- lcov 排除 `/usr/*`、`third/`、`build*/`、`test/`
- 生成 HTML 报告：`coverage-html/index.html`
- 阈值为 60%（当前低于阈值只发 warning，非阻塞）

### interop（已移出 CI）

> 自建的 interop 测试环境（`interop_runner.py` 及相关编排文件）已删除，
> CI 中的 `interop.yml` workflow 一并移除。互操作测试现直接使用官方
> [quic-interop-runner](https://github.com/quic-interop/quic-interop-runner)
> 执行（本地构建 `quicx-interop:latest` 镜像后由官方 runner 调度），
> 完整流程见 [`guide/interop_runbook.md`](./interop_runbook.md)。

### lint (lint.yml)
- PR 时只检查**改动文件**（git diff）
- clang-format：`clang-format --dry-run --Werror <file>`
- clang-tidy：先用 `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` 生成 `build-tidy/compile_commands.json`，再 `clang-tidy -p build-tidy <file>`
- 当前设为非阻塞（warning）

### fuzz-smoke (fuzz-smoke.yml)
- 配置 `-DENABLE_FUZZING=ON -DENABLE_TESTING=OFF -DBUILD_EXAMPLES=OFF` 构建 `build-fuzz`
- libFuzzer 每个目标跑 60s
- 发现 crash 会上传到 artifacts
- 语料保留在 `fuzz-corpus/`（本地运行时）

## 常见问题

### Q: 本地 build 通过，CI 为啥挂？
1. **submodules**：CI 用 `submodules: recursive`，本地先 `git submodule update --init --recursive`
2. **依赖**：CI 用 ubuntu-22.04，本地如果是旧系统可能缺 `ninja-build` / `lcov` / `clang-14` / `clang-format-14` / `clang-tidy-14`
3. **并发**：本地 `run_tests.py` 的 integration 是并发跑的，多连接绑同端口可能冲突 — 本地可改环境变量或串行跑

### Q: act 太慢/太耗磁盘怎么办？
- 用 `act -n` 先 dry-run 验证 yaml 语法
- 直接用 cmake + `run_tests.py` 在宿主机跑 — 快得多
- 仅在首次 / 合并前用 act 最终验证

### Q: 如何只跑某个单测？
```bash
cmake --build build --parallel 4
./build/bin/quicx_utest --gtest_filter='*YourTest*' --gtest_color=yes
```

### Q: coverage 报告里某些文件 100% 未覆盖？
通常是 CMake `collect_sources` 收集了未被任何测试引用的源文件。运行 `genhtml --ignore-errors source,unmapped` 已经忽略这类错误；要提升覆盖就写单测。

### Q: 哪些必须 push 才能验证？
只有两项：
1. 推 `ghcr.io/quicx/quicx-interop` 镜像（需要 `GITHUB_TOKEN`）
2. 向 [quic-interop/quic-interop-runner](https://github.com/quic-interop/quic-interop-runner) 提 PR 上榜

其余所有工作都可本地闭环。
