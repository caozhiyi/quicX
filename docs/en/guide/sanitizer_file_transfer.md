# Sanitizer Testing with File Transfer

This document describes using the `file_transfer` example program with AddressSanitizer (ASan), ThreadSanitizer (TSan) and UndefinedBehaviorSanitizer (UBSan) to verify memory safety and concurrency correctness.

## Overview

`file_transfer` is a file-transfer example built on the HTTP/3 streaming API, exercising key paths such as multi-threaded EventLoops, network I/O, and memory-pool allocation — an ideal integration-test vehicle for detecting latent problems in the QUIC protocol stack.

| Sanitizer | Abbreviation | Detection Targets |
|---|---|---|
| AddressSanitizer | ASan | heap overflow, use-after-free, memory leaks, stack overflow |
| ThreadSanitizer | TSan | data races, deadlocks, thread-safety issues |
| UndefinedBehaviorSanitizer | UBSan | undefined behavior (integer overflow, null-pointer dereference, alignment violations, etc.) |

---

## Method One: Build and Test

### Step 1: Build

```bash
cd /data/workspace/quicX

# ASan build
cmake -S . -B build-asan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSANITIZER=asan \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_TESTING=OFF \
    -G "Unix Makefiles"
cmake --build build-asan --target file_transfer_server file_transfer_client -j$(nproc)

# TSan build
cmake -S . -B build-tsan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSANITIZER=tsan \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_TESTING=OFF \
    -G "Unix Makefiles"
cmake --build build-tsan --target file_transfer_server file_transfer_client -j$(nproc)

# UBSan build
cmake -S . -B build-ubsan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSANITIZER=ubsan \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_TESTING=OFF \
    -G "Unix Makefiles"
cmake --build build-ubsan --target file_transfer_server file_transfer_client -j$(nproc)
```

### Step 2: Prepare Test Data

```bash
# Create the test-file directories
mkdir -p /tmp/ft_test_data /tmp/ft_upload

# Generate test files
dd if=/dev/urandom of=/tmp/ft_test_data/large.bin bs=1M count=10 2>/dev/null
echo "hello sanitizer test $(date)" > /tmp/ft_test_data/small.txt
```

### Step 3: Run the Tests

#### ASan test

```bash
# Set ASan environment variables
export ASAN_OPTIONS="detect_leaks=1:halt_on_error=0:log_path=./sanitizer-results/asan_san:print_stats=1"
export LSAN_OPTIONS="suppressions=/dev/null"

# With a GCC build, LD_PRELOAD may be needed
# export LD_PRELOAD=$(gcc -print-file-name=libasan.so)

# Start the server
./build-asan/bin/file_transfer_server /tmp/ft_upload &
SERVER_PID=$!
sleep 3

# Upload test
./build-asan/bin/file_transfer_client upload "https://127.0.0.1:7006/upload/small.txt" /tmp/ft_test_data/small.txt
./build-asan/bin/file_transfer_client upload "https://127.0.0.1:7006/upload/large.bin" /tmp/ft_test_data/large.bin

# Download test
./build-asan/bin/file_transfer_client download "https://127.0.0.1:7006/large.bin" /tmp/ft_test_data/downloaded.bin

# Stop the server
kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null
unset ASAN_OPTIONS LSAN_OPTIONS LD_PRELOAD
```

#### TSan test

```bash
export TSAN_OPTIONS="halt_on_error=0:second_deadlock_stack=1:log_path=./sanitizer-results/tsan_san:history_size=7"

# With a GCC build, LD_PRELOAD may be needed
# export LD_PRELOAD=$(gcc -print-file-name=libtsan.so)

./build-tsan/bin/file_transfer_server /tmp/ft_upload &
SERVER_PID=$!
sleep 3

./build-tsan/bin/file_transfer_client upload "https://127.0.0.1:7006/upload/small.txt" /tmp/ft_test_data/small.txt
./build-tsan/bin/file_transfer_client upload "https://127.0.0.1:7006/upload/large.bin" /tmp/ft_test_data/large.bin
./build-tsan/bin/file_transfer_client download "https://127.0.0.1:7006/large.bin" /tmp/ft_test_data/downloaded.bin

kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null
unset TSAN_OPTIONS LD_PRELOAD
```

#### UBSan test

```bash
export UBSAN_OPTIONS="halt_on_error=0:print_stacktrace=1:log_path=./sanitizer-results/ubsan_san"

./build-ubsan/bin/file_transfer_server /tmp/ft_upload &
SERVER_PID=$!
sleep 3

./build-ubsan/bin/file_transfer_client upload "https://127.0.0.1:7006/upload/small.txt" /tmp/ft_test_data/small.txt
./build-ubsan/bin/file_transfer_client upload "https://127.0.0.1:7006/upload/large.bin" /tmp/ft_test_data/large.bin
./build-ubsan/bin/file_transfer_client download "https://127.0.0.1:7006/large.bin" /tmp/ft_test_data/downloaded.bin

kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null
unset UBSAN_OPTIONS
```

### Step 4: Analyze the Reports

```bash
# Check whether sanitizer report files were generated
ls -la sanitizer-results/

# Existing report files mean problems were detected
cat sanitizer-results/asan_san.*   2>/dev/null  # ASan issues
cat sanitizer-results/tsan_san.*   2>/dev/null  # TSan issues
cat sanitizer-results/ubsan_san.*  2>/dev/null  # UBSan issues

# No report file = that sanitizer found nothing ✓
```

---

## Method Two: Using Together with CI

CI's `sanitizer.yml` builds with clang + sanitizer flags (equivalent to `-DSANITIZER={asan,ubsan,tsan}`) on every push/PR and daily schedule, running the unit tests (`run_tests.py utest`), without file_transfer. Local reproduction:

```bash
# Run sanitizer + unit tests
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DSANITIZER=tsan -DBUILD_EXAMPLES=ON
cmake --build build-tsan -j$(nproc)
rm -rf build && ln -s build-tsan build
TSAN_OPTIONS=halt_on_error=1 python3 run_tests.py utest
```

The file_transfer integration test runs separately via the manual procedure above, as a supplement to the unit tests.

---

## Sanitizer Runtime Options Reference

### ASan (ASAN_OPTIONS)

| Option | Recommended | Description |
|---|---|---|
| `detect_leaks` | `1` | enable memory-leak detection |
| `halt_on_error` | `0` | keep running after a finding (collect more information) |
| `log_path` | `path prefix` | write reports to files (PID suffix appended automatically) |
| `print_stats` | `1` | print memory statistics at exit |
| `check_initialization_order` | `1` | check global-variable initialization order |

### TSan (TSAN_OPTIONS)

| Option | Recommended | Description |
|---|---|---|
| `halt_on_error` | `0` | keep running after a race |
| `second_deadlock_stack` | `1` | print the second thread's stack in deadlock reports |
| `log_path` | `path prefix` | write reports to files |
| `history_size` | `4`~`7` | memory-access history depth (higher = more precise, but slower and more memory) |

### UBSan (UBSAN_OPTIONS)

| Option | Recommended | Description |
|---|---|---|
| `halt_on_error` | `0` | keep running after UB |
| `print_stacktrace` | `1` | print the full call stack |
| `log_path` | `path prefix` | write reports to files |

---

## Troubleshooting

### Q: ASan reports `LeakSanitizer: detected memory leaks`

Usually a genuine leak. Inspect the report's call stack to locate allocated-but-unreleased objects. Common causes:
- `shared_ptr` reference cycles
- forgetting to release manually allocated resources in a destructor
- early returns on exception paths skipping cleanup

### Q: TSan reports `data race`

Typical scenarios:
- cross-thread access to unlocked member variables
- the EventLoop being used by another thread before initialization finished (fixed via `WaitUntilReady()`)
- calling non-async-signal-safe functions inside signal handlers (fixed)

Fix: make sure shared data is protected by `mutex`, `atomic`, or a thread-safe queue.

### Q: UBSan reports `runtime error: signed integer overflow`

The QUIC protocol involves heavy computation (RTT, congestion window, timestamps). Watch out for:
- using `uint64_t` instead of `int64_t` to avoid signed overflow
- computing time differences with `std::chrono::duration` rather than raw integers

### Q: BoringSSL emits lots of warnings during the build

Expected behavior. CMakeLists.txt already disables `-Werror` for third-party targets; sanitizer detection is unaffected.

### Q: Can TSan and ASan be used together?

**No.** ASan and TSan have incompatible memory layouts; they must be built and tested separately. UBSan can be enabled together with ASan (this project doesn't currently combine them; separate runs are recommended for easier triage).

### Q: The server fails to start during a test?

1. Check whether port 7006 is taken: `ss -ulnp | grep 7006`
2. Confirm the TLS certificate files are accessible (the server uses a built-in test certificate by default)
3. Check the build output to confirm compilation succeeded (rerun `cmake --build` and look for errors)

### Q: How do I enlarge test files or concurrent connections?

In manual mode, adjust freely:

```bash
# Generate a 100MB test file
dd if=/dev/urandom of=/tmp/ft_test_data/huge.bin bs=1M count=100

# Run multiple concurrent clients (more likely to trigger races)
for i in $(seq 1 5); do
    ./build-tsan/bin/file_transfer_client upload \
        "https://127.0.0.1:7006/upload/file_${i}.bin" \
        /tmp/ft_test_data/large.bin &
done
wait
```

---

## Summary

| Need | Recommended Approach |
|---|---|
| Quick verification in daily development | manual tsan build + upload/download test |
| Full check before a release | one manual round each of ASan / TSan / UBSan |
| Debugging a specific race scenario | manual build + tuned env vars + concurrent clients |
| CI integration | `sanitizer.yml` (unit tests) + the manual file_transfer procedure |

Keeping **ASan / TSan / UBSan clean** is this project's quality floor — run at least one round of `tsan` before every commit (multithreading is where bugs breed most easily), and all three must be green before a release.