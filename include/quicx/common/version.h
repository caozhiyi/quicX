// Copyright (c) the QuicX authors.
// SPDX-License-Identifier: BSD-3-Clause
#ifndef QUICX_COMMON_VERSION_H
#define QUICX_COMMON_VERSION_H

// QuicX library / product version (Semantic Versioning, https://semver.org).
//
// NOTE: This is the *product* version of the QuicX library.  Do NOT confuse it
// with the on-the-wire QUIC protocol version (RFC 9000 / RFC 9369), which is
// defined separately in <quic/common/version.h> as
// `quicx::quic::kQuicVersion1` / `kQuicVersion2`.
//
// During the 0.x series the public API is NOT considered stable.  Any 0.y.z
// release MAY introduce source-incompatible changes; downstream code should
// pin to an exact patch release.  Stability guarantees begin at 1.0.0.
//
// The numeric macros here MUST stay in sync with:
//   - the top-level `CMakeLists.txt` `project(... VERSION X.Y.Z)` line, and
//   - the root `VERSION` file.
// The CMake build is the single source of truth; this header is updated at
// the same time when the version is bumped.

#define QUICX_VERSION_MAJOR 0
#define QUICX_VERSION_MINOR 1
#define QUICX_VERSION_PATCH 0

// Helper macros to stringify the version components at preprocessing time.
#define QUICX_VERSION_STRINGIFY_IMPL(x) #x
#define QUICX_VERSION_STRINGIFY(x) QUICX_VERSION_STRINGIFY_IMPL(x)

// Human-readable version string, e.g. "0.1.0".
#define QUICX_VERSION_STRING            \
    QUICX_VERSION_STRINGIFY(QUICX_VERSION_MAJOR) "." \
    QUICX_VERSION_STRINGIFY(QUICX_VERSION_MINOR) "." \
    QUICX_VERSION_STRINGIFY(QUICX_VERSION_PATCH)

// Single 32-bit integer encoding suitable for compile-time comparisons.
//   QUICX_VERSION >= QUICX_VERSION_NUMBER(0, 2, 0)
// is the recommended way to feature-gate against a minimum library version.
#define QUICX_VERSION_NUMBER(major, minor, patch) \
    (((major) * 10000) + ((minor) * 100) + (patch))

#define QUICX_VERSION \
    QUICX_VERSION_NUMBER(QUICX_VERSION_MAJOR, QUICX_VERSION_MINOR, QUICX_VERSION_PATCH)

namespace quicx {

// Returns the QuicX library version as a null-terminated string ("0.1.0").
// Safe to call from any thread; the returned pointer has static storage
// duration and must not be freed by the caller.
inline const char* GetVersionString() {
    return QUICX_VERSION_STRING;
}

// Returns the encoded numeric version (see QUICX_VERSION_NUMBER above).
inline unsigned int GetVersionNumber() {
    return QUICX_VERSION;
}

}  // namespace quicx

#endif  // QUICX_COMMON_VERSION_H
