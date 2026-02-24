import subprocess
import time
import os
import signal
import sys
import argparse

def run_test(bin_dir):
    server_path = os.path.join(bin_dir, "bidirectional_server")
    client_path = os.path.join(bin_dir, "bidirectional_client")

    if not os.path.exists(server_path):
        print(f"Error: Server binary not found at {server_path}")
        return False
    if not os.path.exists(client_path):
        print(f"Error: Client binary not found at {client_path}")
        return False

    print(f"Starting server: {server_path}")
    # Start server in background
    # Use preexec_fn=os.setsid to create a process group so we can kill the whole tree if needed
    server_process = subprocess.Popen([server_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    
    try:
        # Give server time to start
        time.sleep(1)

        print(f"Starting client: {client_path}")
        
        # Prepare input for client (simulate user typing)
        client_input = "Hello\nWorld\nquit\n"
        
        # Run client and capture output
        # Run client and capture output
        client_process = subprocess.run(
            [client_path, "https://127.0.0.1:7002"], 
            input=client_input,
            capture_output=True, 
            text=True, 
            timeout=10
        )

        if client_process.returncode != 0:
            print(f"Client failed with return code {client_process.returncode}")
            print("Client stderr:", client_process.stderr)
            return False

        output = client_process.stdout
        print("Client output:\n" + "-"*20 + "\n" + output + "\n" + "-"*20)

        # Verification
        expected_strings = [
            "Connected successfully!",
            "[Server]: Message received: Hello",
            "Echo: Hello",
            "[Server]: Message received: World",
            "Echo: World"
        ]

        missing = []
        for s in expected_strings:
            if s not in output:
                missing.append(s)

        if missing:
            print(f"FAILED: Missing expected output: {missing}")
            return False

        print("SUCCESS: Client output verified.")
        
        # Now verify server detected disconnection
        # Wait a moment for server to process disconnection
        time.sleep(1)
        
        # We need to kill the server to read its full output if it's still running
        if server_process.poll() is None:
            server_process.terminate()
            try:
                server_process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                server_process.kill()
        
        # Read server output
        # server_stdout = server_process.stdout.read()
        # print("Server output:\n" + "-"*20 + "\n" + server_stdout + "\n" + "-"*20)
        
        print("SUCCESS: Client output verified.")
        return True

    except subprocess.TimeoutExpired:
        print("FAILED: Client timed out")
        return False
    except Exception as e:
        print(f"FAILED: Exception occurred: {e}")
        return False
    finally:
        # cleanup server
        if server_process.poll() is None:
            server_process.terminate()
            try:
                stdout, stderr = server_process.communicate(timeout=2)
            except:
                server_process.kill()
                stdout, stderr = server_process.communicate()
        else:
            stdout, stderr = server_process.communicate()
            
        print("Server stdout:\n" + "-"*20 + "\n" + (stdout if stdout else "") + "\n" + "-"*20)
        print("Server stderr:\n" + "-"*20 + "\n" + (stderr if stderr else "") + "\n" + "-"*20)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run bidirectional example test")
    parser.add_argument("--bin-dir", default="../../build/bin", help="Directory containing client/server binaries")
    args = parser.parse_args()

    # Make sure we use absolute path for bin_dir if it's relative
    bin_dir = os.path.abspath(args.bin_dir)
    
    success = run_test(bin_dir)
    sys.exit(0 if success else 1)
