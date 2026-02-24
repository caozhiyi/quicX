#!/usr/bin/env python3
"""
quicX QUIC Interoperability Test Runner

Replaces run_full_interop.sh and cross_impl_matrix.sh with a robust Python
implementation. Supports self-testing, cross-implementation testing, and
full matrix generation.

Based on: https://github.com/quic-interop/quic-interop-runner
Reference: https://interop.seemann.io/

Usage:
    # Self-test (quicX client + quicX server)
    python3 interop_runner.py

    # Cross-impl test
    python3 interop_runner.py --client quicx --server quiche

    # Full matrix (selected implementations)
    python3 interop_runner.py --matrix --implementations quicx,quiche,ngtcp2

    # Full matrix (all implementations)
    python3 interop_runner.py --matrix --implementations all

    # Specific scenario
    python3 interop_runner.py --scenario handshake

    # Local mode (no Docker, use local binaries)
    python3 interop_runner.py --local
"""

from __future__ import annotations

import argparse
import filecmp
import json
import logging
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

from testcases import SCENARIOS, TEST_FILE_SIZES, TestResult, TestScenario

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent
DEFAULT_PORT = 443
DEFAULT_HOST = "localhost"
CONTAINER_TIMEOUT = 60  # seconds per test
SERVER_STARTUP_WAIT = 2  # seconds to wait for server to start
UNSUPPORTED_EXIT_CODE = 127

logger = logging.getLogger("interop")


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------

@dataclass
class SingleTestResult:
    scenario: str
    server: str
    client: str
    result: TestResult
    duration_s: float = 0.0
    error_message: str = ""


@dataclass
class BuildConfig:
    dockerfile: str
    context: str

@dataclass
class Implementation:
    name: str
    image: str
    url: str
    role: str  # "both", "server", "client"
    build: Optional[dict] = None

    def can_be_server(self) -> bool:
        return self.role in ("both", "server")

    def can_be_client(self) -> bool:
        return self.role in ("both", "client")

    @property
    def build_config(self) -> Optional[BuildConfig]:
        if self.build:
            return BuildConfig(**self.build)
        return None


# ---------------------------------------------------------------------------
# Implementation registry
# ---------------------------------------------------------------------------

def load_implementations() -> dict[str, Implementation]:
    """Load implementation definitions from implementations.json."""
    json_path = SCRIPT_DIR / "implementations.json"
    with open(json_path) as f:
        data = json.load(f)
    return {
        name: Implementation(name=name, **info)
        for name, info in data.items()
    }


# ---------------------------------------------------------------------------
# Docker helpers
# ---------------------------------------------------------------------------

class DockerManager:
    """Manages Docker containers for interop testing."""

    def __init__(self, timeout: int = CONTAINER_TIMEOUT, certs_dir: Optional[Path] = None):
        self.timeout = timeout
        self._active_containers: list[str] = []
        self._certs_dir = certs_dir

    def is_available(self) -> bool:
        try:
            subprocess.run(
                ["docker", "info"],
                capture_output=True, timeout=10, check=False,
            )
            return True
        except (FileNotFoundError, subprocess.TimeoutExpired):
            return False

    def image_exists(self, image: str) -> bool:
        r = subprocess.run(
            ["docker", "images", "-q", image],
            capture_output=True, text=True, check=False,
        )
        return bool(r.stdout.strip())

    def pull_image(self, image: str) -> bool:
        logger.info("Pulling image: %s", image)
        r = subprocess.run(
            ["docker", "pull", "--platform", "linux/amd64", image],
            capture_output=True, text=True, check=False,
        )
        return r.returncode == 0

    def build_image(self, image: str, dockerfile: str, context: str) -> bool:
        """Build a Docker image from a Dockerfile."""
        # Resolve paths relative to project root
        project_root = SCRIPT_DIR.parent.parent
        dockerfile_path = project_root / dockerfile
        context_path = project_root / context

        if not dockerfile_path.exists():
            logger.error("Dockerfile not found: %s", dockerfile_path)
            return False

        logger.info("Building image: %s (dockerfile=%s, context=%s)",
                     image, dockerfile_path, context_path)
        r = subprocess.run(
            [
                "docker", "build",
                "-t", image,
                "-f", str(dockerfile_path),
                str(context_path),
            ],
            text=True, check=False,
        )
        if r.returncode != 0:
            logger.error("Failed to build image: %s", image)
            return False
        logger.info("Successfully built image: %s", image)
        return True

    def ensure_image(self, image: str, build_config: Optional[BuildConfig] = None, force_build: bool = False) -> bool:
        # Check if image already exists
        if not force_build and self.image_exists(image):
            logger.info("Image already exists: %s", image)
            return True
        
        # If build config is provided, build locally instead of pulling
        if build_config:
            logger.info("Building local image: %s", image)
            return self.build_image(image, build_config.dockerfile, build_config.context)
        
        # Otherwise, try to pull from registry
        logger.info("Attempting to pull image: %s", image)
        return self.pull_image(image)

    def start_server(
        self,
        name: str,
        image: str,
        scenario: str,
        port: int,
        www_dir: Path,
        log_dir: Path,
        extra_env: Optional[dict[str, str]] = None,
    ) -> bool:
        """Start a server container in background. Returns True if started."""
        container_name = f"{name}-server-{os.getpid()}"
        self._active_containers.append(container_name)

        certs_dir = self._certs_dir or (Path(__file__).resolve().parent / "certs")

        cmd = [
            "docker", "run", "-d", "--rm",
            "--name", container_name,
            "--network", "host",
            "--cap-add", "NET_ADMIN",
            "-e", "ROLE=server",
            "-e", f"PORT={port}",
            "-e", f"TESTCASE={scenario}",
            "-e", "QLOGDIR=/logs/qlog/",
            "-e", "SSLKEYLOGFILE=/logs/keys.log",
            "-v", f"{www_dir}:/www:ro",
            "-v", f"{certs_dir}:/certs:ro",
            "-v", f"{log_dir}:/logs",
        ]
        
        # Only add --platform for remote images (with registry prefix)
        if any(image.startswith(prefix) for prefix in ["docker.io/", "ghcr.io/", "quay.io/", "cloudflare/", "aiortc/", "privateoctopus/", "litespeedtech/", "martenseemann/", "stammw/"]):
            cmd.insert(3, "--platform")
            cmd.insert(4, "linux/amd64")

        if extra_env:
            for k, v in extra_env.items():
                cmd.extend(["-e", f"{k}={v}"])

        cmd.append(image)

        r = subprocess.run(cmd, capture_output=True, text=True, check=False)
        if r.returncode != 0:
            logger.error("Failed to start server: %s", r.stderr.strip())
            self._save_output(log_dir, "docker_run", r.stdout, r.stderr)
            return False

        # Wait for server readiness
        time.sleep(SERVER_STARTUP_WAIT)

        # Verify container is running
        check = subprocess.run(
            ["docker", "ps", "-q", "-f", f"name={container_name}"],
            capture_output=True, text=True, check=False,
        )
        if not check.stdout.strip():
            logger.error("Server container exited prematurely")
            logs = subprocess.run(
                ["docker", "logs", container_name],
                capture_output=True, text=True, check=False,
            )
            logger.error("Server logs:\n%s", logs.stdout + logs.stderr)
            self._save_output(log_dir, "container", logs.stdout, logs.stderr)
            return False

        return True

    def run_client(
        self,
        name: str,
        image: str,
        scenario: str,
        host: str,
        port: int,
        urls: str,
        download_dir: Path,
        log_dir: Path,
        extra_env: Optional[dict[str, str]] = None,
    ) -> int:
        """Run a client container (blocking). Returns exit code."""
        container_name = f"{name}-client-{os.getpid()}-{int(time.time() * 1000) % 100000}"

        certs_dir = self._certs_dir or (Path(__file__).resolve().parent / "certs")

        cmd = [
            "docker", "run", "--rm",
            "--name", container_name,
            "--network", "host",
            "--cap-add", "NET_ADMIN",
            "-e", "ROLE=client",
            "-e", f"SERVER={host}",
            "-e", f"PORT={port}",
            "-e", f"REQUESTS={urls}",
            "-e", f"TESTCASE={scenario}",
            "-e", "QLOGDIR=/logs/qlog/",
            "-e", "SSLKEYLOGFILE=/logs/keys.log",
            "-v", f"{download_dir}:/downloads:delegated",
            "-v", f"{certs_dir}:/certs:ro",
            "-v", f"{log_dir}:/logs",
        ]
        
        # Only add --platform for remote images (with registry prefix)
        if any(image.startswith(prefix) for prefix in ["docker.io/", "ghcr.io/", "quay.io/", "cloudflare/", "aiortc/", "privateoctopus/", "litespeedtech/", "martenseemann/", "stammw/"]):
            cmd.insert(3, "--platform")
            cmd.insert(4, "linux/amd64")

        if extra_env:
            for k, v in extra_env.items():
                cmd.extend(["-e", f"{k}={v}"])

        cmd.append(image)

        try:
            r = subprocess.run(
                cmd, capture_output=True, text=True,
                timeout=self.timeout, check=False,
            )
            self._save_output(log_dir, "container", r.stdout, r.stderr)
            if r.returncode != 0:
                logger.debug("Client stderr:\n%s", r.stderr)
            return r.returncode
        except subprocess.TimeoutExpired:
            logger.error("Client timed out after %ds", self.timeout)
            subprocess.run(
                ["docker", "stop", container_name],
                capture_output=True, check=False,
            )
            return 1

    @staticmethod
    def _save_output(log_dir: Path, prefix: str, stdout: str, stderr: str) -> None:
        """Save container stdout/stderr to log directory."""
        try:
            if stdout:
                (log_dir / f"{prefix}_stdout.log").write_text(stdout)
            if stderr:
                (log_dir / f"{prefix}_stderr.log").write_text(stderr)
        except OSError as e:
            logger.debug("Failed to save container output: %s", e)

    def stop_server(self, name: str, log_dir: Optional[Path] = None) -> None:
        container_name = f"{name}-server-{os.getpid()}"
        # Capture server container logs before stopping
        if log_dir:
            logs = subprocess.run(
                ["docker", "logs", container_name],
                capture_output=True, text=True, check=False,
            )
            self._save_output(log_dir, "container", logs.stdout, logs.stderr)
        subprocess.run(
            ["docker", "stop", "-t", "2", container_name],
            capture_output=True, check=False,
        )
        if container_name in self._active_containers:
            self._active_containers.remove(container_name)

    def cleanup(self) -> None:
        for c in list(self._active_containers):
            subprocess.run(
                ["docker", "stop", "-t", "2", c], capture_output=True, check=False,
            )
        self._active_containers.clear()


# ---------------------------------------------------------------------------
# Local process helpers (no Docker)
# ---------------------------------------------------------------------------

class LocalProcessManager:
    """Manages local binary processes for testing without Docker."""

    def __init__(self, build_dir: Optional[Path] = None):
        self.build_dir = build_dir or (PROJECT_ROOT / "build")
        self._server_proc: Optional[subprocess.Popen] = None
        self._cert_dir: Optional[Path] = None

    @property
    def server_bin(self) -> Path:
        return self.build_dir / "bin" / "interop_server"

    @property
    def client_bin(self) -> Path:
        return self.build_dir / "bin" / "interop_client"

    def is_available(self) -> bool:
        return self.server_bin.exists() and self.client_bin.exists()

    def generate_certs(self, cert_dir: Path) -> bool:
        cert_dir.mkdir(parents=True, exist_ok=True)
        self._cert_dir = cert_dir
        r = subprocess.run(
            [
                "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                "-keyout", str(cert_dir / "priv.key"),
                "-out", str(cert_dir / "cert.pem"),
                "-days", "1",
                "-subj", "/CN=localhost",
            ],
            capture_output=True, check=False,
        )
        return r.returncode == 0

    def start_server(
        self,
        port: int,
        www_dir: Path,
        cert_dir: Path,
        extra_env: Optional[dict[str, str]] = None,
    ) -> bool:
        self.stop_server()

        env = os.environ.copy()
        env.update({
            "PORT": str(port),
            "WWW": str(www_dir),
            "CERT_FILE": str(cert_dir / "cert.pem"),
            "KEY_FILE": str(cert_dir / "priv.key"),
        })
        if extra_env:
            env.update(extra_env)

        self._server_proc = subprocess.Popen(
            [str(self.server_bin)],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        time.sleep(SERVER_STARTUP_WAIT)

        if self._server_proc.poll() is not None:
            _, stderr = self._server_proc.communicate()
            logger.error("Server failed to start: %s", stderr.decode())
            self._server_proc = None
            return False

        return True

    def run_client(
        self,
        host: str,
        port: int,
        urls: str,
        download_dir: Path,
        extra_env: Optional[dict[str, str]] = None,
        timeout: int = CONTAINER_TIMEOUT,
    ) -> int:
        env = os.environ.copy()
        env.update({
            "SERVER": host,
            "PORT": str(port),
            "REQUESTS": urls,
            "DOWNLOADS": str(download_dir),
        })
        if extra_env:
            env.update(extra_env)

        try:
            r = subprocess.run(
                [str(self.client_bin)],
                env=env,
                capture_output=True,
                timeout=timeout,
                check=False,
            )
            if r.returncode != 0:
                logger.debug("Client stderr:\n%s", r.stderr.decode())
            return r.returncode
        except subprocess.TimeoutExpired:
            logger.error("Client timed out after %ds", timeout)
            return 1

    def stop_server(self) -> None:
        if self._server_proc and self._server_proc.poll() is None:
            self._server_proc.terminate()
            try:
                self._server_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._server_proc.kill()
        self._server_proc = None

    def cleanup(self) -> None:
        self.stop_server()


# ---------------------------------------------------------------------------
# File management
# ---------------------------------------------------------------------------

def generate_test_files(www_dir: Path) -> None:
    """Generate random binary test files of various sizes."""
    www_dir.mkdir(parents=True, exist_ok=True)
    for name, size in TEST_FILE_SIZES.items():
        filepath = www_dir / name
        if not filepath.exists():
            logger.info("Generating %s (%d bytes)", name, size)
            with open(filepath, "wb") as f:
                f.write(os.urandom(size))


def verify_downloads(www_dir: Path, download_dir: Path, files: list[str]) -> tuple[bool, list[str]]:
    """Verify downloaded files match source files.

    Returns (all_ok, list_of_errors).
    """
    errors: list[str] = []
    for filename in files:
        src = www_dir / filename
        dst = download_dir / filename

        if not dst.exists():
            errors.append(f"File not downloaded: {filename}")
            continue

        src_size = src.stat().st_size
        dst_size = dst.stat().st_size
        if src_size != dst_size:
            errors.append(
                f"Size mismatch: {filename} (expected {src_size}, got {dst_size})"
            )
            continue

        if not filecmp.cmp(str(src), str(dst), shallow=False):
            errors.append(f"Content mismatch: {filename}")
            continue

        logger.debug("Verified: %s (%d bytes)", filename, dst_size)

    return (len(errors) == 0, errors)


# ---------------------------------------------------------------------------
# Test executor
# ---------------------------------------------------------------------------

class InteropTestRunner:
    """Orchestrates QUIC interoperability tests."""

    def __init__(
        self,
        *,
        local: bool = False,
        port: int = DEFAULT_PORT,
        host: str = DEFAULT_HOST,
        timeout: int = CONTAINER_TIMEOUT,
        build_dir: Optional[Path] = None,
        rebuild: bool = False,
    ):
        self.local = local
        self.port = port
        self.host = host
        self.timeout = timeout
        self.rebuild = rebuild
        self.results: list[SingleTestResult] = []

        # Working directories
        self.work_dir = SCRIPT_DIR
        self.www_dir = self.work_dir / "www"
        self.download_dir = self.work_dir / "downloads"
        self.log_dir = self.work_dir / "logs"

        if local:
            self.process_mgr = LocalProcessManager(build_dir)
            self.docker_mgr = None
        else:
            self.docker_mgr = DockerManager(timeout=timeout)
            self.process_mgr = None

    # -- Setup / Teardown --------------------------------------------------

    def _test_log_dir(self, scenario: str, server_name: str, client_name: str, role: str) -> Path:
        """Return per-test log directory: logs/{scenario}/{server}_{client}/{role}/"""
        d = self.log_dir / scenario / f"{server_name}_{client_name}" / role
        d.mkdir(parents=True, exist_ok=True)
        return d

    def setup(self) -> None:
        """Prepare test environment."""
        generate_test_files(self.www_dir)
        self.download_dir.mkdir(parents=True, exist_ok=True)
        self.log_dir.mkdir(parents=True, exist_ok=True)

        if self.local:
            assert self.process_mgr is not None
            if not self.process_mgr.is_available():
                raise RuntimeError(
                    f"Local binaries not found in {self.process_mgr.build_dir}/bin/. "
                    "Please build with: cmake --build build --target interop_server interop_client"
                )
            cert_dir = self.work_dir / "certs"
            if not (cert_dir / "cert.pem").exists():
                logger.info("Generating TLS certificates...")
                if not self.process_mgr.generate_certs(cert_dir):
                    raise RuntimeError("Failed to generate TLS certificates")
        else:
            assert self.docker_mgr is not None
            if not self.docker_mgr.is_available():
                raise RuntimeError("Docker is not available")

    def cleanup(self) -> None:
        """Clean up test environment."""
        if self.docker_mgr:
            self.docker_mgr.cleanup()
        if self.process_mgr:
            self.process_mgr.cleanup()

        # Clean downloads
        if self.download_dir.exists():
            shutil.rmtree(self.download_dir, ignore_errors=True)
            self.download_dir.mkdir(parents=True, exist_ok=True)

    # -- Docker mode -------------------------------------------------------

    def _docker_start_server(
        self, impl: str, image: str, scenario: str,
        log_dir: Path,
        extra_env: Optional[dict[str, str]] = None,
    ) -> bool:
        assert self.docker_mgr is not None
        return self.docker_mgr.start_server(
            name=impl,
            image=image,
            scenario=scenario,
            port=self.port,
            www_dir=self.www_dir,
            log_dir=log_dir,
            extra_env=extra_env,
        )

    def _docker_run_client(
        self, impl: str, image: str, scenario: str, urls: str,
        log_dir: Path,
        extra_env: Optional[dict[str, str]] = None,
    ) -> int:
        assert self.docker_mgr is not None
        # Clean downloads before each client run
        if self.download_dir.exists():
            shutil.rmtree(self.download_dir, ignore_errors=True)
        self.download_dir.mkdir(parents=True, exist_ok=True)

        return self.docker_mgr.run_client(
            name=impl,
            image=image,
            scenario=scenario,
            host=self.host,
            port=self.port,
            urls=urls,
            download_dir=self.download_dir,
            log_dir=log_dir,
            extra_env=extra_env,
        )

    def _docker_stop_server(self, impl: str, log_dir: Optional[Path] = None) -> None:
        assert self.docker_mgr is not None
        self.docker_mgr.stop_server(impl, log_dir)
        time.sleep(1)

    # -- Local mode --------------------------------------------------------

    def _local_start_server(
        self, extra_env: Optional[dict[str, str]] = None,
    ) -> bool:
        assert self.process_mgr is not None
        cert_dir = self.work_dir / "certs"
        return self.process_mgr.start_server(
            port=self.port,
            www_dir=self.www_dir,
            cert_dir=cert_dir,
            extra_env=extra_env,
        )

    def _local_run_client(
        self, urls: str, extra_env: Optional[dict[str, str]] = None,
    ) -> int:
        assert self.process_mgr is not None
        # Clean downloads before each client run
        if self.download_dir.exists():
            shutil.rmtree(self.download_dir, ignore_errors=True)
        self.download_dir.mkdir(parents=True, exist_ok=True)

        return self.process_mgr.run_client(
            host=self.host,
            port=self.port,
            urls=urls,
            download_dir=self.download_dir,
            extra_env=extra_env,
            timeout=self.timeout,
        )

    def _local_stop_server(self) -> None:
        if self.process_mgr:
            self.process_mgr.stop_server()
            time.sleep(0.5)

    # -- URL builder -------------------------------------------------------

    def _build_urls(self, files: list[str]) -> str:
        return " ".join(
            f"https://{self.host}:{self.port}/{f}" for f in files
        )

    # -- Single scenario execution -----------------------------------------

    def _run_scenario_docker(
        self,
        scenario: TestScenario,
        server_impl: Implementation,
        client_impl: Implementation,
    ) -> SingleTestResult:
        """Execute a single test scenario in Docker mode."""
        server_name = server_impl.name
        client_name = client_impl.name
        server_image = server_impl.image
        client_image = client_impl.image

        server_log_dir = self._test_log_dir(scenario.name, server_name, client_name, "server")
        client_log_dir = self._test_log_dir(scenario.name, server_name, client_name, "client")

        urls = self._build_urls(scenario.files)

        # -- Two-connection scenarios (resumption, zerortt) --
        if scenario.needs_two_connections:
            return self._run_two_connection_test_docker(
                scenario, server_name, server_image, client_name, client_image, urls,
                server_log_dir, client_log_dir,
            )

        # -- Concurrent clients (multiconnect) --
        if scenario.concurrent_clients > 0:
            return self._run_multiconnect_test_docker(
                scenario, server_name, server_image, client_name, client_image, urls,
                server_log_dir, client_log_dir,
            )

        # -- Standard single-connection test --
        if not self._docker_start_server(
            server_name, server_image, scenario.name, server_log_dir,
            scenario.server_env or None,
        ):
            return SingleTestResult(
                scenario=scenario.name, server=server_name, client=client_name,
                result=TestResult.FAILED, error_message="Server failed to start",
            )

        try:
            exit_code = self._docker_run_client(
                client_name, client_image, scenario.name, urls, client_log_dir,
                scenario.client_env or None,
            )

            if exit_code == UNSUPPORTED_EXIT_CODE:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.UNSUPPORTED,
                )

            if exit_code != 0:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.FAILED,
                    error_message=f"Client exited with code {exit_code}",
                )

            # Verify files
            ok, errors = verify_downloads(self.www_dir, self.download_dir, scenario.files)
            if not ok:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.FAILED,
                    error_message="; ".join(errors),
                )

            return SingleTestResult(
                scenario=scenario.name, server=server_name, client=client_name,
                result=TestResult.PASSED,
            )
        finally:
            self._docker_stop_server(server_name, server_log_dir)

    def _run_two_connection_test_docker(
        self,
        scenario: TestScenario,
        server_name: str, server_image: str,
        client_name: str, client_image: str,
        urls: str,
        server_log_dir: Path, client_log_dir: Path,
    ) -> SingleTestResult:
        """Run a test that needs two sequential connections (resumption, zerortt)."""
        session_dir = Path(tempfile.mkdtemp(prefix="quicx-session-"))

        if not self._docker_start_server(server_name, server_image, scenario.name, server_log_dir):
            return SingleTestResult(
                scenario=scenario.name, server=server_name, client=client_name,
                result=TestResult.FAILED, error_message="Server failed to start",
            )

        try:
            # First connection
            client_env = dict(scenario.client_env or {})
            client_env["SESSION_CACHE"] = str(session_dir)

            exit_code = self._docker_run_client(
                client_name, client_image, scenario.name, urls, client_log_dir, client_env,
            )
            if exit_code == UNSUPPORTED_EXIT_CODE:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.UNSUPPORTED,
                )
            if exit_code != 0:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.FAILED,
                    error_message=f"First connection failed (exit {exit_code})",
                )

            time.sleep(1)

            # Second connection
            exit_code = self._docker_run_client(
                client_name, client_image, scenario.name, urls, client_log_dir, client_env,
            )
            if exit_code != 0:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.FAILED,
                    error_message=f"Second connection failed (exit {exit_code})",
                )

            ok, errors = verify_downloads(self.www_dir, self.download_dir, scenario.files)
            if not ok:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.FAILED, error_message="; ".join(errors),
                )

            return SingleTestResult(
                scenario=scenario.name, server=server_name, client=client_name,
                result=TestResult.PASSED,
            )
        finally:
            self._docker_stop_server(server_name, server_log_dir)
            shutil.rmtree(session_dir, ignore_errors=True)

    def _run_multiconnect_test_docker(
        self,
        scenario: TestScenario,
        server_name: str, server_image: str,
        client_name: str, client_image: str,
        urls: str,
        server_log_dir: Path, client_log_dir: Path,
    ) -> SingleTestResult:
        """Run concurrent client connections."""
        assert self.docker_mgr is not None
        n = scenario.concurrent_clients

        if not self._docker_start_server(server_name, server_image, scenario.name, server_log_dir):
            return SingleTestResult(
                scenario=scenario.name, server=server_name, client=client_name,
                result=TestResult.FAILED, error_message="Server failed to start",
            )

        try:
            successes = 0
            conn_dirs: list[Path] = []

            def run_one_client(idx: int) -> int:
                conn_dir = self.download_dir / f"conn{idx}"
                conn_dir.mkdir(parents=True, exist_ok=True)
                conn_dirs.append(conn_dir)
                per_client_log = client_log_dir / f"conn{idx}"
                per_client_log.mkdir(parents=True, exist_ok=True)
                container_name = f"{client_name}-multi-{os.getpid()}-{idx}"
                certs_dir = self.docker_mgr._certs_dir or (Path(__file__).resolve().parent / "certs")
                cmd = [
                    "docker", "run", "--rm",
                    "--name", container_name,
                    "--network", "host",
                    "--cap-add", "NET_ADMIN",
                    "-e", "ROLE=client",
                    "-e", f"SERVER={self.host}",
                    "-e", f"PORT={self.port}",
                    "-e", f"REQUESTS={urls}",
                    "-e", f"TESTCASE={scenario.name}",
                    "-e", "QLOGDIR=/logs/qlog/",
                    "-e", "SSLKEYLOGFILE=/logs/keys.log",
                    "-v", f"{conn_dir}:/downloads:delegated",
                    "-v", f"{certs_dir}:/certs:ro",
                    "-v", f"{per_client_log}:/logs",
                    client_image,
                ]
                r = subprocess.run(
                    cmd, capture_output=True, text=True, timeout=self.timeout, check=False,
                )
                self.docker_mgr._save_output(per_client_log, "container", r.stdout, r.stderr)
                return r.returncode

            with ThreadPoolExecutor(max_workers=n) as pool:
                futures = {pool.submit(run_one_client, i): i for i in range(n)}
                for future in as_completed(futures):
                    try:
                        if future.result() == 0:
                            successes += 1
                    except Exception as e:
                        logger.debug("Connection %d failed: %s", futures[future], e)

            # Cleanup temp dirs
            for d in conn_dirs:
                shutil.rmtree(d, ignore_errors=True)

            if successes == n:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.PASSED,
                )
            else:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.FAILED,
                    error_message=f"Only {successes}/{n} connections succeeded",
                )
        finally:
            self._docker_stop_server(server_name, server_log_dir)

    # -- Local mode scenario execution -------------------------------------

    def _run_scenario_local(self, scenario: TestScenario) -> SingleTestResult:
        """Execute a single test scenario in local mode."""
        urls = self._build_urls(scenario.files)
        server_name = "quicx"
        client_name = "quicx"

        if scenario.needs_two_connections:
            return self._run_two_connection_test_local(scenario, urls)

        if scenario.concurrent_clients > 0:
            return self._run_multiconnect_test_local(scenario, urls)

        # Standard test
        if not self._local_start_server(scenario.server_env or None):
            return SingleTestResult(
                scenario=scenario.name, server=server_name, client=client_name,
                result=TestResult.FAILED, error_message="Server failed to start",
            )

        try:
            exit_code = self._local_run_client(urls, scenario.client_env or None)

            if exit_code == UNSUPPORTED_EXIT_CODE:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.UNSUPPORTED,
                )
            if exit_code != 0:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.FAILED,
                    error_message=f"Client exited with code {exit_code}",
                )

            ok, errors = verify_downloads(self.www_dir, self.download_dir, scenario.files)
            if not ok:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.FAILED, error_message="; ".join(errors),
                )

            return SingleTestResult(
                scenario=scenario.name, server=server_name, client=client_name,
                result=TestResult.PASSED,
            )
        finally:
            self._local_stop_server()

    def _run_two_connection_test_local(
        self, scenario: TestScenario, urls: str,
    ) -> SingleTestResult:
        session_dir = Path(tempfile.mkdtemp(prefix="quicx-session-"))
        server_name = client_name = "quicx"

        if not self._local_start_server(scenario.server_env or None):
            return SingleTestResult(
                scenario=scenario.name, server=server_name, client=client_name,
                result=TestResult.FAILED, error_message="Server failed to start",
            )

        try:
            client_env = dict(scenario.client_env or {})
            client_env["SESSION_FILE"] = str(session_dir)

            # First connection
            exit_code = self._local_run_client(urls, client_env)
            if exit_code == UNSUPPORTED_EXIT_CODE:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.UNSUPPORTED,
                )
            if exit_code != 0:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.FAILED,
                    error_message=f"First connection failed (exit {exit_code})",
                )

            time.sleep(1)

            # Second connection
            exit_code = self._local_run_client(urls, client_env)
            if exit_code != 0:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.FAILED,
                    error_message=f"Second connection failed (exit {exit_code})",
                )

            ok, errors = verify_downloads(self.www_dir, self.download_dir, scenario.files)
            if not ok:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.FAILED, error_message="; ".join(errors),
                )

            return SingleTestResult(
                scenario=scenario.name, server=server_name, client=client_name,
                result=TestResult.PASSED,
            )
        finally:
            self._local_stop_server()
            shutil.rmtree(session_dir, ignore_errors=True)

    def _run_multiconnect_test_local(
        self, scenario: TestScenario, urls: str,
    ) -> SingleTestResult:
        assert self.process_mgr is not None
        n = scenario.concurrent_clients
        server_name = client_name = "quicx"

        if not self._local_start_server(scenario.server_env or None):
            return SingleTestResult(
                scenario=scenario.name, server=server_name, client=client_name,
                result=TestResult.FAILED, error_message="Server failed to start",
            )

        try:
            successes = 0

            def run_one(idx: int) -> int:
                conn_dir = self.download_dir / f"conn{idx}"
                conn_dir.mkdir(parents=True, exist_ok=True)
                env = {
                    "SERVER": self.host,
                    "PORT": str(self.port),
                    "REQUESTS": urls,
                    "DOWNLOADS": str(conn_dir),
                }
                try:
                    r = subprocess.run(
                        [str(self.process_mgr.client_bin)],
                        env={**os.environ, **env},
                        capture_output=True,
                        timeout=self.timeout,
                        check=False,
                    )
                    return r.returncode
                except subprocess.TimeoutExpired:
                    return 1

            with ThreadPoolExecutor(max_workers=n) as pool:
                futures = {pool.submit(run_one, i): i for i in range(n)}
                for future in as_completed(futures):
                    try:
                        if future.result() == 0:
                            successes += 1
                    except Exception:
                        pass

            if successes == n:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.PASSED,
                )
            else:
                return SingleTestResult(
                    scenario=scenario.name, server=server_name, client=client_name,
                    result=TestResult.FAILED,
                    error_message=f"Only {successes}/{n} connections succeeded",
                )
        finally:
            self._local_stop_server()

    # -- High-level execution ----------------------------------------------

    def run_test(
        self,
        scenario_name: str,
        server_impl: Optional[Implementation] = None,
        client_impl: Optional[Implementation] = None,
    ) -> SingleTestResult:
        """Run a single test scenario and record the result."""
        scenario = SCENARIOS.get(scenario_name)
        if scenario is None:
            return SingleTestResult(
                scenario=scenario_name,
                server=server_impl.name if server_impl else "quicx",
                client=client_impl.name if client_impl else "quicx",
                result=TestResult.UNSUPPORTED,
                error_message=f"Unknown scenario: {scenario_name}",
            )

        start = time.monotonic()

        if self.local:
            result = self._run_scenario_local(scenario)
        else:
            if server_impl is None or client_impl is None:
                raise ValueError("Docker mode requires server_impl and client_impl")
            if not self.docker_mgr.ensure_image(server_impl.image, server_impl.build_config):
                return SingleTestResult(
                    scenario=scenario_name, server=server_impl.name,
                    client=client_impl.name, result=TestResult.FAILED,
                    error_message=f"Docker image not available: {server_impl.image}",
                )
            if not self.docker_mgr.ensure_image(client_impl.image, client_impl.build_config):
                return SingleTestResult(
                    scenario=scenario_name, server=server_impl.name,
                    client=client_impl.name, result=TestResult.FAILED,
                    error_message=f"Docker image not available: {client_impl.image}",
                )
            result = self._run_scenario_docker(scenario, server_impl, client_impl)

        result.duration_s = round(time.monotonic() - start, 2)
        self.results.append(result)
        return result

    def run_self_test(
        self,
        scenarios: Optional[list[str]] = None,
        impl_name: str = "quicx",
    ) -> list[SingleTestResult]:
        """Run all scenarios as self-test (same impl for client and server)."""
        impls = load_implementations()
        impl = impls.get(impl_name)
        if impl is None and not self.local:
            raise ValueError(f"Unknown implementation: {impl_name}")

        # Pre-build/pull images before running tests (so build time is excluded from test timing)
        if not self.local and impl is not None:
            logger.info("Ensuring Docker image is available: %s", impl.image)
            if not self.docker_mgr.ensure_image(impl.image, impl.build_config, force_build=self.rebuild):
                raise RuntimeError(f"Docker image not available: {impl.image}")

        scenario_list = scenarios or list(SCENARIOS.keys())
        results = []

        for name in scenario_list:
            logger.info("--- Test: %s ---", name)
            if self.local:
                r = self.run_test(name)
            else:
                r = self.run_test(name, server_impl=impl, client_impl=impl)
            results.append(r)
            self._log_result(r)

        return results

    def run_cross_test(
        self,
        server_name: str,
        client_name: str,
        scenarios: Optional[list[str]] = None,
    ) -> list[SingleTestResult]:
        """Run tests with different client and server implementations."""
        impls = load_implementations()
        server = impls.get(server_name)
        client = impls.get(client_name)
        if server is None:
            raise ValueError(f"Unknown server implementation: {server_name}")
        if client is None:
            raise ValueError(f"Unknown client implementation: {client_name}")

        # Pre-build/pull images before running tests
        logger.info("Ensuring Docker images are available...")
        if not self.docker_mgr.ensure_image(server.image, server.build_config, force_build=self.rebuild):
            raise RuntimeError(f"Docker image not available: {server.image}")
        if not self.docker_mgr.ensure_image(client.image, client.build_config, force_build=self.rebuild):
            raise RuntimeError(f"Docker image not available: {client.image}")

        scenario_list = scenarios or list(SCENARIOS.keys())
        results = []

        for name in scenario_list:
            logger.info("--- Test: %s [%s -> %s] ---", name, client_name, server_name)
            r = self.run_test(name, server_impl=server, client_impl=client)
            results.append(r)
            self._log_result(r)

        return results

    def run_matrix(
        self,
        impl_names: list[str],
        scenarios: Optional[list[str]] = None,
    ) -> list[SingleTestResult]:
        """Run a full cross-implementation matrix (including self-tests)."""
        impls = load_implementations()

        # Support "all" to select every implementation
        if len(impl_names) == 1 and impl_names[0].lower() == "all":
            impl_names = list(impls.keys())

        selected = {}
        for name in impl_names:
            if name not in impls:
                logger.warning("Unknown implementation: %s (skipping)", name)
                continue
            selected[name] = impls[name]

        if len(selected) < 1:
            raise ValueError("Need at least 1 implementation for matrix test")

        scenario_list = scenarios or list(SCENARIOS.keys())

        # Compute test matrix pairs
        pairs: list[tuple[str, str]] = []
        for server_name, server_impl in selected.items():
            if not server_impl.can_be_server():
                continue
            for client_name, client_impl in selected.items():
                if not client_impl.can_be_client():
                    continue
                pairs.append((server_name, client_name))

        total_tests = len(pairs) * len(scenario_list)
        print(f"  Implementations: {', '.join(selected.keys())}")
        print(f"  Scenarios:       {len(scenario_list)}")
        print(f"  Pairs:           {len(pairs)} (server x client)")
        print(f"  Total tests:     {total_tests}")
        print()

        # Pre-pull/build all images so build time is excluded from test timing
        assert self.docker_mgr is not None
        print("  Preparing Docker images...")
        unavailable: list[str] = []
        for name, impl in list(selected.items()):
            logger.info("Ensuring image for %s: %s", name, impl.image)
            if self.docker_mgr.ensure_image(impl.image, impl.build_config, force_build=self.rebuild):
                print(f"    ✓ {name}: {impl.image}")
            else:
                print(f"    ✗ {name}: {impl.image} (unavailable, skipping)")
                unavailable.append(name)
                del selected[name]

        if unavailable:
            # Recompute pairs after removing unavailable implementations
            pairs = []
            for server_name, server_impl in selected.items():
                if not server_impl.can_be_server():
                    continue
                for client_name, client_impl in selected.items():
                    if not client_impl.can_be_client():
                        continue
                    pairs.append((server_name, client_name))
            total_tests = len(pairs) * len(scenario_list)
            print(f"\n  Skipped {len(unavailable)} unavailable: {', '.join(unavailable)}")
            print(f"  Continuing with {len(selected)} implementations, {total_tests} tests")

        if len(selected) < 1:
            raise ValueError("No implementations available (all image pulls failed)")
        print()

        results = []
        completed = 0

        for scenario_name in scenario_list:
            print(f"  === Scenario: {scenario_name} ===")
            for server_name, client_name in pairs:
                server_impl = selected[server_name]
                client_impl = selected[client_name]
                completed += 1
                logger.info(
                    "--- [%d/%d] %s [%s -> %s] ---",
                    completed, total_tests,
                    scenario_name, client_name, server_name,
                )
                r = self.run_test(scenario_name, server_impl, client_impl)
                results.append(r)
                self._log_result(r)
            print()

        return results

    def _log_result(self, r: SingleTestResult) -> None:
        symbol = {
            TestResult.PASSED: "\033[32m✓\033[0m",
            TestResult.FAILED: "\033[31m✗\033[0m",
            TestResult.UNSUPPORTED: "\033[33m⊘\033[0m",
            TestResult.SKIPPED: "\033[36m-\033[0m",
        }.get(r.result, "?")
        msg = f"  {symbol} {r.scenario}: {r.result.value} ({r.duration_s}s)"
        if r.error_message:
            msg += f" - {r.error_message}"
        print(msg)


# ---------------------------------------------------------------------------
# Output formatters
# ---------------------------------------------------------------------------

class ResultFormatter:
    """Formats test results for output."""

    def __init__(self, results: list[SingleTestResult]):
        self.results = results

    def _summary_stats(self) -> dict[str, int]:
        stats = {"total": 0, "passed": 0, "failed": 0, "unsupported": 0, "skipped": 0}
        for r in self.results:
            stats["total"] += 1
            if r.result == TestResult.PASSED:
                stats["passed"] += 1
            elif r.result == TestResult.FAILED:
                stats["failed"] += 1
            elif r.result == TestResult.UNSUPPORTED:
                stats["unsupported"] += 1
            elif r.result == TestResult.SKIPPED:
                stats["skipped"] += 1
        return stats

    def to_text(self) -> str:
        lines = []
        lines.append("")
        lines.append("=" * 72)
        lines.append("  QUIC Interoperability Test Results")
        lines.append("=" * 72)
        lines.append("")
        lines.append(f"  {'Scenario':<25} {'Server':<10} {'Client':<10} {'Status':<14} {'Time':>6}")
        lines.append("  " + "-" * 68)

        for r in self.results:
            status = r.result.value
            lines.append(
                f"  {r.scenario:<25} {r.server:<10} {r.client:<10} {status:<14} {r.duration_s:>5.1f}s"
            )

        stats = self._summary_stats()
        lines.append("")
        lines.append("  " + "-" * 68)
        lines.append(f"  Total: {stats['total']}  |  "
                      f"Passed: {stats['passed']}  |  "
                      f"Failed: {stats['failed']}  |  "
                      f"Unsupported: {stats['unsupported']}  |  "
                      f"Skipped: {stats['skipped']}")

        executed = stats["total"] - stats["skipped"]
        if executed > 0:
            pass_rate = stats["passed"] * 100 // executed
            lines.append(f"  Pass Rate: {pass_rate}% (excluding skipped)")
        lines.append("")

        return "\n".join(lines)

    def to_json(self) -> str:
        stats = self._summary_stats()
        data = {
            "metadata": {
                "timestamp": datetime.now(timezone.utc).isoformat(),
                "tool": "quicX interop runner",
            },
            "summary": stats,
            "results": [
                {
                    "scenario": r.scenario,
                    "server": r.server,
                    "client": r.client,
                    "result": r.result.value,
                    "duration_s": r.duration_s,
                    "error": r.error_message or None,
                }
                for r in self.results
            ],
        }
        return json.dumps(data, indent=2)

    def to_markdown(self) -> str:
        lines = []
        lines.append("# quicX QUIC Interoperability Test Results\n")
        lines.append(f"**Date:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")

        # Check if this is a matrix or a simple list
        servers = sorted(set(r.server for r in self.results))
        clients = sorted(set(r.client for r in self.results))
        scenarios = sorted(set(r.scenario for r in self.results),
                           key=lambda s: list(SCENARIOS.keys()).index(s)
                           if s in SCENARIOS else 999)

        if len(servers) > 1 or len(clients) > 1:
            # Matrix format: one table per scenario
            for scenario_name in scenarios:
                scenario_results = [r for r in self.results if r.scenario == scenario_name]
                lines.append(f"\n## {scenario_name}\n")

                # Build header
                header_clients = sorted(set(r.client for r in scenario_results))
                lines.append("| Server \\\\ Client | " + " | ".join(header_clients) + " |")
                lines.append("|---" + "|---" * len(header_clients) + "|")

                for server in sorted(set(r.server for r in scenario_results)):
                    row = [f"**{server}**"]
                    for client in header_clients:
                        match = next(
                            (r for r in scenario_results
                             if r.server == server and r.client == client),
                            None,
                        )
                        if match is None:
                            row.append("-")
                        elif match.result == TestResult.PASSED:
                            row.append("✅")
                        elif match.result == TestResult.FAILED:
                            row.append("❌")
                        elif match.result == TestResult.UNSUPPORTED:
                            row.append("⚠️")
                        else:
                            row.append("⏭️")
                    lines.append("| " + " | ".join(row) + " |")
        else:
            # Simple list format
            lines.append("| # | Scenario | Description | Status | Duration |")
            lines.append("|---|---------|-------------|--------|----------|")
            for i, r in enumerate(self.results, 1):
                desc = SCENARIOS[r.scenario].description if r.scenario in SCENARIOS else ""
                status_map = {
                    TestResult.PASSED: "✅ PASS",
                    TestResult.FAILED: "❌ FAIL",
                    TestResult.UNSUPPORTED: "⚠️ UNSUPPORTED",
                    TestResult.SKIPPED: "⏭️ SKIP",
                }
                status = status_map.get(r.result, "❓")
                lines.append(f"| {i} | {r.scenario} | {desc} | {status} | {r.duration_s}s |")

        stats = self._summary_stats()
        lines.append("\n## Summary\n")
        lines.append(f"- **Total:** {stats['total']}")
        lines.append(f"- **Passed:** {stats['passed']}")
        lines.append(f"- **Failed:** {stats['failed']}")
        lines.append(f"- **Unsupported:** {stats['unsupported']}")
        lines.append(f"- **Skipped:** {stats['skipped']}")

        executed = stats["total"] - stats["skipped"]
        if executed > 0:
            lines.append(f"- **Pass Rate:** {stats['passed'] * 100 // executed}%")
        lines.append("")

        return "\n".join(lines)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="quicX QUIC Interoperability Test Runner",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Local self-test (no Docker required)
  %(prog)s --local

  # Docker self-test (quicX vs quicX)
  %(prog)s

  # Cross-implementation test
  %(prog)s --server quicx --client quiche

  # Full matrix (selected)
  %(prog)s --matrix --implementations quicx,quiche,ngtcp2

  # Full matrix (all implementations)
  %(prog)s --matrix --implementations all

  # Specific scenario only
  %(prog)s --local --scenario handshake

  # JSON output
  %(prog)s --local --output json
""",
    )

    # Mode selection
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--local", action="store_true",
        help="Run locally using compiled binaries (no Docker)",
    )
    mode.add_argument(
        "--matrix", action="store_true",
        help="Run full cross-implementation matrix",
    )

    # Implementation selection
    parser.add_argument(
        "--server", type=str, default=None,
        help="Server implementation name (default: quicx)",
    )
    parser.add_argument(
        "--client", type=str, default=None,
        help="Client implementation name (default: quicx)",
    )
    parser.add_argument(
        "--implementations", type=str, default="quicx,quiche,ngtcp2",
        help="Comma-separated implementation list for matrix mode (use 'all' for every implementation)",
    )

    # Scenario selection
    parser.add_argument(
        "--scenario", type=str, default=None,
        help="Run only a specific scenario",
    )

    # Output options
    parser.add_argument(
        "--output", type=str, choices=["text", "json", "markdown"], default="text",
        help="Output format (default: text)",
    )
    parser.add_argument(
        "--output-file", type=str, default=None,
        help="Write results to file (in addition to stdout)",
    )

    # Configuration
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--host", type=str, default=DEFAULT_HOST)
    parser.add_argument("--timeout", type=int, default=CONTAINER_TIMEOUT)
    parser.add_argument("--build-dir", type=str, default=None)
    parser.add_argument(
        "--rebuild", action="store_true",
        help="Force rebuild Docker image even if it already exists",
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true",
        help="Enable verbose/debug logging",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()

    # Setup logging
    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(
        level=log_level,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
    )

    # Determine scenario list
    scenarios = [args.scenario] if args.scenario else None

    # Create runner
    build_dir = Path(args.build_dir) if args.build_dir else None
    runner = InteropTestRunner(
        local=args.local,
        port=args.port,
        host=args.host,
        timeout=args.timeout,
        build_dir=build_dir,
        rebuild=args.rebuild,
    )

    # Handle signals
    def signal_handler(sig, frame):
        logger.info("Interrupted, cleaning up...")
        runner.cleanup()
        sys.exit(130)

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    try:
        runner.setup()

        print()
        print("=" * 60)
        print("  quicX QUIC Interoperability Test Suite")
        print("=" * 60)
        print()

        if args.local:
            print(f"  Mode:   Local (binaries)")
            print(f"  Port:   {args.port}")
            runner.run_self_test(scenarios)

        elif args.matrix:
            impl_list = [s.strip() for s in args.implementations.split(",")]
            print(f"  Mode:   Matrix")
            print()
            runner.run_matrix(impl_list, scenarios)

        else:
            server_name = args.server or "quicx"
            client_name = args.client or "quicx"
            print(f"  Mode:   Docker")
            print(f"  Server: {server_name}")
            print(f"  Client: {client_name}")
            print()

            if server_name == client_name:
                runner.run_self_test(scenarios, impl_name=server_name)
            else:
                runner.run_cross_test(server_name, client_name, scenarios)

        # Format and output results
        formatter = ResultFormatter(runner.results)

        if args.output == "json":
            output = formatter.to_json()
        elif args.output == "markdown":
            output = formatter.to_markdown()
        else:
            output = formatter.to_text()

        print(output)

        # Print log directory location
        print(f"  Logs: {runner.log_dir}/")
        print()

        # Write to file if requested
        if args.output_file:
            Path(args.output_file).write_text(output)
            logger.info("Results written to: %s", args.output_file)

        # Return exit code based on failures
        stats = formatter._summary_stats()
        return 1 if stats["failed"] > 0 else 0

    except Exception as e:
        logger.error("Fatal error: %s", e)
        if args.verbose:
            import traceback
            traceback.print_exc()
        return 2

    finally:
        runner.cleanup()


if __name__ == "__main__":
    sys.exit(main())
