import subprocess
import time
import os
import signal
import sys
import argparse

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
    # Start server in background
    server_process = subprocess.Popen([server_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    
    try:
        # Give server time to start
        time.sleep(1)

        print(f"Starting client: {client_path}")
        
        # Run client and capture output
        # Note: concurrent_server listens on port 7003
        client_process = subprocess.run(
            [client_path, "https://127.0.0.1:7003"], 
            capture_output=True, 
            text=True, 
            timeout=30  # Longer timeout for concurrent requests
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

        # Check that we have successful requests
        if "Successful Requests:  15" not in output and "Successful Requests:  0" in output:
            print("FAILED: No successful requests")
            return False

        # Check for reasonable speedup (should be > 1x if multiplexing works)
        import re
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
        
        # Terminate server
        if server_process.poll() is None:
            server_process.terminate()
            try:
                server_process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                server_process.kill()
        
        # Read server output
        server_stdout = server_process.stdout.read()
        print("Server output:\n" + "-"*60 + "\n" + server_stdout + "\n" + "-"*60)
        
        return True

    except subprocess.TimeoutExpired:
        print("FAILED: Client timed out")
        return False
    except Exception as e:
        print(f"FAILED: Exception occurred: {e}")
        return False
    finally:
        # Cleanup server
        if server_process.poll() is None:
            server_process.terminate()
            try:
                server_process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                server_process.kill()

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
