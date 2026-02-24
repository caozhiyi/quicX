import subprocess
import time
import os
import sys
import argparse

def run_test(bin_dir):
    # Use hello_world server as the test server (simplest)
    server_path = os.path.join(bin_dir, "hello_world_server")
    demo_path = os.path.join(bin_dir, "connection_lifecycle_demo")

    if not os.path.exists(server_path):
        # Try concurrent_server as fallback
        server_path = os.path.join(bin_dir, "concurrent_server")
        if not os.path.exists(server_path):
            print(f"Error: No server binary found in {bin_dir}")
            print("  Tried: hello_world_server, concurrent_server")
            return False

    if not os.path.exists(demo_path):
        print(f"Error: Demo binary not found at {demo_path}")
        return False

    # Determine server port based on which server we're using
    if "hello_world" in server_path:
        server_port = 7001
    elif "concurrent" in server_path:
        server_port = 7003
    else:
        server_port = 8443
    
    print(f"Starting server: {server_path} (port {server_port})")
    server_process = subprocess.Popen(
        [server_path], 
        stdout=subprocess.PIPE, 
        stderr=subprocess.PIPE, 
        text=True
    )
    
    try:
        # Give server more time to start and bind port
        time.sleep(2)

        print(f"Starting demo: {demo_path}")
        
        # Run demo and capture output
        demo_process = subprocess.run(
            [demo_path, f"https://127.0.0.1:{server_port}"], 
            capture_output=True, 
            text=True, 
            timeout=60  # Longer timeout for lifecycle tests
        )

        output = demo_process.stdout
        print("Demo output:\n" + "-"*60 + "\n" + output + "\n" + "-"*60)

        if demo_process.returncode != 0:
            print(f"Demo failed with return code {demo_process.returncode}")
            print("stderr:", demo_process.stderr)
            return False

        # Verification - check for expected output strings
        expected_strings = [
            "Connection Lifecycle Demo",
            "Test 1: Connection Reuse",
            "Test 2: Health Check",
            "Test 3: Idle Connection Cleanup",
            "Test 4: Graceful Shutdown",
            "Demo completed!"
        ]

        missing = []
        for s in expected_strings:
            if s not in output:
                missing.append(s)

        if missing:
            print(f"FAILED: Missing expected output: {missing}")
            return False

        # Check for connection reuse (key feature)
        if "Reused existing connection" in output:
            print("SUCCESS: Connection reuse verified")
        elif "Created new connection" in output:
            print("OK: Connections created (reuse may not have triggered)")
        else:
            print("WARNING: No connection activity detected")

        # Check for graceful shutdown
        if "All connections closed gracefully" in output:
            print("SUCCESS: Graceful shutdown verified")
        
        print("SUCCESS: Demo output verified.")
        return True

    except subprocess.TimeoutExpired:
        print("FAILED: Demo timed out")
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
    parser = argparse.ArgumentParser(description="Run connection lifecycle demo test")
    parser.add_argument("--bin-dir", default=None, help="Directory containing binaries")
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
            if os.path.exists(os.path.join(abs_path, "connection_lifecycle_demo")):
                bin_dir = abs_path
                break
        if not bin_dir:
            print("Error: Could not find bin directory. Please specify with --bin-dir")
            sys.exit(1)
    
    success = run_test(bin_dir)
    sys.exit(0 if success else 1)
