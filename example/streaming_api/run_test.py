import subprocess
import time
import os
import signal
import sys
import argparse
import random
import hashlib

def run_test(bin_dir):
    server_path = os.path.join(bin_dir, "streaming_server")
    client_path = os.path.join(bin_dir, "streaming_client")

    if not os.path.exists(server_path):
        print(f"Error: Server binary not found at {server_path}")
        return False
    if not os.path.exists(client_path):
        print(f"Error: Client binary not found at {client_path}")
        return False

    # Create dummy file for test
    test_filename = "test_data.bin"
    download_filename = "downloaded_data.bin"
    
    # Generate 1MB of random data
    print(f"Generating test file: {test_filename} (1MB)")
    with open(test_filename, "wb") as f:
        f.write(os.urandom(1024 * 1024))
    
    # Calculate checksum
    with open(test_filename, "rb") as f:
        file_hash = hashlib.sha256(f.read()).hexdigest()
    print(f"Original file hash: {file_hash}")

    print(f"Starting server: {server_path}")
    # Start server in background
    # Default port is 7009
    server_process = subprocess.Popen([server_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    try:
        # Give server time to start
        time.sleep(1)

        # Test 1: Check Status
        print("\nTest 1: Check Status")
        status_url = "https://127.0.0.1:7009/status"
        cmd = [client_path, status_url]
        print(f"Running: {' '.join(cmd)}")
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
        
        if result.returncode != 0:
            print(f"Status check failed with return code {result.returncode}")
            print("Stderr:", result.stderr)
            return False
        
        if "HTTP/3 Streaming Demo" not in result.stdout:
             print("FAILED: Expected status output not found")
             print("Output:", result.stdout)
             return False
        print("Status check passed")

        # Test 2: Upload File
        print("\nTest 2: Upload File")
        server_filename = "server_" + test_filename
        upload_url = f"https://127.0.0.1:7009/upload/{server_filename}"
        cmd = [client_path, upload_url, test_filename]
        print(f"Running: {' '.join(cmd)}")
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)

        if result.returncode != 0:
            print(f"Upload failed with return code {result.returncode}")
            print("Stderr:", result.stderr)
            return False
            
        if "Status: 200" not in result.stdout:
             print("FAILED: Upload did not return 200 OK")
             print("Output:", result.stdout)
             return False
        print("Upload passed")
        
        # Test 3: Download File
        print("\nTest 3: Download File")
        # Removing previous download if exists
        if os.path.exists(download_filename):
            os.remove(download_filename)
            
        # Download the file we just uploaded (using the server-side name)
        download_url = f"https://127.0.0.1:7009/download/{server_filename}"
        cmd = [client_path, download_url, download_filename]
        print(f"Running: {' '.join(cmd)}")
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)

        if result.returncode != 0:
            print(f"Download failed with return code {result.returncode}")
            print("Stderr:", result.stderr)
            return False

        if "Completed:" not in result.stdout:
             print("FAILED: Download completion message not found")
             print("Output:", result.stdout)
             return False

        # Verify file content
        if not os.path.exists(download_filename):
            print(f"FAILED: Downloaded file {download_filename} not created")
            return False
            
        with open(download_filename, "rb") as f:
            download_hash = hashlib.sha256(f.read()).hexdigest()
        
        print(f"Downloaded file hash: {download_hash}")
        
        if download_hash != file_hash:
            print("FAILED: Hash mismatch! integrity check failed.")
            return False
            
        print("Download passed: integrity verified")

        print("\nSUCCESS: All tests completed successfully.")
        return True

    except subprocess.TimeoutExpired:
        print("FAILED: Operation timed out")
        return False
    except Exception as e:
        print(f"FAILED: Exception occurred: {e}")
        return False
    finally:
        # cleanup server
        print("\nKilling server...")
        if server_process.poll() is None:
            server_process.terminate()
        
        try:
            # Wait briefly for termination
            stdout, stderr = server_process.communicate(timeout=2)
        except Exception as e:
            print(f"Error reading server output: {e}")

        server_process.wait()
        
        # Cleanup files
        if os.path.exists(test_filename):
            os.remove(test_filename)
        if os.path.exists(download_filename):
            os.remove(download_filename)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run streaming_api example test")
    parser.add_argument("--bin-dir", default="../../build/bin", help="Directory containing client/server binaries")
    args = parser.parse_args()

    # Make sure we use absolute path for bin_dir if it's relative
    bin_dir = os.path.abspath(args.bin_dir)
    
    success = run_test(bin_dir)
    sys.exit(0 if success else 1)
