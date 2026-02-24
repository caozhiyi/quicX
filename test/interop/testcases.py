"""
QUIC Interop Test Scenarios Definition

Based on: https://github.com/quic-interop/quic-interop-runner
Reference: https://interop.seemann.io/
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Optional


class TestResult(Enum):
    PASSED = "PASSED"
    FAILED = "FAILED"
    UNSUPPORTED = "UNSUPPORTED"
    SKIPPED = "SKIPPED"


@dataclass
class TestScenario:
    """Definition of a single interop test scenario."""
    name: str
    description: str
    files: list[str]
    server_env: dict[str, str] = field(default_factory=dict)
    client_env: dict[str, str] = field(default_factory=dict)
    needs_two_connections: bool = False
    concurrent_clients: int = 0


# ---------------------------------------------------------------------------
# All 14 official QUIC Interop Runner test scenarios
# ---------------------------------------------------------------------------

SCENARIOS: dict[str, TestScenario] = {
    "handshake": TestScenario(
        name="handshake",
        description="Basic QUIC handshake completion",
        files=["1KB.bin"],
    ),
    "transfer": TestScenario(
        name="transfer",
        description="Flow control and stream multiplexing",
        files=["1MB.bin", "5MB.bin"],
    ),
    "retry": TestScenario(
        name="retry",
        description="Server Retry packet handling",
        files=["1KB.bin"],
        server_env={"FORCE_RETRY": "1"},
    ),
    "resumption": TestScenario(
        name="resumption",
        description="Session resumption without 0-RTT",
        files=["1KB.bin"],
        needs_two_connections=True,
    ),
    "zerortt": TestScenario(
        name="zerortt",
        description="Zero round-trip time data transfer",
        files=["1KB.bin"],
        needs_two_connections=True,
    ),
    "http3": TestScenario(
        name="http3",
        description="HTTP/3 protocol functionality",
        files=["10KB.bin", "100KB.bin", "1MB.bin"],
    ),
    "multiconnect": TestScenario(
        name="multiconnect",
        description="Handshake resilience under packet loss",
        files=["1KB.bin"],
        concurrent_clients=5,
    ),
    "versionnegotiation": TestScenario(
        name="versionnegotiation",
        description="Version negotiation protocol",
        files=["1KB.bin"],
        client_env={"PREFERRED_VERSION": "0x1a2a3a4a"},
    ),
    "chacha20": TestScenario(
        name="chacha20",
        description="ChaCha20-Poly1305 cipher suite",
        files=["5MB.bin"],
        server_env={"CIPHER_SUITES": "TLS_CHACHA20_POLY1305_SHA256"},
        client_env={"CIPHER_SUITES": "TLS_CHACHA20_POLY1305_SHA256"},
    ),
    "keyupdate": TestScenario(
        name="keyupdate",
        description="TLS key update mechanism",
        files=["2MB.bin"],
        client_env={"FORCE_KEY_UPDATE": "1"},
    ),
    "v2": TestScenario(
        name="v2",
        description="QUIC version 2 support (RFC 9369)",
        files=["1KB.bin"],
        server_env={"QUIC_VERSION": "0x6b3343cf"},
        client_env={"QUIC_VERSION": "0x6b3343cf"},
    ),
    "rebind-port": TestScenario(
        name="rebind-port",
        description="NAT port rebinding (path validation)",
        files=["5MB.bin"],
    ),
    "rebind-addr": TestScenario(
        name="rebind-addr",
        description="NAT address rebinding (path validation)",
        files=["5MB.bin"],
    ),
    "connectionmigration": TestScenario(
        name="connectionmigration",
        description="Active connection migration",
        files=["5MB.bin"],
    ),
}

# Files sizes needed for test generation (name -> bytes)
TEST_FILE_SIZES: dict[str, int] = {
    "32B.bin":    32,
    "1KB.bin":    1024,
    "5KB.bin":    5120,
    "10KB.bin":   10240,
    "100KB.bin":  102400,
    "500KB.bin":  512000,
    "1MB.bin":    1048576,
    "2MB.bin":    2097152,
    "3MB.bin":    3145728,
    "5MB.bin":    5242880,
    "10MB.bin":   10485760,
}
