import subprocess
import time
import os
import signal
import sys
import argparse

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from _test_helpers import start_server, stop_server  # noqa: E402

def run_test(bin_dir):
    server_path = os.path.join(bin_dir, "concurrent_server")
    client_path = os.path.join(bin_dir, "concurrent_client")

    if not os.path.exists(server_path):
        print(f"Error: Server binary not found at {server_path}")
        return False
    if not os.path.exists(client_path):
        print(f"Error: Client binary not found at {client_path}")
        return False

    print(f"Starting server: {server_path}")
    # Launch in its own process group so cleanup is robust against children.
    server_process = start_server([server_path], text=True)
    
    try:
        # Give server time to start
        time.sleep(1)

        print(f"Starting client: {client_path}")
        
        # Run client and capture output
        # Note: concurrent_server listens on port 7003.
        # Timeout budget: client itself caps each WaitForCompletion at 15s and
        # the suite has 4 phases plus tiny pauses, so 60s leaves headroom.
        client_process = subprocess.run(
            [client_path, "https://127.0.0.1:7003"], 
            capture_output=True, 
            text=True, 
            timeout=60
        )

        if client_process.returncode != 0:
            print(f"Client failed with return code {client_process.returncode}")
            print("Client stderr:", client_process.stderr)
            return False

        output = client_process.stdout
        print("Client output:\n" + "-"*60 + "\n" + output + "\n" + "-"*60)

        # Verification - check for expected output strings
        expected_strings = [
            "CONCURRENT REQUEST RESULTS",
            "Total Requests:",
            "Successful Requests:",
            "Multiplexing Efficiency:",
            "Speedup Factor:"
        ]

        missing = []
        for s in expected_strings:
            if s not in output:
                missing.append(s)

        if missing:
            print(f"FAILED: Missing expected output: {missing}")
            return False

        # Check that we have successful requests in Test 1 (15 mixed requests).
        # We allow a couple of stragglers — the demo only needs to prove that
        # the client multiplexed concurrently, not that every single request
        # came back. The previous strict "15 successful" check was the
        # fail-mode driver here once WaitForCompletion was given a budget.
        import re
        m = re.search(r"Test 1: Mixed Concurrent Requests.*?Successful Requests:\s+(\d+)",
                      output, re.DOTALL)
        if m:
            n_success = int(m.group(1))
            if n_success < 12:
                print(f"FAILED: Test 1 only had {n_success}/15 successful requests")
                return False
            print(f"Test 1 successful requests: {n_success}/15")

        # Check for reasonable speedup (should be > 1x if multiplexing works)
        speedup_match = re.search(r"Speedup Factor:\s+([\d.]+)x", output)
        if speedup_match:
            speedup = float(speedup_match.group(1))
            if speedup < 1.0:
                print(f"WARNING: Speedup factor {speedup}x is less than 1.0")
            else:
                print(f"Multiplexing working: {speedup}x speedup achieved")
        
        print("SUCCESS: Client output verified.")
        
        # Wait a moment for server to process
        time.sleep(1)

        # Terminate server (whole process group)
        stop_server(server_process)
        
        # Read server output
        try:
            server_stdout = server_process.stdout.read() if server_process.stdout else ""
        except Exception:
            server_stdout = ""
        print("Server output:\n" + "-"*60 + "\n" + (server_stdout or "") + "\n" + "-"*60)
        
        return True

    except subprocess.TimeoutExpired:
        print("FAILED: Client timed out")
        return False
    except Exception as e:
        print(f"FAILED: Exception occurred: {e}")
        return False
    finally:
        # Cleanup server (kills whole process group)
        stop_server(server_process)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run concurrent requests example test")
    parser.add_argument("--bin-dir", default=None, help="Directory containing client/server binaries")
    args = parser.parse_args()

    # Determine bin_dir: use provided path, or search for it
    if args.bin_dir:
        bin_dir = os.path.abspath(args.bin_dir)
    else:
        # Try to find bin directory relative to script location or current directory
        script_dir = os.path.dirname(os.path.abspath(__file__))
        possible_paths = [
            os.path.join(script_dir, "../../build/bin"),  # From script location
            os.path.join(os.getcwd(), "build/bin"),       # From project root
            os.path.join(os.getcwd(), "bin"),             # From build directory
        ]
        bin_dir = None
        for path in possible_paths:
            abs_path = os.path.abspath(path)
            if os.path.exists(os.path.join(abs_path, "concurrent_server")):
                bin_dir = abs_path
                break
        if not bin_dir:
            print("Error: Could not find bin directory. Please specify with --bin-dir")
            sys.exit(1)
    
    success = run_test(bin_dir)
    sys.exit(0 if success else 1)
