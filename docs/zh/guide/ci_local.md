# CI 本地调试指南

本项目的 `.github/workflows/*.yml` 与 `scripts/ci-local.sh` 保持**同构设计**：CI 上跑什么，本地就跑什么。你可以完全在本地闭环，只有推镜像到 GHCR / 发 Release 时才需要 GitHub。

## 快速开始

```bash
cd /data/workspace/quicX

# 一键跑所有本地可行的 CI 检查
./scripts/ci-local.sh all

# 或者分步调试：
./scripts/ci-local.sh build gcc Debug        # 对应 ci.yml 的 build-and-test job
./scripts/ci-local.sh build clang Release
./scripts/ci-local.sh sanitize asan          # 对应 sanitizer.yml (asan)
./scripts/ci-local.sh sanitize ubsan
./scripts/ci-local.sh sanitize tsan
./scripts/ci-local.sh coverage               # 对应 coverage.yml
./scripts/ci-local.sh interop local          # 对应 interop.yml local-selftest
./scripts/ci-local.sh interop matrix         # 对应 interop.yml cross-matrix
./scripts/ci-local.sh lint                   # 对应 lint.yml
./scripts/ci-local.sh fuzz 60                # 对应 fuzz-smoke.yml
./scripts/ci-local.sh clean                  # 清理所有 build-* 目录
```

## Workflow 与本地命令对照

| Workflow 文件 | 触发时机 | 本地等价命令 | 必须 GitHub？ |
|---|---|---|---|
| `ci.yml` | push / PR | `ci-local.sh build gcc Debug` 等 | ❌ |
| `sanitizer.yml` | push / PR / 每日 | `ci-local.sh sanitize {asan,ubsan,tsan}` | ❌ |
| `coverage.yml` | push / PR | `ci-local.sh coverage` | ❌ |
| `interop.yml` 的 local-selftest | push / PR | `ci-local.sh interop local` | ❌ |
| `interop.yml` 的 cross-matrix | 每日 / 手动 | `ci-local.sh interop matrix` | ❌ |
| `lint.yml` | push / PR | `ci-local.sh lint` | ❌ |
| `fuzz-smoke.yml` | PR / 每夜 | `ci-local.sh fuzz 60` | ❌ |

## 三种本地调试姿势

### 姿势 1：直接用 `ci-local.sh`（推荐）

这是最接近开发循环的方式 — 直接在宿主机上 build + test，快、无需 Docker。

```bash
# 改代码 → 只跑受影响的 job
vim src/quic/stream/send_stream.cpp
./scripts/ci-local.sh build gcc Debug
```

注意：`run_tests.py` 硬编码了 `build/` 路径，脚本会在每次构建后**把 `build` 软链到对应的 `build-gcc-debug` / `build-san-asan`** 等目录，避免多配置互相覆盖。

### 姿势 2：用 `act` 在本地 Docker 里真跑 workflow

[act](https://github.com/nektos/act) 能在本地 Docker 里模拟 `ubuntu-latest` runner 执行 workflow，几乎和真实 CI 等价。

```bash
# 1. 装 act（一次性）
curl -s https://raw.githubusercontent.com/nektos/act/master/install.sh | sudo bash -s -- -b /usr/local/bin

# 2. 用封装脚本
./scripts/ci-act.sh list                 # 列出所有 workflow 和 job
./scripts/ci-act.sh validate             # 只校验 yaml 语法（不执行）
./scripts/ci-act.sh ci                   # 跑 ci.yml
./scripts/ci-act.sh run build-and-test   # 跑指定 job
```

首次运行会拉取 `catthehacker/ubuntu:full-22.04`（约 1-2 GB），耐心等待。之后会缓存。

**使用 act 的注意事项：**
- `schedule` 触发器、`secrets`、GHCR push 等无法真正生效，但执行路径可观察。
- 不支持 `windows-latest` / `macos-latest`，这部分只能靠真 CI。

### 姿势 3：纯命令行（给 AI Agent / 脚本调用）

完全不依赖脚本：

```bash
# build + test (对应 CI 的 build-and-test job)
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
- 只用 clang
- asan: 检查堆溢出 / use-after-free / leak
- ubsan: 检查 undefined behavior（整数溢出、空指针解引用等）
- tsan: 检查数据竞争
- **TSAN 特别重要**：quicX 有大量多线程 worker，本地至少跑一次 `sanitize tsan`

### coverage (coverage.yml)
- gcc + `--coverage` + lcov
- 排除 `third/`、`build*/`、`test/`
- 生成 HTML 报告：`coverage-html/index.html`
- Phase A 目标覆盖率 ≥ 75%（非阻塞）

### interop (interop.yml)
- **local**：跑 `run_tests.py interop`，即 14 个场景自测
- **matrix**：跑 `interop_runner.py --matrix`，与 quiche/ngtcp2/quic-go/aioquic 跨实现
  - 需要 Docker
  - 镜像首次拉取约 2-5 GB
  - 超时设置为 60s/场景

### lint (lint.yml)
- PR 时只检查**改动文件**（git diff）
- 本地 `ci-local.sh lint` 检查 `src/` 和 `test/` 全量
- 当前设为非阻塞（warning），让 v0.1.0 前先统一代码风格

### fuzz-smoke (fuzz-smoke.yml)
- libFuzzer 每个目标跑 60s
- 发现 crash 会上传到 artifacts
- 语料保留在 `fuzz-corpus/`（本地运行时）

## 常见问题

### Q: 本地 build 通过，CI 为啥挂？
1. **submodules**：CI 用 `submodules: recursive`，本地先 `git submodule update --init --recursive`
2. **依赖**：CI 用 ubuntu-22.04，本地如果是旧系统可能缺 `ninja-build` / `lcov` / `clang-14`
3. **并发**：本地 `run_tests.py` 的 integration 是并发跑的，多连接绑同端口可能冲突 — 本地可改环境变量或串行跑

### Q: act 太慢/太耗磁盘怎么办？
- 用 `act -n` 先 dry-run 验证 yaml 语法
- 用 `ci-local.sh` 代替 — 直接在宿主机 build 快得多
- 仅在首次 / 合并前用 act 最终验证

### Q: 如何只跑某个单测？
```bash
./scripts/ci-local.sh build gcc Debug
./build/bin/quicx_utest --gtest_filter='*YourTest*' --gtest_color=yes
```

### Q: coverage 报告里某些文件 100% 未覆盖？
通常是 CMake `collect_sources` 收集了未被任何测试引用的源文件。运行 `genhtml --ignore-errors source,unmapped` 已经忽略这类错误；要提升覆盖就写单测。

### Q: 哪些必须 push 才能验证？
只有两项：
1. 推 `ghcr.io/quicx/quicx-interop` 镜像（需要 `GITHUB_TOKEN`）
2. 向 [quic-interop/quic-interop-runner](https://github.com/quic-interop/quic-interop-runner) 提 PR 上榜

其余 Phase A 的所有工作都可本地闭环。

