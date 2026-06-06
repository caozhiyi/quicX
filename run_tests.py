
import os
import sys
import signal
import subprocess
import argparse
import time
import concurrent.futures

# Configuration
BUILD_DIR = os.path.abspath("build")
BIN_DIR = os.path.join(BUILD_DIR, "bin")
EXAMPLE_DIR = os.path.abspath("example")

# Per-example outer wall-clock timeout, in seconds.
#
# The default budget (60s) is generous for the small "demo" examples (hello_world,
# bidirectional_comm, error_handling, ...) where a client makes a handful of
# requests against a local server. A few examples intentionally do real work
# and need a substantially larger envelope; they each manage their *own*
# inner timeout (subprocess.run timeout=...) for the client and/or server,
# but the outer runner here must give them headroom or it will SIGKILL the
# whole process group prematurely.
#
# Keep this list small and only override when an example genuinely exceeds the
# default. If you're tempted to bump every entry, fix the underlying example
# instead.
EXAMPLE_DEFAULT_TIMEOUT = 60
EXAMPLE_TIMEOUTS = {
    # Generates a 50 MB file, uploads it, downloads it, MD5-verifies both.
    # The script's own per-direction timeout is 120s (upload) + 120s (download),
    # plus file I/O and a 2s server-warmup sleep, so we budget 5 minutes.
    "file_transfer": 300,
    # Runs four sequential phases of multiplexed requests against a local
    # server; each phase caps WaitForCompletion at ~15s, and the script's own
    # client timeout is already 60s. Give the outer runner extra slack so a
    # slightly-loaded CI box doesn't trip the SIGKILL just as the client is
    # printing its summary.
    "concurrent_requests": 120,
}

def _kill_process_group(proc):
    """Best-effort: kill the entire process group of `proc`.

    The child was launched with start_new_session=True so it is the leader of
    its own process group. Killing the group ensures any grandchildren (e.g.
    C++ servers spawned by the test script) are reaped too — otherwise they
    keep holding UDP ports and break subsequent runs with "bind: address in
    use" / "start server failed".
    """
    if proc.poll() is not None:
        return
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
    except (ProcessLookupError, PermissionError, OSError):
        try:
            proc.kill()
        except Exception:
            pass

def _run_with_group_timeout(cmd, cwd, timeout, capture):
    """Run `cmd` in its own process group; on timeout, kill the whole group.

    Returns (returncode, stdout_str, stderr_str). When capture is False, the
    child's stdout/stderr are inherited (printed directly), and the returned
    stdout/stderr are empty strings.
    """
    stdout_pipe = subprocess.PIPE if capture else None
    stderr_pipe = subprocess.PIPE if capture else None

    proc = subprocess.Popen(
        cmd,
        cwd=cwd,
        stdout=stdout_pipe,
        stderr=stderr_pipe,
        text=True if capture else False,
        start_new_session=True,  # new process group / session
    )

    try:
        stdout, stderr = proc.communicate(timeout=timeout)
        return proc.returncode, (stdout or "" if capture else ""), (stderr or "" if capture else "")
    except subprocess.TimeoutExpired:
        # Kill the whole group so grandchildren (servers) die too.
        _kill_process_group(proc)
        try:
            stdout, stderr = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            stdout, stderr = "", ""
        # Surface a clear marker; callers turn this into a failure result.
        raise subprocess.TimeoutExpired(cmd, timeout,
                                        output=(stdout or "") if capture else None,
                                        stderr=(stderr or "") if capture else None)


def run_command(cmd, cwd=None, timeout=None, capture=False):
    """Run a command and return result structure."""
    start_time = time.time()
    try:
        if not capture:
            print(f"Running: {' '.join(cmd)}")
            rc, _, _ = _run_with_group_timeout(cmd, cwd, timeout, capture=False)
            duration = time.time() - start_time
            if rc == 0:
                print(f"PASS ({duration:.2f}s)")
                return True
            else:
                print(f"FAIL (Return code: {rc})")
                return False
        else:
            # In concurrent mode, we must capture output to prevent interleaving
            rc, stdout, stderr = _run_with_group_timeout(cmd, cwd, timeout, capture=True)
            duration = time.time() - start_time
            return {
                "success": rc == 0,
                "returncode": rc,
                "duration": duration,
                "stdout": stdout,
                "stderr": stderr,
            }
    except subprocess.TimeoutExpired as e:
        duration = time.time() - start_time
        if capture:
            return {
                "success": False,
                "returncode": -1,
                "duration": duration,
                "stdout": getattr(e, "output", "") or "",
                "stderr": (getattr(e, "stderr", "") or "") + "\nExecution timed out (process group killed)",
            }
        print("FAIL (Timeout)")
        return False
    except Exception as e:
        duration = time.time() - start_time
        if capture:
            return {
                "success": False,
                "returncode": -1,
                "duration": duration,
                "stdout": "",
                "stderr": f"Exception: {e}"
            }
        print(f"FAIL (Exception: {e})")
        return False

def run_utest():
    """Run unit tests."""
    print("\n" + "="*40)
    print("Running Unit Tests")
    print("="*40)
    
    utest_bin = os.path.join(BIN_DIR, "quicx_utest")
    if not os.path.exists(utest_bin):
        # fallback path
        utest_bin = os.path.join(BUILD_DIR, "test", "quicx_utest")
        
    if not os.path.exists(utest_bin):
        print(f"Error: Unit test binary not found at {utest_bin}")
        print("Did you build the project? (mkdir build && cd build && cmake .. && make)")
        return False

    return run_command([utest_bin])

def run_single_example_test(script_path):
    """Run a single example test script."""
    example_name = os.path.basename(os.path.dirname(script_path))
    cmd = [sys.executable, script_path, "--bin-dir", BIN_DIR]
    cwd = os.path.dirname(script_path)

    timeout = EXAMPLE_TIMEOUTS.get(example_name, EXAMPLE_DEFAULT_TIMEOUT)
    result = run_command(cmd, cwd=cwd, timeout=timeout, capture=True)
    result["name"] = example_name
    return result

def run_example_tests():
    """Run tests for examples concurrently."""
    print("\n" + "="*40)
    print("Running Example Tests")
    print("="*40)
    
    # search for run_test.py
    test_scripts = []
    for root, dirs, files in os.walk(EXAMPLE_DIR):
        if "run_test.py" in files:
            test_scripts.append(os.path.join(root, "run_test.py"))
    
    if not test_scripts:
        print("No example test scripts found.")
        return True

    print(f"Found {len(test_scripts)} example tests, starting concurrent execution...\n")
    test_scripts.sort() 

    passed_tests = []
    failed_tests = []
    
    # Use ThreadPoolExecutor for concurrency
    # Since these exist processes (integration tests), threads are fine to wait for them.
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(len(test_scripts), 10)) as executor:
        future_to_script = {executor.submit(run_single_example_test, script): script for script in test_scripts}
        
        for future in concurrent.futures.as_completed(future_to_script):
            result = future.result()
            name = result["name"]
            duration = result["duration"]
            
            if result["success"]:
                print(f"[PASS] {name:<25} Duration: {duration:.2f}s")
                passed_tests.append(result)
            else:
                print(f"[FAIL] {name:<25} Duration: {duration:.2f}s (Code: {result['returncode']})")
                failed_tests.append(result)

    print("\n" + "-"*40)
    print("Example Test Summary")
    print("-" * 40)
    print(f"Total: {len(test_scripts)}, Passed: {len(passed_tests)}, Failed: {len(failed_tests)}")
    
    if failed_tests:
        print("\n=== Failure Details ===")
        for ft in failed_tests:
            print(f"\nExample: {ft['name']}")
            print(f"Return Code: {ft['returncode']}")
            print(f"Stderr:\n{ft['stderr'].strip()}")
            # Print stdout tail if needed, usually useful for debugging
            if ft['stdout']:
                print(f"Stdout (last 10 lines):\n" + "\n".join(ft['stdout'].strip().splitlines()[-10:]))
            print("-" * 20)
        return False
    
    return True

INTEGRATION_TESTS = [
    "http3_methods_test",
    "connection_management_test",
    "error_handling_test",
    "stress_test",
    "advanced_features_test",
    "streaming_and_push_test",
]

def run_single_integration_test(test_name):
    """Run a single integration test binary."""
    test_bin = os.path.join(BIN_DIR, test_name)
    if not os.path.exists(test_bin):
        return {
            "success": False,
            "returncode": -1,
            "duration": 0,
            "stdout": "",
            "stderr": f"Binary not found: {test_bin}",
            "name": test_name,
        }

    result = run_command([test_bin], timeout=300, capture=True)
    result["name"] = test_name
    return result

def run_integration_tests():
    """Run integration tests sequentially.

    Integration tests each spin up full HTTP/3 servers and tens of clients
    (stress_test fires up 50 concurrent clients at once). Running several of
    them in parallel caused transient SIGSEGVs under resource pressure, so we
    execute them one by one; total wall-clock cost is still around 60s.
    """
    print("\n" + "="*40)
    print("Running Integration Tests")
    print("="*40)

    print(f"Found {len(INTEGRATION_TESTS)} integration tests, running sequentially...\n")

    passed_tests = []
    failed_tests = []

    for name in INTEGRATION_TESTS:
        result = run_single_integration_test(name)
        duration = result["duration"]

        if result["success"]:
            print(f"[PASS] {name:<35} Duration: {duration:.2f}s")
            passed_tests.append(result)
        else:
            print(f"[FAIL] {name:<35} Duration: {duration:.2f}s (Code: {result['returncode']})")
            failed_tests.append(result)

    print("\n" + "-"*40)
    print("Integration Test Summary")
    print("-" * 40)
    print(f"Total: {len(INTEGRATION_TESTS)}, Passed: {len(passed_tests)}, Failed: {len(failed_tests)}")

    if failed_tests:
        print("\n=== Failure Details ===")
        for ft in failed_tests:
            print(f"\nTest: {ft['name']}")
            print(f"Return Code: {ft['returncode']}")
            print(f"Stderr:\n{ft['stderr'].strip()}")
            if ft['stdout']:
                print(f"Stdout (last 20 lines):\n" + "\n".join(ft['stdout'].strip().splitlines()[-20:]))
            print("-" * 20)
        return False

    return True

# --- Generic helper: run a batch of binaries with pass/fail reporting --------
def _run_binary_batch(title, binaries, binary_resolver, timeout, summary_label,
                      skipped=None):
    """Run a list of binaries sequentially and report per-binary results.

    binaries         : iterable of logical names
    binary_resolver  : callable(name) -> (full_path, argv_list)
    timeout          : per-binary wall-clock timeout in seconds
    summary_label    : string used in the summary header (e.g. "Benchmark")
    skipped          : optional {name: reason} dict printed at the top
    Returns True iff every executed binary exited 0.
    """
    print("\n" + "=" * 40)
    print(f"Running {title}")
    print("=" * 40)

    binaries = list(binaries)
    print(f"Found {len(binaries)} {summary_label.lower()} tests, running sequentially...")
    if skipped:
        print("Skipped (known issues):")
        for name, reason in skipped.items():
            print(f"  - {name}: {reason}")
    print()

    passed, failed = [], []
    for name in binaries:
        bin_path, argv = binary_resolver(name)
        if not os.path.exists(bin_path):
            result = {
                "success": False,
                "returncode": -1,
                "duration": 0,
                "stdout": "",
                "stderr": f"Binary not found: {bin_path}",
            }
        else:
            result = run_command(argv, timeout=timeout, capture=True)
        result["name"] = name
        duration = result["duration"]
        if result["success"]:
            print(f"[PASS] {name:<35} Duration: {duration:.2f}s")
            passed.append(result)
        else:
            print(f"[FAIL] {name:<35} Duration: {duration:.2f}s (Code: {result['returncode']})")
            failed.append(result)

    print("\n" + "-" * 40)
    print(f"{summary_label} Test Summary")
    print("-" * 40)
    skipped_n = len(skipped) if skipped else 0
    print(f"Total: {len(binaries)}, Passed: {len(passed)}, Failed: {len(failed)}, Skipped: {skipped_n}")

    if failed:
        print("\n=== Failure Details ===")
        for ft in failed:
            print(f"\nTest: {ft['name']}")
            print(f"Return Code: {ft['returncode']}")
            print(f"Stderr:\n{ft['stderr'].strip()}")
            if ft['stdout']:
                print("Stdout (last 20 lines):\n" + "\n".join(ft['stdout'].strip().splitlines()[-20:]))
            print("-" * 20)
        return False
    return True


# --- Fuzz tests ---------------------------------------------------------------
# Fuzzers live in ${BUILD_DIR}/bin/fuzz/* and are libFuzzer harnesses. libFuzzer
# loops forever by default, so for a regression pass we cap each target with
# -max_total_time to give it a short, deterministic budget. They are opt-in:
# configure with -DENABLE_FUZZING=ON (requires clang + libFuzzer/ASan/UBSan).
FUZZ_BIN_SUBDIR = "fuzz"
FUZZ_PER_TARGET_SECONDS = 5     # per-fuzzer budget for the smoke pass
FUZZ_MAX_LEN = 4096             # cap sample size to keep runs bounded
FUZZ_TIMEOUT = FUZZ_PER_TARGET_SECONDS + 30  # watchdog over libFuzzer itself

def _discover_fuzz_targets():
    """Return a sorted list of fuzz target executable names under build/bin/fuzz/."""
    fuzz_dir = os.path.join(BIN_DIR, FUZZ_BIN_SUBDIR)
    if not os.path.isdir(fuzz_dir):
        return []
    targets = []
    for entry in sorted(os.listdir(fuzz_dir)):
        full = os.path.join(fuzz_dir, entry)
        if os.path.isfile(full) and os.access(full, os.X_OK):
            targets.append(entry)
    return targets

def run_fuzz_tests():
    """Run every libFuzzer target for a short, bounded budget.

    Skips gracefully if the project was built without -DENABLE_FUZZING=ON, so
    this mode can safely participate in the default `all` pipeline.
    """
    print("\n" + "=" * 40)
    print("Running Fuzz Tests")
    print("=" * 40)

    fuzz_dir = os.path.join(BIN_DIR, FUZZ_BIN_SUBDIR)
    targets = _discover_fuzz_targets()
    if not targets:
        print(f"No fuzz targets found in {fuzz_dir}.")
        print("Enable with: cmake -DENABLE_FUZZING=ON .. && make  (requires clang)")
        print("Skipping fuzz stage.")
        return True  # not a failure: fuzzing is opt-in

    def resolver(name):
        path = os.path.join(fuzz_dir, name)
        # -print_final_stats=1 gives a useful coverage/exec summary in the log;
        # runs=-1 lets libFuzzer iterate until max_total_time elapses.
        argv = [
            path,
            f"-max_total_time={FUZZ_PER_TARGET_SECONDS}",
            f"-max_len={FUZZ_MAX_LEN}",
            "-print_final_stats=1",
        ]
        return path, argv

    return _run_binary_batch(
        title="Fuzz Tests",
        binaries=targets,
        binary_resolver=resolver,
        timeout=FUZZ_TIMEOUT,
        summary_label="Fuzz",
    )


# --- Benchmarks (google-benchmark suites under test/benchmarks/) --------------
# These differ from test/perf only in location and scope: micro-benchmarks that
# exercise individual library primitives (buffer, varint, qpack, ...). Like the
# perf binaries, they accept --benchmark_min_time, so we run a minimal-duration
# smoke pass here; full-precision runs should still be invoked manually.
BENCHMARK_BIN_SUBDIR = "benchmarks"
BENCHMARK_TESTS = [
    "buffer_bench",
    "congestion_bench",
    "http3_e2e_bench",
    "memorypool_bench",
    "metrics_bench",
    "pto_bench",
    "qlog_overhead_bench",
    "qpack_bench",
    "qpack_decode_bench",
    "qpack_huffman_bench",
    "qpack_instr_bench",
    "quic_ack_maxdata_bench",
    "quic_aead_bench",
    "quic_frame_bench",
    "timer_bench",
    "varint_bench",
]
BENCHMARK_TEST_TIMEOUT = 180

def run_benchmark_tests():
    """Run micro-benchmark smoke tests sequentially.

    Requires -DENABLE_BENCHMARKS=ON (default). Binaries live in
    ${BUILD_DIR}/bin/benchmarks/.
    """
    bench_dir = os.path.join(BIN_DIR, BENCHMARK_BIN_SUBDIR)
    if not os.path.isdir(bench_dir):
        print("\n" + "=" * 40)
        print("Running Benchmark Tests")
        print("=" * 40)
        print(f"Error: Benchmark directory not found: {bench_dir}")
        print("Build with: cmake -DENABLE_BENCHMARKS=ON .. && make")
        return False

    def resolver(name):
        path = os.path.join(bench_dir, name)
        argv = [path, "--benchmark_min_time=0.01s", "--benchmark_color=false"]
        return path, argv

    return _run_binary_batch(
        title="Benchmark Tests",
        binaries=BENCHMARK_TESTS,
        binary_resolver=resolver,
        timeout=BENCHMARK_TEST_TIMEOUT,
        summary_label="Benchmark",
    )


# --- Congestion-control simulator tests (gtest) ------------------------------
# Single gtest binary at ${BUILD_DIR}/bin/test/cc_test, built when
# -DENABLE_CC_SIMULATOR=ON (default). Covers a matrix of CC algorithms against
# a packet-level network simulator; the suite can take ~1-2 minutes.
CC_TEST_BINARY = os.path.join(BIN_DIR, "test", "cc_test")
CC_TEST_TIMEOUT = 600  # the full suite (~56 tests) takes ~70s on dev boxes

def run_cc_tests():
    """Run the congestion-control simulator gtest suite."""
    print("\n" + "=" * 40)
    print("Running Congestion Control Tests")
    print("=" * 40)

    if not os.path.exists(CC_TEST_BINARY):
        print(f"Error: cc_test binary not found at {CC_TEST_BINARY}")
        print("Build with: cmake -DENABLE_CC_SIMULATOR=ON .. && make cc_test")
        return False

    # Stream output directly: single long-running binary, visible progress is
    # more useful than a buffered dump at the end.
    return run_command([CC_TEST_BINARY, "--gtest_color=no"], timeout=CC_TEST_TIMEOUT)

# --- Performance / micro-benchmark tests --------------------------------------
# All perf binaries live in ${BUILD_DIR}/bin/perf/*_perf_test (plus a couple of
# non-benchmark *_test helpers). They are google-benchmark based, so they accept
# --benchmark_min_time to trim per-benchmark wall time. We run them here purely
# as a smoke / regression pass: every binary must exit 0. Full-precision timing
# runs should still be executed separately.
PERF_BIN_SUBDIR = "perf"

# Binaries we actively run. Order is arbitrary - executed sequentially to avoid
# CPU contention that would skew any timing shown in the output.
PERF_TESTS = [
    "crypto_perf_test",
    "packet_perf_test",
    "frame_perf_test",
    "qpack_perf_test",
    "congestion_control_perf_test",
    "loss_recovery_perf_test",
    "memory_baseline_test",
    "memory_pool_efficiency_test",
    "cpu_hotspot_test",
    "e2e_perf_test",
]

# Known-issue skip list for perf tests. Keep the map (even when empty) so we
# have a single place to annotate flakiness; the summary line always prints
# the skipped count for visibility.
PERF_TESTS_SKIPPED = {}

# Per-test timeout. e2e_perf_test runs a sustained-load scenario (~30s) even
# at the lowest --benchmark_min_time, so give it generous headroom.
PERF_TEST_TIMEOUT = 180

def run_perf_tests():
    """Run performance micro-benchmark smoke tests sequentially.

    Perf binaries are google-benchmark executables; we just verify each one
    exits successfully with a minimal per-benchmark duration. They are run
    sequentially to avoid contention between the multi-threaded benchmarks.
    """
    perf_dir = os.path.join(BIN_DIR, PERF_BIN_SUBDIR)
    if not os.path.isdir(perf_dir):
        print("\n" + "=" * 40)
        print("Running Performance Tests")
        print("=" * 40)
        print(f"Error: Perf binary directory not found: {perf_dir}")
        print("Build with: cmake -DENABLE_PERF_TESTS=ON .. && make")
        return False

    def resolver(name):
        path = os.path.join(perf_dir, name)
        argv = [path, "--benchmark_min_time=0.01s", "--benchmark_color=false"]
        return path, argv

    return _run_binary_batch(
        title="Performance Tests",
        binaries=PERF_TESTS,
        binary_resolver=resolver,
        timeout=PERF_TEST_TIMEOUT,
        summary_label="Performance",
        skipped=PERF_TESTS_SKIPPED,
    )

INTEROP_RUNNER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test", "interop", "interop_runner.py")
INTEROP_BUILD_DIR = os.path.abspath("build_interop")
INTEROP_DEFAULT_PORT = 4433
INTEROP_TIMEOUT = 600  # 10 minutes for all scenarios

def run_interop_tests():
    """Run QUIC interop self-tests via interop_runner.py --local."""
    print("\n" + "="*40)
    print("Running Interop Tests")
    print("="*40)

    # Check interop_runner.py exists
    if not os.path.exists(INTEROP_RUNNER):
        print(f"Error: interop_runner.py not found at {INTEROP_RUNNER}")
        return False

    # Determine build directory: prefer build_interop, fall back to build
    build_dir = INTEROP_BUILD_DIR
    if not os.path.isdir(build_dir):
        build_dir = BUILD_DIR
    
    # Verify interop binaries exist
    bin_dir = os.path.join(build_dir, "bin")
    server_bin = os.path.join(bin_dir, "interop_server")
    client_bin = os.path.join(bin_dir, "interop_client")

    if not os.path.exists(server_bin) or not os.path.exists(client_bin):
        print(f"Error: Interop binaries not found in {bin_dir}")
        print("Build with: cmake -DENABLE_INTEROP=ON .. && make interop_server interop_client")
        return False

    print(f"Build dir: {build_dir}")
    print(f"Port: {INTEROP_DEFAULT_PORT}")
    print(f"Runner: {INTEROP_RUNNER}")
    print()

    cmd = [
        sys.executable, INTEROP_RUNNER,
        "--local",
        "--build-dir", build_dir,
        "--port", str(INTEROP_DEFAULT_PORT),
        "--output", "text",
        "-v",
        "--timeout", "45",
    ]

    start_time = time.time()
    try:
        result = subprocess.run(
            cmd,
            cwd=os.path.dirname(INTEROP_RUNNER),
            timeout=INTEROP_TIMEOUT,
            capture_output=True,
            text=True,
        )
        duration = time.time() - start_time
    except subprocess.TimeoutExpired:
        duration = time.time() - start_time
        print(f"FAIL: Interop tests timed out after {duration:.1f}s")
        return False
    except Exception as e:
        print(f"FAIL: Exception running interop tests: {e}")
        return False

    # Print runner output
    if result.stdout:
        print(result.stdout)
    if result.stderr:
        print(result.stderr, file=sys.stderr)

    # Parse results from output
    passed = 0
    failed = 0
    total = 0
    for line in result.stdout.splitlines():
        # Look for summary line like "Total: 14  |  Passed: 14  |  Failed: 0  ..."
        if line.strip().startswith("Total:"):
            parts = line.split("|")
            for part in parts:
                part = part.strip()
                if part.startswith("Total:"):
                    total = int(part.split(":")[1].strip())
                elif part.startswith("Passed:"):
                    passed = int(part.split(":")[1].strip())
                elif part.startswith("Failed:"):
                    failed = int(part.split(":")[1].strip())

    if total > 0:
        print(f"\nInterop Summary: {passed}/{total} passed, {failed} failed ({duration:.1f}s)")
    else:
        print(f"\nInterop tests completed in {duration:.1f}s (could not parse summary)")

    # Fail if the runner itself returned non-zero or any test failed
    if result.returncode != 0 or failed > 0:
        return False

    return True

def main():
    parser = argparse.ArgumentParser(description="Run quicX test suite")
    parser.add_argument("mode", nargs="?", default="all", 
                        choices=["all", "utest", "example", "integration", "fuzz", "benchmark", "interop", "perf", "cc"],
                        help="Test mode to run (default: all)")
    args = parser.parse_args()

    success = True
    
    if args.mode in ["all", "utest"]:
        if not run_utest():
            success = False
            if args.mode != "all": return sys.exit(1)

    if args.mode in ["all", "integration"]:
        if not run_integration_tests():
            success = False
            if args.mode != "all": return sys.exit(1)

    if args.mode in ["all", "example"]:
        if not run_example_tests():
            success = False
            if args.mode != "all": return sys.exit(1)

    # Future placeholders
    if args.mode in ["all", "fuzz"]:
        if not run_fuzz_tests():
            success = False
            if args.mode != "all": return sys.exit(1)

    if args.mode in ["all", "benchmark"]:
        if not run_benchmark_tests():
            success = False
            if args.mode != "all": return sys.exit(1)

    if args.mode in ["all", "cc"]:
        if not run_cc_tests():
            success = False
            if args.mode != "all": return sys.exit(1)

    if args.mode in ["all", "interop"]:
        if not run_interop_tests():
            success = False
            if args.mode != "all": return sys.exit(1)

    if args.mode in ["all", "perf"]:
        if not run_perf_tests():
            success = False
            if args.mode != "all": return sys.exit(1)

    if not success:
        print("\nOVERALL STATUS: FAILED")
        sys.exit(1)
    else:
        print("\nOVERALL STATUS: SUCCESS")
        sys.exit(0)

if __name__ == "__main__":
    main()
