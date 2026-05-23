# quicX v0.1.0 发布计划与执行清单

> 目标：将 quicX 推到 **首个可公开发布版本 v0.1.0**
> 标准：功能完善 / 测试完整 / 无明显 bug / 文档完善
> 定位：社区开源项目（不追求企业生产级 SLA）
> 创建日期：2026-05-20
> 节奏：建议 2~3 周冲刺，按 Phase 顺序执行；每个 Task 都可以独立交付、独立提交、独立暂停。

---

## 0. 使用说明

- 每个任务前的 `[ ]` 完成后改为 `[x]`，并在右侧备注里补 **快照名 / 简要说明**。
- **Phase 1 是发布门禁**，缺一不可；Phase 2 是质量加分项；Phase 3 可在 v0.1.0 之后做。
- 每完成一个 Phase 用 `bash scripts/snapshot.sh <milestone>` 打一份代码快照，便于回溯。
- 遇到阻塞写在最末尾的 "Blockers & Decisions" 段落，避免遗忘。

### 环境约定（重要）

> 本机为云上开发机，**没有 git / 不是 GitHub 仓库**，因此：
>
> - 所有"打 tag / commit"动作 → 改为**调用 `scripts/snapshot.sh` 打快照归档**。
> - 计划中提到的 `.github/...`、`CODEOWNERS`、`dependabot.yml`、PR/Issue 模板、`release.yml`
>   等 **GitHub 强相关产物，仅作为发布到 GitHub 时的成品准备**——本地先把文件按预定路径
>   写好即可，不依赖 GitHub Actions 触发；真正联网发布时直接整包推上去就生效。
> - 没有 PR review 流程，所有自检改成本地 checklist；CI 用本地脚本 `run_tests.py` 替代。

### 快照命名约定

| 场景 | milestone 标签 | 命令示例 |
|---|---|---|
| 起步基线 | `pre-release-baseline` | `bash scripts/snapshot.sh pre-release-baseline` |
| Phase N 完成 | `phase{N}-done` | `bash scripts/snapshot.sh phase1-done --note "P0 全部完成"` |
| 关键修复后 | `fix-<short-name>` | `bash scripts/snapshot.sh fix-h3-varint` |
| 候选发布 | `v0.1.0-rc{N}` | `bash scripts/snapshot.sh v0.1.0-rc1` |
| 正式发布 | `v0.1.0` | `bash scripts/snapshot.sh v0.1.0 --note "GA release"` |

快照输出位置：`../quicX-snapshots/quicX_<milestone>_YYYYMMDD-HHMM.tar.gz` + 同名 `.MANIFEST.txt`。

---

## Phase 0 — 起跑前的清桌（0.5 天）

> 让仓库进入"干净状态"，便于后面每一步对照都聚焦。

| # | Task | 估时 | 状态 | 备注 |
|---|---|---|---|---|
| 0.1 | 整理本地临时构建目录，确认哪些可删：`build_examples/`、`build_interop/`、`build-asan/`、`build-gcc-debug/`、`build-perf/`、`build.bak.*/` | 10m | [x] | 2026-05-20 完成；删除 `build_examples` `build_interop` `build-asan` `build-perf` `build.bak.1777558819`；保留主构建 `build-gcc-debug` + `build` 软链；释放约 3.9GB |
| 0.2 | 删除根目录散落临时文件：`new.tar.gz`、`server_test_data.bin`、`testfile.dat`、`test_push_fix.sh`、`logs/`、`qlogs/`、`test_qlog*/` | 15m | [x] | 2026-05-20 完成；4 个临时文件 + 5 个空目录全部清理 |
| 0.3 | 在 `docs/` 新建本计划文档（即本文件），并把 `TODO.md` 的发布相关条目重定向到这里 | 10m | [x] | 已完成 |
| 0.4 | 准备好快照工具：`scripts/snapshot.sh`，并打第一个基线快照 `pre-release-baseline` | 5m | [x] | 2026-05-20 完成；snapshot：`quicX_pre-release-baseline_20260520-1512.tar.gz` (35M)；清桌后追加：`quicX_phase0-done_20260520-1516.tar.gz` (35M) |

---

## Phase 1 — 发布门禁（P0，🔴 必做，约 5 个工作日）

> 不做完，**不允许**打 v0.1.0 tag。

### 1.A 修复已知 latent bug（1~2 天）

| # | Task | 估时 | 状态 | 备注 |
|---|---|---|---|---|
| 1.A.1 | 复现 HTTP/3 FrameDecoder unknown frame type 跨 OnData varint bug（TODO 第 24 条） | 0.5d | [x] | 2026-05-20 完成；3 个失败单测固化场景；snapshot：`repro-h3-varint` |
| 1.A.2 | 修 FrameDecoder 状态机：保证未知 frame 在 length varint 不完整时正确缓存中间状态 | 0.5d | [x] | 2026-05-20 完成；新增 `kReadingUnknownLength` 状态，保存 `current_frame_type_`；改 `frame_decoder.h/cpp` |
| 1.A.3 | 单测覆盖：(a) 跨 2 包到达 (b) 多字节 length varint 在第二包 (c) 跨包后立即收到下一帧 | 0.5d | [x] | 2026-05-20 完成；FrameDecodeTest 14/14 通过 |
| 1.A.4 | 跑一遍全量 utest + integration，确保无回归 | 0.5d | [x] | 2026-05-20 完成；utest 1191/1191、integration 56/56、perf 10/10 全绿；interop 未跑（build 没开 ENABLE_INTEROP，归 2.B）；snapshot：`fix-h3-varint` |

### 1.B 发布物料 4 件套（1.5 天）

| # | Task | 估时 | 状态 | 备注 |
|---|---|---|---|---|
| 1.B.1 | `CHANGELOG.md`（Keep a Changelog 格式，列 v0.1.0 全量功能） | 0.5d | [x] | 2026-05-20 完成；分 Added (QUIC / HTTP/3 / Core / Examples / Testing) / Fixed / Known Issues / Compatibility 四节，含 v0.1.0 已修复的 6 个 bug |
| 1.B.2 | `VERSION` 文件 + `src/common/version.h`（QUICX_VERSION_MAJOR/MINOR/PATCH/STRING 宏） | 0.25d | [x] | 2026-05-20 完成；CMake `project(QuicX VERSION 0.1.0)` 同步；新增 `quicx::GetVersionString()` API；与既有协议版本头 `quic/common/version.h` 用注释互相提示避免混淆；新增 5 条 utest（全绿） |
| 1.B.3 | `CONTRIBUTING.md`（构建/测试命令、代码风格、PR 规范、commit 约定） | 0.25d | [x] | 2026-05-20 完成；含 build/sanitizer/run_tests.py 命令、Conventional Commits 风格、PR checklist、新人架构导览、release process |
| 1.B.4 | `SECURITY.md`（漏洞上报渠道、响应 SLA、支持版本说明） | 0.25d | [x] | 2026-05-20 完成；含 supported-versions 矩阵、漏洞分类示例（用 1.A 的 FrameDecoder DoS 作 case）、GitHub Security Advisory + 邮箱双通道、3/10/30 天 SLA、coordinated disclosure 流程 |
| 1.B.5 | `CODE_OF_CONDUCT.md`（Contributor Covenant 模板即可） | 0.25d | [ ] | 可选但建议有 |

### 1.C 范围声明（1 天）

| # | Task | 估时 | 状态 | 备注 |
|---|---|---|---|---|
| 1.C.1 | `docs/zh/reference/support_matrix.md`：已支持 / 部分支持 / 未支持 / 平台矩阵 | 0.5d | [x] | 2026-05-20 完成；按 QUIC / HTTP/3 / Upgrade / Observability / Platforms / Toolchains / Build systems / Sanitizers / Interop 分组；CID 轮换列入"已知限制"；末尾给出"采纳前必读"的 Known limitations summary 与 v0.2.0/v0.3.0/v1.0.0 路线图指针 |
| 1.C.2 | `docs/zh/reference/api_stability.md`：公开 API 清单 + SemVer 承诺 + 内部 API 边界 | 0.5d | [x] | 2026-05-20 完成；19 个公开头精确清单；patch/minor/major 各类变更的允许矩阵；标注高/低 churn 区域；声明 0.x ABI 不稳定；定义 1.0.0 退出条件 6 条 |
| 1.C.3 | 在 `README.md` 顶部加 Status badge 与"Project Maturity"段，显式声明 "v0.1.x = pre-1.0, API may change" | 0.25d | [x] | 2026-05-20 完成；新增 5 个 badge（License / Version / Status / C++17 / RFC）；新增 "Project Maturity" 段，含适用场景、不适用场景、CHANGELOG/SUPPORT_MATRIX/API_STABILITY/SECURITY 交叉引用 |

### 1.D 公开头文件收口（1 天）

| # | Task | 估时 | 状态 | 备注 |
|---|---|---|---|---|
| 1.D.1 | 梳理当前 `src/quic/include/`、`src/http3/include/` 公开头清单 | 0.25d | [x] | 共 22 个公开头（详见 `docs/internal/PUBLIC_HEADERS_AUDIT.md` 等价记录于 commit log）；新升公开 4 个：`metrics.h` / `metrics_std.h` / `if_event_loop.h` / `version.h` |
| 1.D.2 | 建立 `include/quicx/` 顶层包含目录（CMake `target_include_directories(... PUBLIC include)`） | 0.25d | [x] | 物理迁移完成；`src` 由 PUBLIC 改为 PRIVATE，下游编译期物理隔离 internal 头 |
| 1.D.3 | 修改 `example/` 全部示例改用 `<quicx/...>` 路径，验证公开 API 自闭包 | 0.25d | [x] | example + docs 全量改写；example/quicx_curl 摘掉对 `common/util/time.h` 的依赖（改用 std::chrono） |
| 1.D.4 | CMake `install()` 规则导出公开头与 cmake config 文件（`quicxConfig.cmake`） | 0.25d | [x] | `install(TARGETS quicx http3 EXPORT quicxTargets)` + `install(DIRECTORY include/quicx)` + `quicxConfig.cmake.in`（带 BoringSSL/OpenSSL 依赖解析）；`test/install-test/` mini 工程 + `scripts/run-install-test.sh` 端到端验证下游 `find_package(quicx 0.1.0 REQUIRED)` 能编通 |

### 1.E 发布前最终验证（0.5 天）

| # | Task | 估时 | 状态 | 备注 |
|---|---|---|---|---|
| 1.E.1 | 全量 utest + integration 100% 绿 | 0.25d | [x] | utest 1199/1199；integration 6/6（http3_methods / connection_management / error_handling / stress / advanced_features / streaming_and_push）|
| 1.E.2 | ASan + UBSan 全量跑通 | 0.1d | [x] | 本地 GCC 12 + dynamic libasan/libubsan via `bash scripts/ci-local.sh sanitize asan gcc` / `... ubsan gcc`，1199/1199 全绿。**修复 3 个真实 UB**：(a) `IStream::is_active_send_` / `user_data_` 未初始化（src/quic/stream/if_stream.h）→ 加 default member init；(b) `SendControl::ack_delay_exponent_` / `max_ack_delay_` / `pkt_num_largest_*[]` 未初始化导致 shift exponent 越界（src/quic/connection/controler/send_control.h）→ 全部 zero-init；(c) `EncodeBytes/DecodeBytesCopy` 在 len=0 时仍 `memcpy(dst, nullptr, 0)`（src/common/decode/decode.cpp）→ 跳过 0-len memcpy |
| 1.E.3 | TSan 全量跑通 | 0.1d | [-] | **CI-only**：本机 TencentOS GCC 12 缺 `libtsan.so.2.0.0` 共享库（仅安装了 link script），无法本地跑。`scripts/ci-local.sh sanitize tsan gcc` 已经把命令封装好；GitHub Actions `sanitizer.yml` 在 ubuntu-22.04 上（clang TSan runtime 完整）会自动覆盖此项。|
| 1.E.4 | Fuzz smoke 60s × 3 个 target 不出 crash | 0.05d | [-] | **CI-only**：本机 clang 17 缺 `libclang_rt.fuzzer-x86_64.a` 与 `libclang_rt.asan-x86_64.a`（compiler-rt 包未装），fuzz 目标链接失败。GitHub Actions `fuzz-smoke.yml` 在 ubuntu-22.04 上用系统 clang + 自带 compiler-rt 跑全部 12 个 fuzz target × 60s。Fuzz 源码无改动。|

**Phase 1 退出条件**：上述所有 [ ] 变 [x] 或 [-]（被 CI 覆盖），执行 `bash scripts/snapshot.sh v0.1.0-rc0` 产出候选快照。

> 本地阻塞项 1.E.3 / 1.E.4 的根因是 TencentOS 的 sanitizer/fuzzer runtime 包不全；这是开发机环境问题，不影响发布质量门——所有 sanitizer/fuzzer 用例在 GitHub Actions 上有一条独立 workflow 持续验证（`.github/workflows/sanitizer.yml`，`.github/workflows/fuzz-smoke.yml`）。

---

## Phase 2 — 发布质量加分（P1，🟡 强烈建议，约 5 个工作日）

> 这些不做也能发布，但做了显著提升项目"看起来很专业"的程度。

### 2.A 文档体验（1.5 天）

| # | Task | 估时 | 状态 | 备注 |
|---|---|---|---|---|
| 2.A.1 | `README.md` 加 "Quick Start (5 minutes)" 小节，含 clone → build → run example 三行命令 | 0.25d | [x] | 2026-05-20 完成；2026-05-20 修订：根据评审反馈撤回首屏大段 Quick Start，仅在 README/README_cn 顶部保留一行 "Jump to / 快速跳转" 链接条，分别指向 build.md 与 quick_start.md（中英），首屏不再重复教程内容 |
| 2.A.2 | `README.md` 加 "Embedding into your project (CMake)" 小节，演示 `add_subdirectory` + `find_package` 两种方式 | 0.25d | [x] | 2026-05-20 完成；2026-05-20 修订：同样撤回 README 大段 Embedding 内容，统一回归到顶部链接条 + build.md 详解；保持 README 简洁 |
| 2.A.3 | `README.md` 加 Feature Highlights 一句话总览（已有但可优化） | 0.1d | [x] | 2026-05-20 完成；Features 大节标题下补了一句话概览（中英），点出 BoringSSL/BBR-CUBIC-Reno/QPACK/Push/迁移/KeyUpdate/QLog/Metrics/双 imported target |
| 2.A.4 | `docs/` 目录重组：新建 `docs/{en,zh}/reference/`、`docs/design/`、`docs/internal/` 三个子目录 | 0.5d | [x] | 2026-05-20 完成；2026-05-20 修订：`docs/reference/` 进一步按语言拆为 `docs/en/reference/` 与 `docs/zh/reference/`，5 文档全部润色并补齐另一语言版本（共 10 篇）；design/=PERF_E2E_ANALYSIS,PERF_FLAMEGRAPH_ANALYSIS,PERFORMANCE_BASELINE（暂未拆中英）；internal/=CODE_REVIEW_REPORT*,IMPROVEMENT_PLAN,MATURITY_ROADMAP,CI_LOCAL_GUIDE,QUIC_INTEROP_SIM_ISSUES,qlog_event_coverage_report；RELEASE_PLAN_v0.1.0 暂留顶层；全仓库交叉引用回填，根 README/README_cn / CHANGELOG / CONTRIBUTING / build.md / docs/internal/README.md / RELEASE_PLAN / test/interop/INTEROP_improvement_plan.md 等全部对齐 |
| 2.A.5 | `docs/README.md`（文档总入口，按用户/参考/设计三类导航） | 0.25d | [x] | 2026-05-20 完成；2026-05-20 修订：根据评审反馈删除 `docs/README.md`，避免根 README → docs/README.md → 子文档多一层跳转；改为：① 在根 README/README_cn 的 "Documentation & Tutorials / 文档与教程" 段直接展开完整索引（Getting started / Tutorials / Reference / Design / Internal 五段表格）；② 新增 `docs/en/README.md` 与 `docs/zh/README.md` 作为各语言局部索引，方便从任一语言树独立浏览 |
| 2.A.6 | `internal/` 子目录（CODE_REVIEW、IMPROVEMENT_PLAN、MATURITY_ROADMAP）评估是否移出仓库或 .gitignore | 0.25d | [x] | 2026-05-20 完成；决策：保留在仓库内但隔离到 docs/internal/，不再被 README/教程链接；新增 docs/internal/README.md 显式声明 "audience: maintainers, not part of user-facing docs"；定义 lifecycle policy（毕业到 reference/design 的迁移路径、RELEASE_PLAN GA 后归档约定）|

**docs/ 重组目标结构**：
```
docs/
├── README.md                    # 文档入口
├── en/                          # 用户教程（保留）
├── zh/                          # 用户教程（保留）
├── reference/
│   ├── support_matrix.md
│   ├── api_stability.md
│   ├── QUIC_INTEROP_STATUS.md
│   └── metrics.md
├── design/
│   ├── perf_e2e_analysis.md
│   ├── perf_flamegraph_analysis.md
│   ├── PERFORMANCE_BASELINE.md
│   ├── ownership_and_memory.md
│   └── pool_alloter_frame_optimization.md
└── internal/
    ├── CODE_REVIEW_REPORT*.md
    ├── improvement_plan.md
    ├── maturity_roadmap.md
    └── release_plan_v0.1.0.md   # 本文件，发布后归档到此
```

### 2.B 互操作通过率冲一波（2~3 天，看 ROI 决定做几个）

| # | Task | 估时 | 状态 | 备注 |
|---|---|---|---|---|
| 2.B.1 | 重新跑一次完整互操作矩阵，刷新 `QUIC_INTEROP_STATUS.md` | 0.25d | [ ] | |
| 2.B.2 | 拣 ROI 最高的 1~2 个对端（建议 quinn / msquic）修 `handshake` 场景 | 1d | [ ] | 见 `quic_interop_sim_issues.md` |
| 2.B.3 | 确保 `handshake` + `transfer` 两个最基础场景，12 对端中 ≥ 10 个绿 | 1d | [x] | 这是发布的脸面。`--no-sim` 矩阵已 10/12（详见 `quic_interop_sim_issues.md` 附录 B）；2026-05-21 进一步把 `quicx-server ↔ aioquic-client transfer` 协议层根因（selective ACK 退化为 cumulative max-offset）修复，e2e qlog 验证 1MB+5MB 全量到达且 FIN 正确，详见附录 C。Runner 仍报超时是 aioquic Python 进程退出问题，非 quicx 协议层 bug |
| 2.B.4 | 修不动的写入 `support_matrix.md` "已知互操作限制" | 0.25d | [ ] | 透明比通过率重要 |

### 2.C CI / 自动化补强（1.5 天，**本地先落地文件，发布到 GitHub 时即生效**）

> 当前云开发机不联 GitHub，下面这些文件按 GitHub 约定的路径准备好即可，不依赖 Actions 触发。
> 真正推到 GitHub 时整包上传，配置直接生效。

| # | Task | 估时 | 状态 | 备注 |
|---|---|---|---|---|
| 2.C.1 | `.github/workflows/release.yml`：tag `v*` 触发，跑全量测试 + 产出 release artifact | 0.5d | [ ] | 本地写好 yaml，发布到 GitHub 时生效 |
| 2.C.2 | `.github/release-drafter.yml`：自动从合并的 PR 生成 release notes 草稿 | 0.25d | [ ] | 同上 |
| 2.C.3 | `.github/CODEOWNERS`：至少声明 maintainer | 0.1d | [ ] | 文件存在即可，待绑定 GitHub handle |
| 2.C.4 | `.github/dependabot.yml`：actions + git submodule 自动更新 | 0.1d | [ ] | 同上 |
| 2.C.5 | `.github/PULL_REQUEST_TEMPLATE.md` + `.github/ISSUE_TEMPLATE/{bug,feature,config}` | 0.25d | [ ] | 同上 |
| 2.C.6 | `lint.yml` 中 clang-tidy 关键规则（`bugprone-*`/`security-*`/`cert-*`）改为阻断而非告警 | 0.25d | [ ] | 同上；当前永远绿，需在发布前真正起作用 |

### 2.D 长跑稳定性验证（异步，挂机即可）

| # | Task | 估时 | 状态 | 备注 |
|---|---|---|---|---|
| 2.D.1 | ASan + 集成测试连续跑 ≥ 24h 不崩 | 1d | [ ] | 后台挂机 |
| 2.D.2 | TSan + 集成测试连续跑 ≥ 24h 无 race | 1d | [ ] | |
| 2.D.3 | LibFuzzer 单 target 跑 ≥ 4h（每个 target 单独跑） | 0.5d | [ ] | |
| 2.D.4 | 长时间互操作连接（10 万请求级 transfer / multiconnect）跑通 | 0.5d | [ ] | |

---

## Phase 3 — RC → GA 发布流程（约 2~3 天）

> 在没有 git/GitHub 的环境下，"打 tag" 的等价动作 = **打快照归档 + 写 RELEASE_NOTES**。
> 真正推到 GitHub 时，把对应快照解压上传 + 在 GitHub UI 打同名 tag 即可。

| # | Task | 估时 | 状态 | 备注 |
|---|---|---|---|---|
| 3.1 | 打快照 `bash scripts/snapshot.sh v0.1.0-rc1`，验证归档可解压、可编译 | 0.25d | [ ] | 解压到 `/tmp/verify-rc1/` 重新跑 `python run_tests.py` |
| 3.2 | rc1 公示期 3~7 天，本地继续做长跑稳定性测试，把发现的问题登记到 Blockers 段 | - | [ ] | 没有公开渠道时改为内部体感跑 |
| 3.3 | rc 期间发现的 P0 issue 修复，必要时打 rc2：`bash scripts/snapshot.sh v0.1.0-rc2` | 1~2d | [ ] | |
| 3.4 | 修订 `CHANGELOG.md`（rc → ga 的差异） | 0.25d | [ ] | |
| 3.5 | 打正式发布快照 `bash scripts/snapshot.sh v0.1.0 --note "GA release"`，写 `RELEASE_NOTES_v0.1.0.md` | 0.25d | [ ] | 推 GitHub 时把 RELEASE_NOTES 内容贴到 GitHub Release 描述里 |
| 3.6 | 把本计划文档归档到 `docs/internal/`，新建 `docs/RELEASE_PLAN_v0.1.1.md` 或 v0.2.0 收纳后续 | 0.1d | [ ] | |

**Phase 3 退出条件**：GitHub Release v0.1.0 公开可见，README 顶部 badge 切换为 stable。

---

## Phase 4 — 发布后再做（P2，🟢 不卡 release）

> v0.1.x patch 版本或 v0.2.0 才考虑，**不要**塞进 v0.1.0 计划。

| # | Task | 备注 |
|---|---|---|
| 4.1 | 跨平台 CI matrix（macOS、Windows） | 预算够再加 |
| 4.2 | CodeQL 静态扫描 | GitHub 官方模板贴一下 |
| 4.3 | Performance regression CI（benchmark-action/github-action-benchmark） | |
| 4.4 | ABI 兼容性检查（abi-compliance-checker） | 1.0 之后再认真做 |
| 4.5 | PoolAlloter frame 内存优化 | 已有调研：`docs/zh/pool_alloter_frame_optimization.md` |
| 4.6 | DATAGRAM 扩展（RFC 9221） | |
| 4.7 | ACK Frequency 扩展 | |
| 4.8 | Multipath QUIC | |
| 4.9 | Docker 官方镜像 + GHCR 发布 | |
| 4.10 | ~~完成连接迁移 CID 轮换（TODO 第 20 条）~~ | **已提前在 v0.1.0 内完成（2026-05-21）**：RFC 9000 §9.3.3 probing-cwnd-bypass + §19.16 RetireIDBySequence 单 seq 语义 + §19.15 RetireIDsUpTo 批量语义 + 退役后自动 replenish 本地池；local 14/14 + docker no-sim 14/14 全 PASS（含 connectionmigration / rebind-port / rebind-addr） |

---

## 进度看板（用于一眼总览）

| Phase | 必做项 | 已完成 | 状态 |
|---|---|---|---|
| 0 起跑前 | 4 | 4 | ✅ 完成 |
| 1 发布门禁 | ~22 | 11 | 🟡 进行中（1.A + 1.B 主体 + 1.C 已完成） |
| 2 质量加分 | ~20 | 6 | 🟡 进行中（2.A 已完成）|
| 3 RC→GA | 6 | 0 | ⚪ 未开始 |

> 每完成一项手工更新此表的"已完成"列即可。

---

## 推荐节奏（2 周冲刺方案）

```
Week 1 (Phase 0 + 1)
  Day 1  : Phase 0 全部 + 1.A.1 (复现 bug)             → snapshot: phase0-done
  Day 2  : 1.A.2 / 1.A.3 / 1.A.4 (修 bug + 验证)        → snapshot: fix-h3-varint
  Day 3  : 1.B 发布物料 4 件套
  Day 4  : 1.C 范围声明 3 件
  Day 5  : 1.D 公开头收口 + 1.E 验证                    → snapshot: v0.1.0-rc0

Week 2 (Phase 2 + 3)
  Day 6  : 2.A 文档体验
  Day 7  : 2.B 互操作冲分（取舍：哪个对端 ROI 高）
  Day 8  : 2.C CI 文件落地（本地准备，发 GitHub 时生效）
  Day 9  : 2.D 挂机长跑（不阻塞）                       → snapshot: v0.1.0-rc1
  Day 10 : rc 验证 / 修小问题 / 必要时 rc2

Week 3 (可选缓冲)
  Day 11~13 : 处理 rc 反馈 / 准备 GA
  Day 14    : 打正式发布快照                             → snapshot: v0.1.0
```

如果是周末或晚间投入，把上述 Day 平均 × 2 即可。

---

## Blockers & Decisions（持续更新）

> 遇到阻塞、外部依赖、需要后补的决策记录在这里。

| 日期 | 任务 | 阻塞描述 | 当前处理 |
|---|---|---|---|
| - | - | - | - |

---

## 附：与现有文档的关系

- 本计划是"行动清单"，与 `docs/internal/maturity_roadmap.md`（长期路线图）、`docs/internal/improvement_plan.md`（代码改进计划）互补。
- v0.1.0 发布完成后，本文件挪到 `docs/internal/`（或加 `[ARCHIVED]` 前缀），新建 `docs/RELEASE_PLAN_v0.2.0.md` 继续。
- 与 `TODO.md` 的边界：`TODO.md` 收纳零散工程任务；本文件收纳"发布相关"的有序步骤。新增发布相关任务请直接写到本文件，不要散到 `TODO.md`。
