# QuicX API Stability Policy

Applies to **v1.0 and later**. This document specifies which symbols QuicX
treats as "public API" and the compatibility promise we make: **since 1.0,
QuicX follows semantic versioning (SemVer)** — patch and minor releases do
not break the public API; breaking changes land in major releases only.

Read alongside:
- [`support_matrix.md`](./support_matrix.md) (feature support matrix)
- [`../../../CHANGELOG.md`](../../../CHANGELOG.md) (change log)

---

## Versioning policy

Since **v1.0**, QuicX follows **SemVer**: within a major version (`1.x`), the
public API is **source-compatible** — neither patch nor minor releases break
existing callers; breaking changes happen only in major releases (`2.0`), and
always with a deprecation window beforehand. Read `CHANGELOG.md` before
upgrading.

| Phase | Header layout | API breakage policy |
|---|---|---|
| **`1.0.x` patch releases** | `include/quicx/<layer>/` | **No breakage** (bug fixes, docs, tests, and perf work that does not change observable behavior only) |
| **`1.x` minor releases** (`1.1.0` / `1.2.0`…) | same | **No breakage** (new features and symbols only; no renames, reshapes, or removals of public API) |
| **`2.0.0`** | TBD | the vehicle for major rework / C ABI / related topics; breaking changes land here at the earliest |

---

## What counts as "public API"

A symbol is public API **iff** it is declared in one of the following
directories:

| Layer | Public include directory |
|---|---|
| Common (buffers, types, metrics) | `include/quicx/common/` |
| QUIC transport | `include/quicx/quic/` |
| HTTP/3 | `include/quicx/http3/` |
| HTTP upgrade | `include/quicx/upgrade/` |

Everything under `src/**` — including any `if_*.h` internal headers, headers
included only from `.cpp` files of the same module, the internal slab
allocator, the timer wheel, the frame codecs, QPACK encoder/decoder internals,
etc. — is **internal implementation**. **Do not depend on it.** If you really
need to, please open an issue describing the use case.

### Authoritative public headers in `1.0.x`

```
include/quicx/common/
  if_buffer_read.h
  if_buffer_write.h
  metrics.h
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

`<quicx/common/version.h>` is the product-version header and is public; the
QUIC wire-protocol version constants (`quicx::kQuicVersion1` /
`quicx::kQuicVersion2`) live in `<quicx/quic/type.h>` and are public as well.
`src/quic/common/version.h` is internal and **not** intended for application
use.

### Public namespaces

- `quicx::` — all top-level public types and protocol constants (including
  `kQuicVersion1` / `kQuicVersion2`) live here.

Any other namespace (`*::internal`, `*::detail`, etc.) is implementation
detail.

---

## What "stable" means

The shape of the public API **is a contract**: source compatibility is
guaranteed within `1.x`. Specifically:

| Change kind | `1.0.z` patch | `1.x → 1.(x+1)` minor | `2.0` major | Notes |
|---|:---:|:---:|:---:|---|
| Bug fix that does not change behavior of correct code | ✅ allowed | ✅ allowed | ✅ allowed | |
| Add new function / type / enum value | ✅ allowed | ✅ allowed | ✅ allowed | New enumerators may break exhaustive `switch` consumers — beware |
| Add new field at end of public struct | 🟡 discouraged but allowed | ✅ allowed | ✅ allowed | We do not promise ABI stability; recompile against the version you link |
| Reorder / rename public struct fields | ❌ forbidden | ❌ forbidden | ✅ allowed | |
| Rename function / type / namespace | ❌ forbidden | ❌ forbidden | ✅ allowed | |
| Remove public symbol | ❌ forbidden | ❌ forbidden | ✅ allowed | at least one minor of `[[deprecated]]` window before removal |
| Tighten precondition / change semantics | ❌ forbidden | ❌ forbidden | ✅ allowed | |
| Loosen precondition / accept more inputs | ✅ allowed | ✅ allowed | ✅ allowed | |
| Change default value of a config field | 🟡 security fixes only | 🟡 security fixes only | ✅ allowed | Every default change must be in `CHANGELOG` |
| Change QUIC / HTTP/3 wire format (RFC-bound) | ❌ except RFC errata | ❌ except RFC errata | ❌ except RFC errata | We follow the RFCs, not our own protocol version |

Legend: ✅ allowed · 🟡 discouraged · ⚠ requires CHANGELOG entry · ❌ forbidden.

---

## ABI stability

**QuicX makes no ABI stability promises** (not even for `1.x`). **Always
rebuild your application against the exact QuicX version you link.** We use
C++17 standard-library types (`std::shared_ptr`, `std::string`,
`std::function`, …) freely in public APIs, and BoringSSL itself is not
ABI-stable either.

If you need a stable C ABI, that is a `2.0` topic — please open an issue
describing your embedding scenario.

## Versioning rules

Since 1.0, QuicX **follows SemVer**:

- **Patch** (`1.0.0 → 1.0.1`): bug fixes, security fixes, doc changes, new
  tests, performance improvements that do not change observable behavior of
  correct code
- **Minor** (`1.0.x → 1.1.0`): new features and new public symbols; **no
  breakage** of existing public API (no renames / reshapes / removals), no
  default changes. Always documented in `CHANGELOG.md`
- **Major** (`1.x → 2.0`): reserved for breaking changes and major rework
  (e.g. a stable C ABI, an overall architecture change)
- Removal of a public symbol is always preceded by **at least one minor of
  `[[deprecated]]` window**

### Pre-release tags

- `vX.Y.Z-rc0`, `vX.Y.Z-rc1`, … are release candidates sharing the contract of
  the upcoming version. Do not pin a long-lived deployment to an `-rc` tag.

---

## Header inclusion patterns

The public include root is `include/quicx/`. Include like this:

```cpp
#include <quicx/quic/if_quic_server.h>
#include <quicx/http3/if_server.h>
#include <quicx/common/if_buffer_write.h>
#include <quicx/common/version.h>
```

---

## Deprecation process

1. The deprecated symbol gains `[[deprecated("use X instead")]]` and an entry
   under `### Deprecated` in `CHANGELOG.md`
2. The replacement (if any) ships in the same release
3. The symbol is removed no earlier than the **next major (`2.0`)** release

If you spot an API that should have been deprecated but was not, please open
an issue.

---

## Compile-time version checks

Use the macros from `<quicx/common/version.h>`:

```cpp
#include <quicx/common/version.h>

#if QUICX_VERSION_MAJOR == 1 && QUICX_VERSION_MINOR < 1
    // 1.0.x-specific code path
#endif
```

A runtime helper is also available:

```cpp
const char* v = quicx::GetVersionString();   // e.g. "1.0.0"
```

---

## Reporting an unintended break

If a patch release (`1.0.z → 1.0.(z+1)`) breaks your build or behavior, that
is a **bug** — please file it via `CONTRIBUTING.md`. We will revert or
hot-fix.

If a minor release (`1.x → 1.(x+1)`) breaks your build or behavior, that is
likewise treated as a **bug** — please open an issue with the use case; for
high-impact downstream consumers we are happy to discuss compatibility shims.
