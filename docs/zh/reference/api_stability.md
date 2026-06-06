# QuicX API 稳定性策略

> 适用于 **v1.0.x**。本文档说明 QuicX 把哪些符号视为"公有 API"，以及在当前阶段
> 我们对源码 / ABI 兼容性给出的（非常有限的）承诺。
>
> 配套阅读：
> - [`support_matrix.md`](./support_matrix.md)（功能支持矩阵）
> - [`../../../CHANGELOG.md`](../../../CHANGELOG.md)（变更日志）

---

## 当前阶段

代码、文档、测试自洽，读者可以从 `example/hello_world` 一路跟到 UDP
I/O、包解析、握手、流、拥塞控制、HTTP/3 + QPACK；**它不意味着公有 C++ API 在
后续 1.x 版本之间不会变**。如果你要把 QuicX 嵌入到自己的项目里，**请 pin 到
具体的 `1.0.z` 标签**，升级前先读 `CHANGELOG.md`。

| 阶段 | 头文件布局 | API 破坏策略 |
|---|---|---|
| **`1.0.x` 补丁版** | `include/quicx/<layer>/` | **不破坏**（仅修 bug、补文档、加测试、不影响可观察行为的性能优化） |
| **`1.x` minor 版**（`1.1.0` / `1.2.0`…） | 同上 | **可能破坏**，会写入 `CHANGELOG`；条件允许时给一个 minor 的 deprecation 窗口 |
| **`2.0.0`** | 待定 | 重大重构 / C ABI / 等议题的承载版本 |

---

## 什么算"公有 API"

一个符号是公有 API **当且仅当** 它声明在以下目录之一：

| 层 | 公有 include 目录 |
|---|---|
| 公共组件（buffer、type 等） | `include/quicx/common/` |
| QUIC 传输层 | `include/quicx/quic/` |
| HTTP/3 | `include/quicx/http3/` |
| HTTP 升级 | `include/quicx/upgrade/` |

`src/**` 下的所有东西 —— 包括任何 `if_*.h`、只在同一模块的 `.cpp` 中 include
的头、内部 slab 分配器、时间轮、帧编解码器、QPACK 编/解码器内部细节等 ——
都是**内部实现**。**请不要依赖**。如果你确实需要依赖，请提 issue 描述用例。

### `1.0.x` 公有头清单（权威）

```
include/quicx/common/
  if_buffer_read.h
  if_buffer_write.h
  type.h
  version.h

include/quicx/quic/
  if_quic_bidirection_stream.h
  if_quic_client.h
  if_quic_connection.h
  if_quic_recv_stream.h
  if_quic_send_stream.h
  if_quic_server.h
  if_quic_stream.h
  type.h

include/quicx/http3/
  if_async_handler.h
  if_client.h
  if_request.h
  if_response.h
  if_server.h
  type.h

include/quicx/upgrade/
  if_upgrade.h
  type.h
```

`<quicx/common/version.h>` 是产品版本号头，也是公有；QUIC 协议版本号头
`<quicx/quic/common/version.h>` **不**作为应用 API 提供（它只承载协议常量）。

### 公有 namespace

- `quicx::` —— 顶层公有类型都在这里。
- `quicx::quic::` —— 仅协议级常量（见上）。

任何其他 namespace（`*::internal`、`*::detail` 等）都是实现细节。

---

## "稳定"是什么意思

在当前阶段，**公有 API 的存在与形态是一个被刻意保留的设计自由度，不是契约**。
具体策略：

| 变更类型 | `1.0.z` 补丁 | `1.x → 1.(x+1)` minor | `2.0` major | 备注 |
|---|:---:|:---:|:---:|---|
| 不改变正确代码行为的 bug 修复 | ✅ 允许 | ✅ 允许 | ✅ 允许 | |
| 新增函数 / 类型 / 枚举值 | ✅ 允许 | ✅ 允许 | ✅ 允许 | 新枚举值会破坏穷尽 `switch` 的消费者，请留意 |
| 在公有 struct 末尾新增字段 | 🟡 不鼓励但允许 | ✅ 允许 | ✅ 允许 | 我们不承诺 ABI 稳定，请按链接的版本重新编译 |
| 重排 / 改名公有 struct 字段 | ❌ 禁止 | ✅ 允许（`CHANGELOG` 必写） | ✅ 允许 | |
| 重命名函数 / 类型 / namespace | ❌ 禁止 | ✅ 允许（条件允许时给 deprecation） | ✅ 允许 | |
| 移除公有符号 | ❌ 禁止 | ✅ 允许（条件允许时给 1 个 minor 的 deprecation） | ✅ 允许 | |
| 收紧前置条件 / 改变语义 | ❌ 禁止 | ⚠ 允许，需写入 `CHANGELOG` | ✅ 允许 | |
| 放宽前置条件 / 接受更多输入 | ✅ 允许 | ✅ 允许 | ✅ 允许 | |
| 改变某个 config 字段的默认值 | 🟡 仅限安全修复 | ✅ 允许 | ✅ 允许 | 默认值变更必写 `CHANGELOG` |
| 改 QUIC / HTTP/3 wire format（受 RFC 约束） | ❌ 除非 RFC 勘误 | ❌ 除非 RFC 勘误 | ❌ 除非 RFC 勘误 | 我们跟随 RFC，不发明自己的协议版本 |

图例：✅ 允许 · 🟡 不鼓励 · ⚠ 必须写入 `CHANGELOG` · ❌ 禁止。

---

## ABI 稳定性

**QuicX 不承诺 ABI 稳定**（`1.x` 也不承诺）。**请始终基于你链接的具体 QuicX
版本重新编译你的应用**。我们在公有 API 中自由使用 C++17 标准库类型
（`std::shared_ptr`、`std::string`、`std::function` 等），且 BoringSSL 自身也不
承诺 ABI 稳定。

如果你需要稳定的 C ABI，那是 `2.0` 的话题 —— 请提 issue 描述你的嵌入场景。

## 版本号规则

QuicX **严格遵循 SemVer**。我们采用如下约定（与 `CHANGELOG.md` 的"Versioning"
段一致）：

- **Patch**（`1.0.0 → 1.0.1`）：bug 修复、安全修复、文档变更、新增测试、不改变正确代码可观察行为的性能优化
- **Minor**（`1.0.x → 1.1.0`）：可能新增功能、可能重命名 / 重塑 / 移除公有 API、可能改默认值。一律写入 `CHANGELOG.md`
- **Major**（`1.x → 2.0`）：保留给重大重构（例如稳定 C ABI、整体架构更换等）
- 我会**尽量在移除公有符号之前给一个 minor 的 deprecation**

### 预发布标签

- `vX.Y.Z-rc0`、`vX.Y.Z-rc1`…… 是发布候选，与即将发布的版本共享契约。**不要把长期生产部署 pin 到 `-rc` 标签**。

---

## 头文件 include 模式

公有 include 根是 `include/quicx/`。请按下列方式 include：

```cpp
#include <quicx/quic/if_quic_server.h>
#include <quicx/http3/if_server.h>
#include <quicx/common/if_buffer_write.h>
#include <quicx/common/version.h>
```

---

## Deprecation 流程

1. 被废弃的符号会被加上 `[[deprecated("use X instead")]]`，并在 `CHANGELOG.md` 的 `### Deprecated` 段写明
2. 替代符号（如有）在同一版本中给出
3. 该符号最早**下一个 minor**才会被删除

如果你发现某个不该留下的 API 还没被打 deprecated，请提 issue。

---

## 编译期版本检查

使用 `<quicx/common/version.h>` 中的宏：

```cpp
#include <quicx/common/version.h>

#if QUICX_VERSION_MAJOR == 1 && QUICX_VERSION_MINOR < 1
    // 1.0.x 专属代码路径
#endif
```

也提供运行期辅助函数：

```cpp
const char* v = quicx::GetVersionString();   // 例如 "1.0.0"
```

---

## 报告"非预期破坏"

如果某个补丁版本（`1.0.z → 1.0.(z+1)`）破坏了你的构建或行为，那是**bug** ——
请按 `CONTRIBUTING.md` 提交。我们会回滚或紧急修复。

如果你认为 minor 版本的破坏过于激进，欢迎提 issue 阐述用例；对高影响下游
消费者，我们乐意讨论兼容垫片。
