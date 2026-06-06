# QuicX API Stability Policy

> Applies to **v0.1.x**. This document specifies which APIs QuicX considers
> public, what stability guarantees apply during the `0.x` series, and how
> guarantees will tighten at `1.0.0`.
>
> Read alongside [`support_matrix.md`](./support_matrix.md) and
> [`CHANGELOG.md`](../../../CHANGELOG.md).

---

## TL;DR

| Phase | Header layout | API breakage policy |
|---|---|---|
| **`0.1.x` patch releases** | Today's headers, currently rooted under `src/<layer>/include/` | **No** breakage. Bug fixes only. |
| **`0.x` minor releases** (`0.2.0`, `0.3.0`, …) | Will move under a top-level `include/quicx/` once 1.D lands | **May break** in clearly documented ways, with one minor-release deprecation window when feasible. |
| **`1.0.0`** | Frozen | SemVer applies. Source-compatible across `1.x`. |

If you embed QuicX today, **pin to an exact `0.1.z` tag** and read the
`CHANGELOG` before bumping.

---

## What is "public"

A symbol is part of the public API **iff** it is declared in one of the
following directories:

| Layer | Public include directory (current) | Future location (post-1.D) |
|---|---|---|
| Common (buffers, types) | `src/common/include/` | `include/quicx/common/` |
| QUIC transport | `src/quic/include/` | `include/quicx/quic/` |
| HTTP/3 | `src/http3/include/` | `include/quicx/http3/` |
| HTTP upgrade | `src/upgrade/include/` | `include/quicx/upgrade/` |

Everything else under `src/**` — including any header named `*_impl.h`, any
header that is `#include`d only from `.cpp` files in the same module, the
internal slab allocator, the timer wheel, the frame codec, the QPACK
encoder/decoder internals, etc. — is **internal**. Do not depend on it.
If you need to, file an issue describing the use case.

### Authoritative public headers in `0.1.x`

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

19 headers total. The `<common/version.h>` product-version header is also
public; the QUIC protocol-version header `<quic/common/version.h>` is **not**
intended for application use (it ships protocol constants only).

### Public namespaces

- `quicx::` — all top-level public types live here
- `quicx::quic::` — QUIC protocol-level constants only (see note above)

Any other namespace (`*::internal`, `*::detail`, etc.) is implementation
detail.

---

## What "stability" means in `0.x`

In the `0.x` series, **the existence and shape of the public API is a
deliberate design surface, not a contract.** Specifically:

| Change kind | `0.1.z` patch | `0.x → 0.(x+1)` minor | `1.x` | Notes |
|---|:---:|:---:|:---:|---|
| Bug fix that does not change behavior of correct code | ✅ allowed | ✅ allowed | ✅ allowed | |
| Add new function / type / enum value | ✅ allowed | ✅ allowed | ✅ allowed | New enumerators may break exhaustive `switch` consumers — beware |
| Add new field at end of public struct | 🟡 discouraged but allowed | ✅ allowed | ⚠ minor only | We do not promise ABI stability even at 1.x; recompile against the version you link |
| Reorder / rename public struct fields | ❌ forbidden | ✅ allowed (with note in CHANGELOG) | ❌ forbidden | |
| Rename function / type / namespace | ❌ forbidden | ✅ allowed (with deprecation when feasible) | ❌ forbidden | |
| Remove public symbol | ❌ forbidden | ✅ allowed (with one-minor deprecation when feasible) | ❌ forbidden | |
| Tighten precondition / change semantics | ❌ forbidden | ⚠ allowed if documented in CHANGELOG | ❌ forbidden | |
| Loosen precondition / accept more inputs | ✅ allowed | ✅ allowed | ✅ allowed | |
| Change default value of a config field | 🟡 only for security fixes | ✅ allowed | ⚠ minor only | We document every default change in CHANGELOG |
| Change wire format of QUIC / HTTP/3 (we are RFC-bound here) | ❌ except RFC errata | ❌ except RFC errata | ❌ except RFC errata | We follow the RFCs, not our own version. |

Legend: ✅ allowed · 🟡 discouraged · ⚠ requires CHANGELOG entry · ❌ forbidden.

---

## ABI stability

**QuicX makes no ABI stability promises in any release line, including
`1.x`.** Always rebuild your application against the exact version of QuicX
you link. We use C++17 standard-library types (`std::shared_ptr`,
`std::string`, `std::function`, …) freely in public APIs, and BoringSSL
itself is not ABI-stable.

If you need a stable C ABI, that would be a `2.0` discussion — please open
an issue describing the embedding scenario.

---

## Areas with **higher** churn risk in `0.x`

Even within the public surface, some APIs are more likely to change than
others. Use them, but watch the CHANGELOG.

### Higher churn (expect changes in `0.2.0` / `0.3.0`)

- **Connection-migration related fields** in `QuicConnectionConfig` —
  may grow new options once full CID rotation lands. (See `SUPPORT_MATRIX`.)
- **Congestion-control configuration** — BBRv3 is experimental; its tuning
  knobs may move.
- **Metrics names and label sets** — we treat metrics as best-effort
  observability, not a stable telemetry contract. If you wire QuicX metrics
  into a production dashboard, alias them inside your own collector.
- **QLog field schema** — follows the QLog spec, which itself is still
  evolving.
- **`MetricsConfig`** (HTTP metrics endpoint shape).
- **Push promise callback signatures** — the streaming-handler family is the
  newest API in v0.1.0 and may be refined.
- **Retry policy parameters** in `RetryPolicy::SELECTIVE` mode — heuristic
  thresholds may be replaced.

### Lower churn (we will try hard not to break these)

- The basic shape of `IClient` / `IServer` / `IRequest` / `IResponse`.
- `QuicServerConfig` / `QuicClientConfig` / `Http3ServerConfig` /
  `Http3ClientConfig` core fields (cert/key paths, listen address, idle
  timeout, max streams).
- The `IBufferRead` / `IBufferWrite` interfaces in `common/include`.
- Stream open / close / read / write semantics on `IQuicStream` and its
  send/recv subclasses.
- HTTP method registration (`Get`, `Post`, …) and routing pattern syntax.
- Middleware ordering: Before → handler → After.

---

## Versioning rules

- We follow [SemVer 2.0](https://semver.org/) **once we reach `1.0.0`**.
- During `0.x`:
  - **Patch** (`0.1.0 → 0.1.1`): bug fixes, security fixes, doc-only changes,
    new tests, performance improvements that do not change observable
    behavior of correct code.
  - **Minor** (`0.1.x → 0.2.0`): may add features, may rename / reshape /
    remove public API, may change defaults. Always documented in
    `CHANGELOG.md`.
  - **Major** (we will not ship a `1.0.0` until the API freeze list is met —
    see Roadmap below).
- We try to provide **one minor release of deprecation** before removing a
  public symbol, but cannot promise it during `0.x`.

### Pre-release tags

- `v0.1.0-rc0`, `v0.1.0-rc1`, … are release candidates. They share the
  contract of the upcoming version. Do not pin a long-lived deployment to
  an `-rc` tag.

---

## Header inclusion patterns

While the public include root is still `src/<layer>/include/`, prefer:

```cpp
#include <quicx/quic/if_quic_server.h>
#include <quicx/http3/if_server.h>
#include <quicx/common/if_buffer_write.h>
#include <quicx/common/version.h>
```

After 1.D ships (a top-level `include/quicx/` tree), the supported pattern
will be:

```cpp
#include <quicx/quic/if_quic_server.h>
#include <quicx/http3/if_server.h>
#include <quicx/common/if_buffer_write.h>
#include <quicx/common/version.h>
```

The new layout will land **alongside** the old one for one minor release
before the old paths are removed, to give downstream consumers a window to
migrate.

---

## Deprecation process

1. The deprecated symbol gains an `[[deprecated("use X instead")]]` attribute
   and a `CHANGELOG` entry under `### Deprecated`.
2. The replacement (if any) is shipped in the same release.
3. The symbol is removed no earlier than the **next minor release** during
   `0.x`, or the **next major release** during `1.x+`.

If you spot something useful that has not been deprecated but you think
should be, please open an issue.

---

## Compile-time version checks

Use the macros from `<common/version.h>`:

```cpp
#include <quicx/common/version.h>

#if QUICX_VERSION_MAJOR == 1 && QUICX_VERSION_MINOR < 1
    // 1.0.x specific code path
#endif
```

A runtime helper is also available:

```cpp
const char* v = quicx::GetVersionString();   // "1.0.0"
```

---

## Reporting an unintended break

If a patch release (`0.1.z → 0.1.(z+1)`) breaks your build or behavior, that
is a **bug** — please file it via `CONTRIBUTING.md`. We will either revert
or hot-fix.

For minor-release breaks that you believe are excessive, please open an
issue with the use case; we are happy to discuss compatibility shims for
high-impact downstream consumers.

---

## Path to `1.0.0`

`1.0.0` will ship after **all** of the following are true:

1. The Known Limitations list in `support_matrix.md` is empty or moved into
   "intentionally out of scope".
2. The `include/quicx/` layout is the only supported include layout (1.D
   complete).
3. CI continuously covers Linux + macOS + Windows.
4. A 4-week interop bake against the major implementations passes the
   `handshake` + `transfer` + `resumption` + `keyupdate` matrix.
5. ASan / UBSan / TSan are clean on the full test corpus and on a
   long-running soak.
6. At least one external dependent (other than the QuicX repo itself) has
   adopted `0.x` in a non-trivial way, and we have absorbed their feedback.

Until then, treat the public API as **stable on best-effort basis only.**
