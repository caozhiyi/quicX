#!/usr/bin/env python3
"""
Simple test script for file transfer example.
Creates a 50MB file, uploads it, downloads it, and verifies MD5 checksum.
"""

import subprocess
import time
import os
import sys
import argparse
import tempfile
import hashlib

def generate_test_file(filepath, size_mb):
    """Generate a test file with deterministic content."""
    print(f"Generating {size_mb}MB test file: {filepath}")
    pattern = bytes(range(256)) * 4096  # 1MB pattern
    with open(filepath, 'wb') as f:
        for _ in range(size_mb):
            f.write(pattern[:1024*1024])

def calculate_md5(filepath):
    """Calculate MD5 checksum of a file."""
    hash_md5 = hashlib.md5()
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

def main():
    parser = argparse.ArgumentParser(description='File transfer test')
    parser.add_argument('--bin-dir', default='./build/bin', help='Binary directory')
    parser.add_argument('--size', type=int, default=50, help='Test file size in MB')
    args = parser.parse_args()

    bin_dir = os.path.abspath(args.bin_dir)
    server_bin = os.path.join(bin_dir, 'file_transfer_server')
    client_bin = os.path.join(bin_dir, 'file_transfer_client')

    if not os.path.exists(server_bin) or not os.path.exists(client_bin):
        print(f"Error: Binaries not found in {bin_dir}")
        return 1

    with tempfile.TemporaryDirectory() as tmpdir:
        # Generate test file
        test_file = os.path.join(tmpdir, 'test.bin')
        generate_test_file(test_file, args.size)
        original_md5 = calculate_md5(test_file)
        print(f"Original MD5: {original_md5}")
        print()

        # Start server
        print(f"Starting server with root: {tmpdir}")
        server = subprocess.Popen([server_bin, tmpdir], 
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        time.sleep(2)

        try:
            # Upload test
            print("=" * 60)
            print("UPLOAD TEST")
            print("=" * 60)
            result = subprocess.run(
                [client_bin, 'upload', 'https://127.0.0.1:7006/upload/uploaded.bin', test_file],
                capture_output=True, text=True, timeout=120
            )
            print(result.stdout)
            if result.returncode != 0:
                print(f"Upload failed: {result.stderr}")
                return 1

            # Verify uploaded file
            uploaded_file = os.path.join(tmpdir, 'uploaded.bin')
            # Wait a bit for file to be written and closed
            time.sleep(1)
            
            # Debug: list all files in tmpdir
            print(f"Files in {tmpdir}:")
            for f in os.listdir(tmpdir):
                fpath = os.path.join(tmpdir, f)
                print(f"  {f}: {os.path.getsize(fpath)} bytes")
            
            if os.path.exists(uploaded_file):
                uploaded_md5 = calculate_md5(uploaded_file)
                print(f"Uploaded file MD5: {uploaded_md5}")
                if uploaded_md5 == original_md5:
                    print("✓ Upload checksum verified!")
                else:
                    print("✗ Upload checksum MISMATCH!")
                    return 1
            else:
                print("✗ Uploaded file not found on server")
                return 1

            print()

            # Download test
            print("=" * 60)
            print("DOWNLOAD TEST")
            print("=" * 60)
            download_file = os.path.join(tmpdir, 'downloaded.bin')
            result = subprocess.run(
                [client_bin, 'download', 'https://127.0.0.1:7006/uploaded.bin', download_file],
                capture_output=True, text=True, timeout=120
            )
            print(result.stdout)
            if result.returncode != 0:
                print(f"Download failed: {result.stderr}")
                return 1

            # Verify downloaded file
            if os.path.exists(download_file):
                downloaded_md5 = calculate_md5(download_file)
                print(f"Downloaded file MD5: {downloaded_md5}")
                if downloaded_md5 == original_md5:
                    print("✓ Download checksum verified!")
                else:
                    print("✗ Download checksum MISMATCH!")
                    return 1
            else:
                print("✗ Downloaded file not found")
                return 1

            print()
            print("=" * 60)
            print("ALL TESTS PASSED!")
            print("=" * 60)
            return 0

        except subprocess.TimeoutExpired:
            print("Test timed out!")
            return 1
        finally:
            server.terminate()
            server.wait()

if __name__ == '__main__':
    sys.exit(main())
