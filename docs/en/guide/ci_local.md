# Local CI Debugging Guide

This repo depends on no extra local wrapper scripts: every check executed in `.github/workflows/*.yml` can be reproduced locally with the repo-root `cmake` + `run_tests.py`. You can close the loop entirely locally; only pushing images to GHCR / publishing a Release needs GitHub.

Modes supported by `run_tests.py`: `all` / `utest` / `example` / `integration` / `fuzz` / `benchmark` / `perf` / `cc`.

> Note: `run_tests.py` hard-codes the `build/` path. If, like CI, you use multiple build directories (`build-asan`, `build-cov`, `build-fuzz`, …), symlink `build` to the target directory as the workflows do: `rm -rf build && ln -s build-asan build`.

## Quick Start

```bash
cd /data/workspace/quicX

# Build and run unit + integration tests (matches the build-and-test job of ci.yml)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTING=ON
cmake --build build --parallel 4
python3 run_tests.py utest
python3 run_tests.py integration

# Other common modes
python3 run_tests.py example       # examples
python3 run_tests.py benchmark     # benchmarks
python3 run_tests.py perf          # test/perf performance tests
python3 run_tests.py cc            # congestion-control simulator tests
```

## Workflow ↔ Local Command Equivalents

| Workflow File | Trigger | Local Equivalent | GitHub Required? |
|---|---|---|---|
| `ci.yml` | push / PR | `cmake -S . -B build ... && cmake --build build` + `run_tests.py utest` / `integration` | ❌ |
| `sanitizer.yml` | push / PR / daily | clang + `-fsanitize={address,undefined,thread}` builds into `build-{asan,ubsan,tsan}`, then `run_tests.py utest` | ❌ |
| `coverage.yml` | push / PR | gcc + `--coverage` + lcov (see below) | ❌ |
| `lint.yml` | push / PR | `clang-format --dry-run --Werror <files>` / `clang-tidy -p build-tidy <files>` | ❌ |
| `fuzz-smoke.yml` | PR / nightly | `-DENABLE_FUZZING=ON` build into `build-fuzz`, short run of each target | ❌ |

## Three Local Debugging Postures

### Posture 1: cmake + run_tests.py directly (recommended)

The closest to the development loop — build + test straight on the host, fast, no Docker.

```bash
# edit code → rebuild → run only the affected tests
vim src/quic/stream/send_stream.cpp
cmake --build build --parallel 4
python3 run_tests.py utest
```

CI's `build-and-test` job matrix is `{gcc, clang} × {Debug, Release}` — 4 configurations; locally, covering at least `gcc Debug` and `clang Release` is recommended. CI config parameters: `-DENABLE_TESTING=ON -DBUILD_EXAMPLES=ON -DENABLE_INTEROP=OFF -DENABLE_FUZZING=OFF`.

### Posture 2: Run workflows for real in local Docker with `act`

[act](https://github.com/nektos/act) can emulate the `ubuntu-latest` runner in local Docker, nearly equivalent to real CI.

```bash
# 1. Install act (one time)
curl -s https://raw.githubusercontent.com/nektos/act/master/install.sh | sudo bash -s -- -b /usr/local/bin

# 2. Common commands
act -l                                   # list all workflows and jobs
act -n                                   # dry-run, syntax check only (no execution)
act -W .github/workflows/ci.yml          # run ci.yml
act -j build-and-test                    # run a specific job
```

The first run pulls `catthehacker/ubuntu:full-22.04` (~1-2 GB); be patient. It's cached afterwards.

**Caveats when using act:**
- `schedule` triggers, `secrets`, GHCR pushes etc. don't truly take effect, but the execution path is observable.
- `windows-latest` / `macos-latest` are unsupported; those parts need real CI.

### Posture 3: Pure command line (for AI agents / scripts)

No script dependency at all:

```bash
# build + test (matches CI's build-and-test job)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTING=ON
cmake --build build --parallel 4
python3 run_tests.py utest
python3 run_tests.py integration
```

## Per-Job Details

### build (ci.yml)
- Compiles all static libs + unit tests + integration tests + examples
- Matrix: {gcc, clang} × {Debug, Release} = 4 configurations
- Locally, running the `gcc Debug` and `clang Release` pair is recommended coverage

### sanitize (sanitizer.yml)
- clang only, building into `build-asan` / `build-ubsan` / `build-tsan`
- asan: `-fsanitize=address -fno-omit-frame-pointer -O1 -g`, checking heap overflow / use-after-free / leaks
- ubsan: `-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -O1 -g`, checking undefined behavior (integer overflow, null-pointer dereference, etc.)
- tsan: `-fsanitize=thread -fno-omit-frame-pointer -O1 -g`, checking data races
- After building, symlink `build` to the target directory, then run `run_tests.py utest`
- **TSAN is especially important**: quicX has many multi-threaded workers; run tsan at least once locally

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
- gcc + `--coverage -O0 -g -fprofile-update=atomic`, building into `build-cov`
- lcov excludes `/usr/*`, `third/`, `build*/`, `test/`
- Generates the HTML report: `coverage-html/index.html`
- Threshold is 60% (currently below-threshold only warns, non-blocking)

### interop (moved out of CI)

> The in-repo interop test environment (`interop_runner.py` and the related
> compose files) has been removed, together with the `interop.yml` CI
> workflow. Interop testing now runs directly with the official
> [quic-interop-runner](https://github.com/quic-interop/quic-interop-runner)
> (build the `quicx-interop:latest` image locally, then let the official
> runner schedule it). See [`guide/interop_runbook.md`](./interop_runbook.md)
> for the full workflow.

### lint (lint.yml)
- On PRs, checks only **changed files** (git diff)
- clang-format: `clang-format --dry-run --Werror <file>`
- clang-tidy: first generate `build-tidy/compile_commands.json` with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, then `clang-tidy -p build-tidy <file>`
- Currently non-blocking (warning)

### fuzz-smoke (fuzz-smoke.yml)
- Configured `-DENABLE_FUZZING=ON -DENABLE_TESTING=OFF -DBUILD_EXAMPLES=OFF`, building `build-fuzz`
- libFuzzer runs each target for 60s
- Crashes found are uploaded to artifacts
- Corpora kept in `fuzz-corpus/` (when run locally)

## FAQ

### Q: Local build passes, why does CI fail?
1. **submodules**: CI uses `submodules: recursive`; locally run `git submodule update --init --recursive` first
2. **dependencies**: CI uses ubuntu-22.04; an older local system may lack `ninja-build` / `lcov` / `clang-14` / `clang-format-14` / `clang-tidy-14`
3. **concurrency**: local `run_tests.py` runs integration concurrently; multiple connections binding the same port can conflict — adjust env vars or run serially

### Q: act is too slow / eats too much disk?
- Use `act -n` to dry-run and validate yaml syntax first
- Just run cmake + `run_tests.py` on the host — much faster
- Use act only for first-time / pre-merge final verification

### Q: How do I run just one unit test?
```bash
cmake --build build --parallel 4
./build/bin/quicx_utest --gtest_filter='*YourTest*' --gtest_color=yes
```

### Q: Some files show 100% uncovered in the coverage report?
Usually CMake's `collect_sources` gathered source files referenced by no test. Running `genhtml --ignore-errors source,unmapped` already ignores such errors; to raise coverage, write unit tests.

### Q: What can only be verified by pushing?
Only two things:
1. Pushing the `ghcr.io/quicx/quicx-interop` image (needs `GITHUB_TOKEN`)
2. Filing a PR to [quic-interop/quic-interop-runner](https://github.com/quic-interop/quic-interop-runner) to get on the leaderboard

Everything else can close the loop locally.
