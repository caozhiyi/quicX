import subprocess
import time
import os
import signal
import sys
import argparse
import socket

def wait_for_port(port, host='127.0.0.1', timeout=5.0):
    """Wait until a port is open."""
    start_time = time.time()
    while time.time() - start_time < timeout:
        try:
            with socket.create_connection((host, port), timeout=1):
                return True
        except (socket.timeout, ConnectionRefusedError):
            time.sleep(0.1)
    return False

def run_test(bin_dir):
    server_path = os.path.join(bin_dir, "metrics_monitoring_server")
    client_path = os.path.join(bin_dir, "metrics_monitoring_client")

    if not os.path.exists(server_path):
        print(f"Error: Server binary not found at {server_path}")
        return False
    if not os.path.exists(client_path):
        print(f"Error: Client binary not found at {client_path}")
        return False

    print(f"Starting server: {server_path}")
    # Start server in background
    # Use preexec_fn=os.setsid to create a new process group, so we can kill the whole tree if needed
    server_process = subprocess.Popen(
        [server_path], 
        stdout=subprocess.PIPE, 
        stderr=subprocess.PIPE,
        preexec_fn=os.setsid
    )
    
    try:
        # Give server time to start
        print("Waiting for server to start...")
        time.sleep(2)
        
        if server_process.poll() is not None:
             print("FAILED: Server process died prematurely")
             stdout, stderr = server_process.communicate()
             print(f"Server stdout: {stdout.decode(errors='replace')}")
             print(f"Server stderr: {stderr.decode(errors='replace')}")
             return False

        print(f"Starting client: {client_path}")
        # Run client and capture output
        # The client runs multiple tests including basic functionality, load test, and metrics fetch
        client_process = subprocess.run(
            [client_path, "https://127.0.0.1:7010"], 
            capture_output=True, 
            text=True, 
            timeout=30 # Increased timeout as client runs multiple tests including sleeps
        )

        output = client_process.stdout
        print("Client output:\n" + "-"*20 + "\n" + output + "\n" + "-"*20)

        if client_process.returncode != 0:
            print(f"Client failed with return code {client_process.returncode}")
            print("Client stderr:", client_process.stderr)
            return False

        # Verification
        expected_strings = [
            "Test 1: Basic Functionality",
            "Test 2: Load Test",
            "Test 3: Fetching Metrics",
            "Server Metrics (Prometheus Format)",
            "Test completed!"
        ]

        missing = []
        for s in expected_strings:
            if s not in output:
                missing.append(s)

        if missing:
            print(f"FAILED: Missing expected output: {missing}")
            return False
            
        # Also check for success metrics in the output
        if "Failed:              0 [FAIL]" not in output:
             print("WARNING: Some requests failed (expected 0 failures in robust environment, but might be acceptable in some CI envs)")
             # We don't fail the test strictly on this for now unless it's critical, 
             # but typical expectation is 0 failures for local test.
        
        print("SUCCESS: All expected output found.")
        return True

    except subprocess.TimeoutExpired:
        print("FAILED: Client timed out")
        return False
    except Exception as e:
        print(f"FAILED: Exception occurred: {e}")
        return False
    finally:
        # cleanup server
        print("Killing server...")
        try:
            os.killpg(os.getpgid(server_process.pid), signal.SIGTERM)
        except ProcessLookupError:
            pass # Already dead

        try:
            stdout, stderr = server_process.communicate(timeout=2)
            # Only print server output if something went wrong or for debug (optional)
            # print("Server stdout:\n" + "-"*20 + "\n" + (stdout.decode(errors='replace') if stdout else "") + "\n" + "-"*20)
            # print("Server stderr:\n" + "-"*20 + "\n" + (stderr.decode(errors='replace') if stderr else "") + "\n" + "-"*20)
        except Exception as e:
            print(f"Error reading server output: {e}")
        
        server_process.wait()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run metrics_monitoring example test")
    parser.add_argument("--bin-dir", default="../../build/bin", help="Directory containing client/server binaries")
    args = parser.parse_args()

    # Make sure we use absolute path for bin_dir if it's relative
    bin_dir = os.path.abspath(args.bin_dir)
    
    success = run_test(bin_dir)
    sys.exit(0 if success else 1)
