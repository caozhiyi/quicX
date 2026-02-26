
import os
import sys
import subprocess
import argparse
import time
import concurrent.futures

# Configuration
BUILD_DIR = os.path.abspath("build")
BIN_DIR = os.path.join(BUILD_DIR, "bin")
EXAMPLE_DIR = os.path.abspath("example")

def run_command(cmd, cwd=None, timeout=None, capture=False):
    """Run a command and return result structure."""
    start_time = time.time()
    try:
        # If not in capture mode, let output print directly to terminal
        if not capture:
            print(f"Running: {' '.join(cmd)}")
            result = subprocess.run(cmd, cwd=cwd, timeout=timeout, capture_output=False)
            duration = time.time() - start_time
            if result.returncode == 0:
                print(f"PASS ({duration:.2f}s)")
                return True
            else:
                print(f"FAIL (Return code: {result.returncode})")
                return False
        else:
            # In concurrent mode, we must capture output to prevent interleaving
            result = subprocess.run(cmd, cwd=cwd, timeout=timeout, capture_output=True, text=True)
            duration = time.time() - start_time
            return {
                "success": result.returncode == 0,
                "returncode": result.returncode,
                "duration": duration,
                "stdout": result.stdout,
                "stderr": result.stderr
            }
    except subprocess.TimeoutExpired:
        duration = time.time() - start_time
        if capture:
            return {
                "success": False,
                "returncode": -1,
                "duration": duration,
                "stdout": "",
                "stderr": "Execution timed out"
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
    
    result = run_command(cmd, cwd=cwd, timeout=60, capture=True)
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

def run_fuzz_tests():
    print("\n" + "="*40)
    print("Running Fuzz Tests (Not Implemented)")
    print("="*40)
    return True

def run_benchmark_tests():
    print("\n" + "="*40)
    print("Running Benchmark Tests (Not Implemented)")
    print("="*40)
    return True

def run_interop_tests():
    print("\n" + "="*40)
    print("Running Interop Tests (Not Implemented)")
    print("="*40)
    return True

def main():
    parser = argparse.ArgumentParser(description="Run quicX test suite")
    parser.add_argument("mode", nargs="?", default="all", 
                        choices=["all", "utest", "example", "fuzz", "benchmark", "interop"],
                        help="Test mode to run (default: all)")
    args = parser.parse_args()

    success = True
    
    if args.mode in ["all", "utest"]:
        if not run_utest():
            success = False
            if args.mode != "all": return sys.exit(1)

    if args.mode in ["all", "example"]:
        if not run_example_tests():
            success = False
            if args.mode != "all": return sys.exit(1)

    # Future placeholders
    if args.mode in ["all", "fuzz"]:
        run_fuzz_tests()
        
    if args.mode in ["all", "benchmark"]:
        run_benchmark_tests()
        
    if args.mode in ["all", "interop"]:
        run_interop_tests()

    if not success:
        print("\nOVERALL STATUS: FAILED")
        sys.exit(1)
    else:
        print("\nOVERALL STATUS: SUCCESS")
        sys.exit(0)

if __name__ == "__main__":
    main()
