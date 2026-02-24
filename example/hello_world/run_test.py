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
    server_path = os.path.join(bin_dir, "hello_world_server")
    client_path = os.path.join(bin_dir, "hello_world_client")

    if not os.path.exists(server_path):
        print(f"Error: Server binary not found at {server_path}")
        return False
    if not os.path.exists(client_path):
        print(f"Error: Client binary not found at {client_path}")
        return False

    print(f"Starting server: {server_path}")
    # Start server in background
    server_process = subprocess.Popen([server_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    try:
        # Give server time to start
        # QUIC uses UDP, so standard TCP connect check doesn't work easily.
        # We'll sleep and check if process is still alive.
        time.sleep(2)
        
        if server_process.poll() is not None:
             print("FAILED: Server process died prematurely")
             stdout, stderr = server_process.communicate()
             print(f"Server stdout: {stdout.decode(errors='replace')}")
             print(f"Server stderr: {stderr.decode(errors='replace')}")
             return False

        print(f"Starting client: {client_path}")
        # Run client and capture output
        client_process = subprocess.run([client_path], capture_output=True, text=True, timeout=5)

        if client_process.returncode != 0:
            print(f"Client failed with return code {client_process.returncode}")
            print("Client stderr:", client_process.stderr)
            return False

        output = client_process.stdout
        print("Client output:\n" + "-"*20 + "\n" + output + "\n" + "-"*20)

        # Verification
        expected_strings = [
            "Status: 200",
            "Body: hello world"
        ]

        missing = []
        for s in expected_strings:
            if s not in output:
                missing.append(s)

        if missing:
            print(f"FAILED: Missing expected output: {missing}")
            return False

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
        if server_process.poll() is None:
            server_process.terminate()
        
        try:
            stdout, stderr = server_process.communicate(timeout=2)
            print("Server stdout:\n" + "-"*20 + "\n" + (stdout.decode(errors='replace') if stdout else "") + "\n" + "-"*20)
            print("Server stderr:\n" + "-"*20 + "\n" + (stderr.decode(errors='replace') if stderr else "") + "\n" + "-"*20)
        except Exception as e:
            print(f"Error reading server output: {e}")

        server_process.wait()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run hello_world example test")
    parser.add_argument("--bin-dir", default="../../build/bin", help="Directory containing client/server binaries")
    args = parser.parse_args()

    # Make sure we use absolute path for bin_dir if it's relative
    bin_dir = os.path.abspath(args.bin_dir)
    
    success = run_test(bin_dir)
    sys.exit(0 if success else 1)
