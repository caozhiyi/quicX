import subprocess
import time
import os
import signal
import sys
import argparse

def run_test(bin_dir):
    # Binary names based on CMakeLists.txt projects: server_push and client_push
    server_path = os.path.join(bin_dir, "server_push")
    client_path = os.path.join(bin_dir, "client_push")

    if not os.path.exists(server_path):
        print(f"Error: Server binary not found at {server_path}")
        return False
    if not os.path.exists(client_path):
        print(f"Error: Client binary not found at {client_path}")
        return False

    print(f"Starting server: {server_path}")
    # Start server in background
    # Using a different port (7008) as per code
    server_process = subprocess.Popen([server_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    try:
        # Give server time to start
        time.sleep(1)

        print(f"Starting client: {client_path}")
        # Run client and capture output. 
        # The client has sleeps (1s + 1s), so we need a timeout > 2s. 
        # Giving it 10s to be safe.
        client_process = subprocess.run([client_path], capture_output=True, text=True, timeout=10)

        if client_process.returncode != 0:
            print(f"Client failed with return code {client_process.returncode}")
            print("Client stderr:", client_process.stderr)
            return False

        output = client_process.stdout
        print("Client output:\n" + "-"*20 + "\n" + output + "\n" + "-"*20)

        # Verification
        # We expect:
        # 1. Normal response verification
        # 2. Push promise verification
        # 3. Push response verification (for the first request)
        
        expected_strings = [
            "status: 200",
            "response: hello world",
            "push status: 200",
            "push response: hello push",
            "get push promise. header:push-key1 value:test1" 
        ]

        missing = []
        for s in expected_strings:
            if s not in output:
                missing.append(s)

        if missing:
            print(f"FAILED: Missing expected output: {missing}")
            return False
            
        # Verify strict counts or order if needed, but existence is a good start.
        # We expect "status: 200" at least twice (two requests).
        if output.count("status: 200") < 2:
             print("FAILED: Expected 'status: 200' at least twice.")
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
            # Wait briefly for termination
            stdout, stderr = server_process.communicate(timeout=2)
            # print("Server stdout:\n" + "-"*20 + "\n" + (stdout.decode(errors='replace') if stdout else "") + "\n" + "-"*20)
            # print("Server stderr:\n" + "-"*20 + "\n" + (stderr.decode(errors='replace') if stderr else "") + "\n" + "-"*20)
        except Exception as e:
            print(f"Error reading server output: {e}")

        server_process.wait()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run server_push example test")
    parser.add_argument("--bin-dir", default="../../build/bin", help="Directory containing client/server binaries")
    args = parser.parse_args()

    # Make sure we use absolute path for bin_dir if it's relative
    bin_dir = os.path.abspath(args.bin_dir)
    
    success = run_test(bin_dir)
    sys.exit(0 if success else 1)
