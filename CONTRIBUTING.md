# Contributing to QuicX

Thanks for taking the time to contribute! This document describes how to
build, test, and submit changes to **QuicX**.

> 📌 **TL;DR**
> 1. Clone with `--recurse-submodules`.
> 2. `cmake -S . -B build && cmake --build build -j` to build.
> 3. `python3 run_tests.py` to run the full test suite locally.
> 4. Match the surrounding style (project root has no `.clang-format` yet — see [Style](#style)).
> 5. Open a PR with a clear description and link to any related issue.

---

## Table of contents

- [Code of conduct](#code-of-conduct)
- [How can I help?](#how-can-i-help)
- [Reporting bugs](#reporting-bugs)
- [Reporting security vulnerabilities](#reporting-security-vulnerabilities)
- [Development environment](#development-environment)
- [Building](#building)
- [Running tests](#running-tests)
- [Style](#style)
- [Commit messages](#commit-messages)
- [Pull-request checklist](#pull-request-checklist)
- [Architecture overview for new contributors](#architecture-overview-for-new-contributors)
- [Release process (maintainers)](#release-process-maintainers)

---

## Code of conduct

Be kind, assume good faith, and keep discussions technical.  We follow the
spirit of the
[Contributor Covenant](https://www.contributor-covenant.org/version/2/1/code_of_conduct/);
a formal `CODE_OF_CONDUCT.md` is on the v0.1 backlog.

---

## How can I help?

Areas where contributions are particularly welcome:

- **Interop fixes** — see [`docs/en/reports/interop_status.md`](docs/en/reports/interop_status.md)
  for the current pass/fail matrix.  Fixing a single failing scenario for a
  popular peer (e.g. quinn, msquic) is a great first PR.
- **Performance** — `cc_simulator`, `test/perf/`, and the flamegraph notes
  under `docs/PERF_*` are good entry points.
- **Documentation** — examples, tutorials, API reference for headers under
  `src/quic/include/` and `src/http3/include/`.
- **Platform support** — macOS and Windows are exercised locally but not
  yet covered by automated cross-platform CI; reproducing build / runtime
  issues on those platforms is very valuable.
- **Fuzzing** — adding new fuzz targets under `test/fuzz/` and reporting
  any crashes you find.

---

## Reporting bugs

Please include the following in any bug report:

1. **What you did** — minimal reproducer, ideally as a unit test or
   `example/`-style program.
2. **What you expected** vs **what you observed**.
3. **Environment** — OS, compiler + version, CMake version, commit hash
   (`git rev-parse HEAD`) or release tag.
4. **Logs** — a `qlog` trace (`-DQUICX_ENABLE_QLOG=ON`) and a `pcap` are
   gold for protocol bugs.

If a bug is **only reproducible against a specific peer**, please mention
which peer (quinn / msquic / aioquic / …) and the version, so we can update
[`docs/en/reports/interop_status.md`](docs/en/reports/interop_status.md).

---

## Reporting security vulnerabilities

**Do not open a public GitHub issue for security bugs.**  Follow the
private-disclosure process described in [`SECURITY.md`](SECURITY.md).

---

## Development environment

| Tool / library      | Version                | Notes                                                |
|---------------------|------------------------|------------------------------------------------------|
| C++ compiler        | C++17 (GCC ≥ 9, Clang ≥ 12, MSVC 19.x) | The project uses `set(CMAKE_CXX_STANDARD 17)`.        |
| CMake               | ≥ 3.16                 | Primary build system.                                |
| Bazel               | recent                 | Optional (see `BUILD.bazel` / `WORKSPACE.bazel`).    |
| BoringSSL           | submodule under `third/boringssl` | **Do NOT** replace with system OpenSSL — QUIC requires APIs only present in BoringSSL. |
| Python 3            | ≥ 3.8                  | Required by `run_tests.py` and the interop runner.   |
| Sanitizers          | ASan / UBSan / TSan via clang or gcc | Strongly recommended on every PR before pushing.     |
| `clang-tidy`        | ≥ 14                   | Optional today, will become CI-blocking — see [Style](#style). |

### Cloning with submodules

```bash
git clone --recurse-submodules https://github.com/caozhiyi/quicX.git
cd quicX
# If you forgot the flag at clone time:
git submodule update --init --recursive
```

---

## Building

### CMake (primary)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

Useful options (default in parentheses):

| Option                        | Default | Effect                                       |
|-------------------------------|:-------:|----------------------------------------------|
| `BUILD_EXAMPLES`              | `ON`    | Build everything under `example/`            |
| `ENABLE_TESTING`              | `ON`    | Build unit tests (`quicx_utest`)             |
| `ENABLE_BENCHMARKS`           | `ON`    | Build Google Benchmark micro-benchmarks       |
| `ENABLE_FUZZING`              | `OFF`   | Build libFuzzer targets (requires Clang)     |
| `ENABLE_CC_SIMULATOR`         | `ON`    | Build `cc_simulator`                         |
| `ENABLE_INTERGRATION` *(sic)* | `ON`    | Build integration tests under `test/integration` |
| `ENABLE_INTEROP`              | `OFF`   | Build the public quic-interop runner harness |
| `ENABLE_PERF_TESTS`           | `ON`    | Build `test/perf/` baselines                 |
| `QUICX_ENABLE_QLOG`           | `ON`    | Compile in QLog support                      |

### Sanitizer builds

```bash
# AddressSanitizer + UndefinedBehaviorSanitizer
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan -j

# ThreadSanitizer (separate build dir, since ASan and TSan are mutually exclusive)
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
cmake --build build-tsan -j
```

### Bazel (alternative)

```bash
bazel build //...
```

---

## Running tests

The single command that exercises **everything** is:

```bash
python3 run_tests.py
```

It reports unit / integration / realistic-network / performance results
in a single summary at the end.

To run subsets directly:

```bash
# Unit tests only (1191 tests as of v0.1.0)
./build/bin/quicx_utest

# Filter by pattern
./build/bin/quicx_utest --gtest_filter='FrameDecodeTest.*'

# Congestion-control simulator
./build/bin/cc_simulator

# Fuzzing (after building with -DENABLE_FUZZING=ON using Clang)
./build_fuzz/bin/fuzz_<target> -max_total_time=60
```

When fixing a bug, **add a regression test that fails before your fix and
passes after.**  This is non-negotiable — the FrameDecoder fix in 0.1.0
shipped with three regression tests for exactly this reason.

---

## Style

The project root does **not** ship a `.clang-format` yet (tracked as
RELEASE_PLAN_v0.1.0 task 2.C.6).  Until it does, please follow the local
conventions you can observe in the file you are editing:

- **Indentation**: 4 spaces, no tabs.
- **Braces**: `K&R` / Google style — open brace on the same line as the
  declaration.
- **Naming**:
  - Classes / structs: `PascalCase`.
  - Methods: `PascalCase` for public, `lowerCamel` is **not** used.
  - Member variables: `lower_snake_case_` (trailing underscore).
  - Local variables: `lower_snake_case`.
  - Constants / enum values: `kPascalCase` (e.g. `kQuicVersion1`).
  - Macros: `UPPER_SNAKE_CASE`.
- **Includes**: project headers use angle brackets relative to `src/`,
  e.g. `#include "common/version.h"`.  System headers come first, then a
  blank line, then project headers.
- **Comments**: prefer self-explanatory code; reserve comments for
  **why**, not **what**.  RFC references in comments are very welcome
  (e.g. `// RFC 9000 §17.2.5`).

If a clang-tidy run flags `bugprone-*`, `cert-*`, or `security-*` warnings
in your patch, please address them.  These categories are slated to become
CI-blocking before 1.0.

---

## Commit messages

We use a lightweight Conventional-Commits-flavoured style.  Every commit
should be small, atomic, and self-explanatory.

```
<type>(<scope>): <short imperative summary>

<longer body explaining *why*, not *what*, wrapped at ~72 columns.
Reference issues, RFCs, or design docs as appropriate.>

<footer with "Fixes #N" / "Closes #N" / "Refs #N">
```

Common `type` values:

| Type       | When to use                                                    |
|------------|----------------------------------------------------------------|
| `feat`     | New user-visible functionality                                 |
| `fix`      | Bug fix                                                        |
| `perf`     | Performance improvement with no behavioural change             |
| `refactor` | Code restructure with no behavioural change                    |
| `test`     | Tests-only change                                              |
| `docs`     | Documentation-only change                                      |
| `build`    | Build system, CMake, Bazel, third-party submodule              |
| `ci`       | CI configuration                                               |
| `chore`    | Anything else (release prep, version bump, etc.)               |

Common `scope` values: `quic`, `http3`, `qpack`, `cc`, `tls`, `loss`,
`buffer`, `qlog`, `metrics`, `interop`, `build`, `docs`.

Example:

```
fix(http3): preserve unknown-frame state across OnData calls

Adds a new kReadingUnknownLength state to FrameDecoder so the already-
consumed unknown frame type is remembered when the length varint arrives
in a later OnData call.  Without this, the next OnData would mis-interpret
the length bytes as a new frame type and corrupt the decoder state — a
malicious peer could trigger ~700M skip counts.

Refs RELEASE_PLAN_v0.1.0 task 1.A.
```

---

## Pull-request checklist

Before opening a PR, please tick off the following yourself:

- [ ] **Builds** with `cmake --build build` on at least one of GCC/Clang.
- [ ] **`python3 run_tests.py` passes** (or the failures are pre-existing
      and explained in the PR description).
- [ ] **New tests added** for any bug fix or new behaviour, and they
      **fail without** your code change.
- [ ] **Public-API changes** are mentioned in `CHANGELOG.md` under
      `## [Unreleased]`.
- [ ] **No accidental `printf`/`std::cout` debug** left behind — use the
      project logger if logging is needed.
- [ ] **No vendored third-party code** in `third/` other than via
      submodule.
- [ ] **Sanitizer-clean** if you can run ASan / UBSan locally.

The PR description should answer:

1. **What** does this change?
2. **Why** is it needed?
3. **How** did you verify it (commands run, tests added)?
4. **Risk** and rollback notes if any.

We aim for a first review within a few business days.  Small, focused PRs
land much faster than sweeping ones.

---

## Architecture overview for new contributors

Roughly top-down, the layers are:

```
example/  ─┐
           ▼
src/http3/   ── HTTP/3 framing, QPACK, routing, push
src/upgrade/ ── HTTP/1.1 → HTTP/3 upgrade
src/quic/    ── QUIC connection, streams, congestion, loss, packet/frame codec, TLS
src/common/  ── buffers, allocators, network I/O, timers, log, metrics, qlog
third/       ── BoringSSL (submodule), GoogleTest, GoogleBenchmark
test/        ── unit_test, integration, perf, fuzz, congestion_control simulator, interop
docs/        ── user docs (en/zh), design notes, release plan
```

Useful entry-point reading for newcomers:

- `docs/en/tutorial/quic_api_guide.md` — QUIC engine / connection / stream
  abstractions.
- `docs/en/tutorial/http3_api_guide.md` — server, client, routing, async
  streaming, server push.
- `docs/en/tutorial/configuration_reference.md` — every knob exposed by
  `QuicConfig` / `Http3Config`.
- `docs/zh/reports/performance_baseline.md` — current performance numbers and how
  they were measured.

---

## Release process (maintainers)

The active release plan lives in
[`docs/release_plan_v0.1.0.md`](docs/release_plan_v0.1.0.md).  Any change
that affects the release schedule, the supported-versions table in
[`SECURITY.md`](SECURITY.md), or the public-API surface should be reflected
there as well.

When bumping the version:

1. Update `VERSION` (root file).
2. Update `QUICX_VERSION_MAJOR/MINOR/PATCH` in `src/common/version.h`.
3. Update `project(QuicX VERSION X.Y.Z ...)` in the top-level
   `CMakeLists.txt`.
4. Move the `## [Unreleased]` section in `CHANGELOG.md` to a new dated
   section.
5. Run `python3 run_tests.py` clean.
6. Snapshot via `bash scripts/snapshot.sh vX.Y.Z`.

---

Thanks again for contributing.  See you in the PR queue.
