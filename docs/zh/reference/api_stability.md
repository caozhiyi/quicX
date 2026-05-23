# QuicX API 稳定性策略

> 适用于 **v0.1.x**。本文档定义 QuicX 把哪些符号视为"公有 API"、`0.x` 期间的稳定性承诺，以及到 `1.0.0` 时承诺会如何收紧。
>
> 配套阅读：[`support_matrix.md`](./support_matrix.md)（功能支持矩阵）、
> [`../../../CHANGELOG.md`](../../../CHANGELOG.md)（变更日志）。

---

## 一句话说

| 阶段 | 头文件布局 | API 破坏策略 |
|---|---|---|
| **`0.1.x` 补丁版** | 当前路径，根在 `src/<layer>/include/` | **不破坏**。仅修 bug。 |
| **`0.x` minor 版**（`0.2.0` / `0.3.0`…） | 1.D 阶段后会迁到顶层 `include/quicx/` | **可能破坏**，且会在 `CHANGELOG` 写明；条件允许时给一个 minor 的 deprecation 窗口 |
| **`1.0.0`** | 冻结 | 适用 SemVer；`1.x` 期间源码兼容 |

如果你今天就要把 QuicX 嵌入项目，**请 pin 到具体 `0.1.z` 标签**，升级前先读 `CHANGELOG.md`。

---

## 什么算"公有 API"

一个符号是公有 API **当且仅当** 它声明在以下目录之一：

| 层 | 当前公有 include 目录 | 1.D 之后的目标位置 |
|---|---|---|
| 公共组件（buffer、type 等） | `src/common/include/` | `include/quicx/common/` |
| QUIC 传输层 | `src/quic/include/` | `include/quicx/quic/` |
| HTTP/3 | `src/http3/include/` | `include/quicx/http3/` |
| HTTP 升级 | `src/upgrade/include/` | `include/quicx/upgrade/` |

`src/**` 下的其他东西 —— 包括任何 `*_impl.h`、只在同一模块的 `.cpp` 中 include 的头、内部 slab 分配器、时间轮、帧编解码器、QPACK 编/解码器内部细节等 —— 都是**内部实现**。**请不要依赖**。如果你确实需要依赖，请提 issue 描述用例。

### `0.1.x` 公有头清单（权威）

```
src/common/include/
  if_buffer_read.h
  if_buffer_write.h
  type.h

src/quic/include/
  if_quic_bidirection_stream.h
  if_quic_client.h
  if_quic_connection.h
  if_quic_recv_stream.h
  if_quic_send_stream.h
  if_quic_server.h
  if_quic_stream.h
  type.h

src/http3/include/
  if_async_handler.h
  if_client.h
  if_request.h
  if_response.h
  if_server.h
  type.h

src/upgrade/include/
  if_upgrade.h
  type.h
```

共 19 个头。`<common/version.h>`（产品版本号头）也是公有；QUIC 协议版本号头 `<quic/common/version.h>` **不**作为应用 API 提供（它只承载协议常量）。

### 公有 namespace

- `quicx::` —— 顶层公有类型都在这里。
- `quicx::quic::` —— 仅协议级常量（见上）。

任何其他 namespace（`*::internal`、`*::detail` 等）都是实现细节。

---

## `0.x` 期间"稳定"是什么意思

在 `0.x` 系列里，**公有 API 的存在与形态是一个被刻意保留的设计自由度，不是契约**。具体策略：

| 变更类型 | `0.1.z` 补丁 | `0.x → 0.(x+1)` minor | `1.x` | 备注 |
|---|:---:|:---:|:---:|---|
| 不改变正确代码行为的 bug 修复 | ✅ 允许 | ✅ 允许 | ✅ 允许 | |
| 新增函数 / 类型 / 枚举值 | ✅ 允许 | ✅ 允许 | ✅ 允许 | 新枚举值会破坏穷尽 `switch` 的消费者，请留意 |
| 在公有 struct 末尾新增字段 | 🟡 不鼓励但允许 | ✅ 允许 | ⚠ 仅限 minor | 我们不承诺 ABI 稳定（即使在 1.x），请按链接的版本重新编译 |
| 重排 / 改名公有 struct 字段 | ❌ 禁止 | ✅ 允许（`CHANGELOG` 必写） | ❌ 禁止 | |
| 重命名函数 / 类型 / namespace | ❌ 禁止 | ✅ 允许（条件允许时给 deprecation） | ❌ 禁止 | |
| 移除公有符号 | ❌ 禁止 | ✅ 允许（条件允许时给 1 个 minor 的 deprecation） | ❌ 禁止 | |
| 收紧前置条件 / 改变语义 | ❌ 禁止 | ⚠ 允许，需写入 `CHANGELOG` | ❌ 禁止 | |
| 放宽前置条件 / 接受更多输入 | ✅ 允许 | ✅ 允许 | ✅ 允许 | |
| 改变某个 config 字段的默认值 | 🟡 仅限安全修复 | ✅ 允许 | ⚠ 仅限 minor | 默认值变更必写 `CHANGELOG` |
| 改 QUIC / HTTP/3 wire format（受 RFC 约束） | ❌ 除非 RFC 勘误 | ❌ 除非 RFC 勘误 | ❌ 除非 RFC 勘误 | 我们跟随 RFC，不发明自己的协议版本 |

图例：✅ 允许 · 🟡 不鼓励 · ⚠ 必须写入 `CHANGELOG` · ❌ 禁止。

---

## ABI 稳定性

**QuicX 在任何版本（包括 `1.x`）都不承诺 ABI 稳定**。**请始终基于你链接的具体 QuicX 版本重新编译你的应用**。我们在公有 API 中自由使用 C++17 标准库类型（`std::shared_ptr`、`std::string`、`std::function` 等），且 BoringSSL 自身也不承诺 ABI 稳定。

如果你需要稳定的 C ABI，那是 `2.0` 的话题 —— 请提 issue 描述你的嵌入场景。

---

## `0.x` 期间**变动风险更高**的区域

即使在公有面之内，有一些 API 比其他更可能调整。你可以使用，但要紧盯 `CHANGELOG`。

### 高风险（预计在 `0.2.0` / `0.3.0` 调整）

- **`QuicConnectionConfig` 中与连接迁移相关的字段** —— 完整 CID 轮换落地后可能新增选项（见 `SUPPORT_MATRIX`）
- **拥塞控制配置** —— BBR v3 仍是实验性，调参可能搬位置
- **Metrics 名称与 label 集合** —— 我们把 metrics 视为"尽力而为"的可观测性，不是稳定遥测契约。如果你把 QuicX metrics 接到生产监控，请在你自己的采集器里做一层别名
- **QLog 字段 schema** —— 跟随 QLog 规范，规范本身也在演进
- **`MetricsConfig`**（HTTP metrics 端点的形态）
- **Push promise 回调签名** —— 流式 handler 家族是 v0.1.0 中最年轻的 API，可能精简
- **`RetryPolicy::SELECTIVE` 模式下的 Retry 策略参数** —— 启发式阈值可能被换掉

### 低风险（我们会努力不破坏这些）

- `IClient` / `IServer` / `IRequest` / `IResponse` 的基本形态
- `QuicServerConfig` / `QuicClientConfig` / `Http3ServerConfig` / `Http3ClientConfig` 的核心字段（证书 / 私钥路径、监听地址、空闲超时、最大流数）
- `common/include` 下的 `IBufferRead` / `IBufferWrite` 接口
- `IQuicStream` 及其 send / recv 子类的 open / close / read / write 语义
- HTTP 方法注册（`Get`、`Post`…）和路由模式语法
- 中间件顺序：Before → handler → After

---

## 版本号规则

- **达到 `1.0.0` 之后**才正式遵循 [SemVer 2.0](https://semver.org/)
- `0.x` 期间：
  - **Patch**（`0.1.0 → 0.1.1`）：bug 修复、安全修复、文档变更、新增测试、不改变正确代码可观察行为的性能优化
  - **Minor**（`0.1.x → 0.2.0`）：可能新增功能、可能重命名 / 重塑 / 移除公有 API、可能改默认值。一律写入 `CHANGELOG.md`
  - **Major**：在满足"API 冻结清单"之前不会发 `1.0.0`（见下文路线图）
- 我们会**尽量在移除公有符号之前给一个 minor 的 deprecation**，但 `0.x` 期间不强制承诺

### 预发布标签

- `v0.1.0-rc0`、`v0.1.0-rc1`…… 是发布候选，与即将发布的版本共享契约。**不要把长期生产部署 pin 到 `-rc` 标签**。

---

## 头文件 include 模式

虽然当前公有 include 根仍是 `src/<layer>/include/`，但建议这样写：

```cpp
#include <quicx/quic/if_quic_server.h>
#include <quicx/http3/if_server.h>
#include <quicx/common/if_buffer_write.h>
#include <quicx/common/version.h>
```

1.D 阶段（顶层 `include/quicx/` 树）落地后，受支持的写法仍然是上面这种。新布局会与旧布局**并存一个 minor**，给下游消费者迁移窗口，然后旧路径才被移除。

---

## Deprecation 流程

1. 被废弃的符号会被加上 `[[deprecated("use X instead")]]`，并在 `CHANGELOG.md` 的 `### Deprecated` 段写明
2. 替代符号（如有）在同一版本中给出
3. 该符号在 `0.x` 期间最早**下一个 minor**才会被删除；`1.x+` 期间最早**下一个 major** 才会被删除

如果你发现某个不该留下的 API 还没被打 deprecated，请提 issue。

---

## 编译期版本检查

使用 `<common/version.h>` 中的宏：

```cpp
#include <quicx/common/version.h>

#if QUICX_VERSION_MAJOR == 0 && QUICX_VERSION_MINOR < 2
    // 0.1.x 专属代码路径
#endif
```

也提供运行期辅助函数：

```cpp
const char* v = quicx::GetVersionString();   // "0.1.0"
```

---

## 报告"非预期破坏"

如果某个补丁版本（`0.1.z → 0.1.(z+1)`）破坏了你的构建或行为，那是**bug** —— 请按 `CONTRIBUTING.md` 提交。我们会回滚或紧急修复。

如果你认为 minor 版本的破坏过于激进，欢迎提 issue 阐述用例；对高影响下游消费者，我们乐意讨论兼容垫片。

---

## 通往 `1.0.0` 的路径

`1.0.0` 在以下条件**全部**满足之后才发布：

1. `support_matrix.md` 的"已知限制汇总"清空，或对应条目转入"刻意不实现"
2. `include/quicx/` 是唯一受支持的 include 布局（1.D 完成）
3. CI 持续覆盖 Linux + macOS + Windows
4. 与主流实现进行 4 周以上互操作浸泡测试，`handshake` + `transfer` + `resumption` + `keyupdate` 矩阵全部通过
5. ASan / UBSan / TSan 在完整测试集与长时间 soak 上都干净
6. 至少有一个 QuicX 仓库之外的外部依赖者以非平凡方式采纳 `0.x`，并且我们已吸收他们的反馈

在此之前，请把公有 API 视为**仅尽力而为的稳定**。
