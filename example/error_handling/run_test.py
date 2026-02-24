#!/usr/bin/env python3
"""
Test script for error handling example.
Runs the error handling server and client to verify various error scenarios.
"""

import subprocess
import time
import os
import sys
import argparse

def run_test(bin_dir):
    server_path = os.path.join(bin_dir, "error_handling_server")
    client_path = os.path.join(bin_dir, "error_handling_client")

    if not os.path.exists(server_path):
        print(f"Error: Server binary not found at {server_path}")
        return False
    if not os.path.exists(client_path):
        print(f"Error: Client binary not found at {client_path}")
        return False

    print(f"Starting server: {server_path}")
    # Start server in background
    server_process = subprocess.Popen(
        [server_path], 
        stdout=subprocess.PIPE, 
        stderr=subprocess.PIPE, 
        text=True
    )
    
    try:
        # Give server time to start
        time.sleep(2)

        print(f"Starting client: {client_path} all https://127.0.0.1:7005")
        
        # Run client with "all" scenario to test everything
        # Note: timeout test takes longest (~20s with timeout + retries simulation)
        client_process = subprocess.run(
            [client_path, "all", "https://127.0.0.1:7005"], 
            capture_output=True, 
            text=True, 
            timeout=60  # Long timeout to accommodate all tests including timeout test
        )

        output = client_process.stdout
        print("Client output:\n" + "-"*60 + "\n" + output + "\n" + "-"*60)

        if client_process.returncode != 0:
            print(f"Client failed with return code {client_process.returncode}")
            print("Client stderr:", client_process.stderr)
            return False

        # Verification - check for expected output strings
        expected_strings = [
            "Error Handling Test Suite",
            "Test 1: Connection Timeout",
            "Test 2: Error Response Handling",
            "Test 3: Retry with Exponential Backoff",
            "Test 4: Large Response Handling",
            "Test 5: Normal Request",
            "All tests completed!"
        ]

        missing = []
        for s in expected_strings:
            if s not in output:
                missing.append(s)

        if missing:
            print(f"FAILED: Missing expected output: {missing}")
            return False

        # Check for successful tests
        success_markers = [
            ("Normal Request", "HTTP 200" in output or "✓" in output),
            ("Error Response", "HTTP 500" in output or "Error response handled correctly" in output),
            ("Retry Logic", "Success!" in output),
        ]
        
        all_passed = True
        for test_name, passed in success_markers:
            if passed:
                print(f"✓ {test_name}: PASSED")
            else:
                print(f"✗ {test_name}: FAILED")
                all_passed = False
        
        # Check large response
        if "Received" in output and "bytes" in output:
            print("✓ Large Response: PASSED")
        else:
            print("✗ Large Response: FAILED")
            all_passed = False
        
        # Check timeout handling (should timeout as expected)
        if "timed out" in output.lower() or "timeout" in output.lower():
            print("✓ Timeout Handling: PASSED")
        else:
            print("✗ Timeout Handling: FAILED")
            all_passed = False
        
        if all_passed:
            print("\nSUCCESS: All error handling tests passed!")
        else:
            print("\nWARNING: Some tests may not have passed as expected")
        
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

        # Show server output for debugging
        try:
            server_stdout, server_stderr = server_process.communicate(timeout=1)
            if server_stdout:
                print("Server output:\n" + "-"*60 + "\n" + server_stdout[:2000] + "\n" + "-"*60)
        except:
            pass

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run error handling example test")
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
            if os.path.exists(os.path.join(abs_path, "error_handling_server")):
                bin_dir = abs_path
                break
        if not bin_dir:
            print("Error: Could not find bin directory. Please specify with --bin-dir")
            sys.exit(1)
    
    success = run_test(bin_dir)
    sys.exit(0 if success else 1)
