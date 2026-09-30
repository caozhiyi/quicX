# Sanitizer Testing with Hello World + Load Tester

This document describes using `hello_world_server` together with `load_tester` for AddressSanitizer (ASan), ThreadSanitizer (TSan) and UndefinedBehaviorSanitizer (UBSan) verification of concurrency and memory safety.

## Overview

`hello_world_server` is the project's minimal usable HTTP/3 service, listening on `0.0.0.0:7001` and answering `hello world` to `GET /hello`. Combined with `load_tester`, it can continuously saturate the request-handling path under multi-threading and high concurrency — handshake, stream creation, request parsing, response sending, stream close, connection recycling — making it ideal for a sanitizer "high-intensity regression" over the QUIC/HTTP3 stack.

| Sanitizer | Abbreviation | Detection Targets |
|---|---|---|
| AddressSanitizer | ASan | heap overflow, use-after-free, memory leaks, stack overflow |
| ThreadSanitizer | TSan | data races, deadlocks, thread-safety issues |
| UndefinedBehaviorSanitizer | UBSan | undefined behavior (integer overflow, null-pointer dereference, alignment violations, etc.) |

The reference load command:

```bash
./load_tester https://localhost:7001/hello --clients 4 --requests 5000
```

> Total requests = `clients × requests = 4 × 5000 = 20000` — enough to trigger concurrency- and lifetime-related bugs without making the TSan run too long.

---

## Method One: Build and Test

### Step 1: Build

```bash
cd /data/workspace/quicX

# ASan
cmake -S . -B build-asan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSANITIZER=asan \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_TESTING=OFF \
    -G "Unix Makefiles"
cmake --build build-asan --target hello_world_server load_tester -j$(nproc)

# TSan
cmake -S . -B build-tsan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSANITIZER=tsan \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_TESTING=OFF \
    -G "Unix Makefiles"
cmake --build build-tsan --target hello_world_server load_tester -j$(nproc)

# UBSan
cmake -S . -B build-ubsan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSANITIZER=ubsan \
    -DBUILD_EXAMPLES=ON \
    -DENABLE_TESTING=OFF \
    -G "Unix Makefiles"
cmake --build build-ubsan --target hello_world_server load_tester -j$(nproc)
```

### Step 2: Run the Tests

#### ASan

```bash
mkdir -p ./sanitizer-results-helloworld
export ASAN_OPTIONS="detect_leaks=1:halt_on_error=0:log_path=./sanitizer-results-helloworld/asan_san:print_stats=1"
export LSAN_OPTIONS="suppressions=/dev/null"

# When built with GCC, some distros need preloading
# export LD_PRELOAD=$(gcc -print-file-name=libasan.so)

./build-asan/bin/hello_world_server &
SERVER_PID=$!
sleep 3

./build-asan/bin/load_tester https://localhost:7001/hello --clients 4 --requests 5000

kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null
unset ASAN_OPTIONS LSAN_OPTIONS LD_PRELOAD
```

#### TSan

```bash
export TSAN_OPTIONS="halt_on_error=0:second_deadlock_stack=1:log_path=./sanitizer-results-helloworld/tsan_san:history_size=7"

# export LD_PRELOAD=$(gcc -print-file-name=libtsan.so)

./build-tsan/bin/hello_world_server &
SERVER_PID=$!
sleep 3

./build-tsan/bin/load_tester https://localhost:7001/hello --clients 4 --requests 5000

kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null
unset TSAN_OPTIONS LD_PRELOAD
```

#### UBSan

```bash
export UBSAN_OPTIONS="halt_on_error=0:print_stacktrace=1:log_path=./sanitizer-results-helloworld/ubsan_san"

./build-ubsan/bin/hello_world_server &
SERVER_PID=$!
sleep 3

./build-ubsan/bin/load_tester https://localhost:7001/hello --clients 4 --requests 5000

kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null
unset UBSAN_OPTIONS
```

### Step 3: Analyze the Reports

```bash
ls -la sanitizer-results-helloworld/

cat sanitizer-results-helloworld/asan_san.*   2>/dev/null
cat sanitizer-results-helloworld/tsan_san.*   2>/dev/null
cat sanitizer-results-helloworld/ubsan_san.*  2>/dev/null
```

No `*_san.*` file = that sanitizer found nothing.

---

## About the `--clients` Ceiling

`load_tester` internally caps `--clients` at the core count (each client spins up an independent master EventLoop thread); beyond `hardware_concurrency()` it auto-scales down with a notice. If you really want to stress the scheduling behavior, add `--force`:

```bash
./load_tester https://localhost:7001/hello --clients 32 --requests 1000 --force
```

> Over-subscribing under sanitizer mode is not advisable: TSan already slows every memory access 5–15×, and CPU preemption drags handshake RTT to seconds, easily triggering false positives via `connection_timeout_ms_`. **Keeping the example command's 4 × 5000 is a safe starting point.**

---

## Troubleshooting

### Q: The server fails to start or the port is taken

```bash
ss -ulnp | grep 7001
# or switch ports (argv[1]; update the client-side URL accordingly)
./build-tsan/bin/hello_world_server 7011 &
./build-tsan/bin/load_tester https://localhost:7011/hello --clients 4 --requests 5000
```

`hello_world_server` listens on 7001 by default, overridable via the first command-line argument or the `QUICX_HELLO_WORLD_PORT` environment variable.

### Q: TSan reports a data race, but the stack is inside a third-party library

- First confirm `LD_PRELOAD` correctly points at `libtsan.so` (common with GCC builds)
- If a third-party library (BoringSSL) wasn't rebuilt with the sanitizer, some stack frames lack symbols, but the race report itself remains valid

### Q: ASan reports leaks, all from the server exit path

QUIC server `Stop()` → `Destroy()` involves extensive asynchronous resource recycling; the server must be given **enough time** to flush. After starting the server, `sleep 3` before load testing; after `kill -TERM`, always `wait`.

### Q: UBSan reports signed integer overflow

QUIC-protocol RTT / congestion-window / packet-number computation is extremely dense. Recommendations:

- Replace `int64_t` with `uint64_t`
- Use `std::chrono::duration` for time differences instead of raw integers

### Q: load_tester shows many timeouts

- Check whether `--timeout` is too small (the 10s default is usually enough under sanitizers)
- Check `/tmp/h3_server_logs`, `/tmp/h3_client_logs` for handshake failures, flow-control blocking
- Reduce `--clients` or `--requests` and rerun to isolate

### Q: Can ASan and TSan be enabled together?

**No.** Their memory layouts are incompatible. UBSan can combine with ASan, but this project currently runs each separately for easier triage.

---

## Relation to Other Sanitizer Tests

| Scenario | Recommended Approach |
|---|---|
| Unit-test coverage | `sanitizer.yml` (local equivalent: `-DSANITIZER=xxx` build + `run_tests.py utest`) |
| File transfer / bulk-stream scenarios | see the manual procedure in [`sanitizer_file_transfer.md`](sanitizer_file_transfer.md) |
| **HTTP/3 high-concurrency short requests** | this document's manual procedure (`-DSANITIZER=xxx` build + hello_world_server + load_tester) |
| Full regression before a release | one round of all three scenario types |

`hello_world + load_tester` and `file_transfer` complement each other: the former stresses **short requests × high concurrency** (connection/stream lifecycles), the latter **long streams × bulk data** (flow control / reassembly / copy paths). Both should be part of regression.

---

## Summary

```bash
# Most-used commands: build one round each of asan / tsan / ubsan and load-test

# Quick check whether the current change introduced a concurrency bug (tsan)
./build-tsan/bin/hello_world_server &
SERVER_PID=$!
sleep 3
./build-tsan/bin/load_tester https://localhost:7001/hello --clients 4 --requests 5000
kill -TERM $SERVER_PID

# Custom stress
./build-tsan/bin/load_tester https://localhost:7001/hello --clients 8 --requests 10000
```

Keeping ASan / TSan / UBSan clean is the project's quality floor — any report anywhere along the hello_world + load_tester chain means the QUIC stack carries a latent hazard in the most common "short request × many clients" scenario and must be fixed.