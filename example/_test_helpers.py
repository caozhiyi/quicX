"""
Shared helpers for example test scripts (run_test.py).

Provides:
- start_server(cmd, ...): Launch a server process in its own process group so
  the entire process tree can be reliably terminated (including any child
  processes the server might spawn) when a test runner times out or finishes.
- stop_server(process, ...): Terminate a server process group started via
  start_server, escalating to SIGKILL if it does not exit promptly.

Designed to work on POSIX systems (Linux/macOS). On Windows it falls back to
using CREATE_NEW_PROCESS_GROUP and process.terminate()/kill() semantics.
"""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import time
from typing import Sequence, Union

_IS_WINDOWS = sys.platform.startswith("win")


def start_server(
    cmd: Sequence[Union[str, os.PathLike]],
    *,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text: bool = False,
    cwd: Union[str, os.PathLike, None] = None,
    env=None,
    **popen_kwargs,
) -> subprocess.Popen:
    """Start a server subprocess in its own process group.

    Putting the server in a dedicated process group means we can later send a
    signal to the whole group with os.killpg(pgid, ...), which kills the
    server *and* any children it spawned. This avoids orphaned processes
    holding on to UDP/TCP ports between test runs.

    Parameters
    ----------
    cmd:
        Command line as a sequence (e.g. [server_binary, "--flag"]).
    stdout, stderr:
        Forwarded to subprocess.Popen. Default is PIPE so the test runner can
        capture output for diagnostics.
    text:
        If True, decode stdout/stderr as text using the default encoding.
    cwd, env:
        Forwarded to subprocess.Popen.
    **popen_kwargs:
        Any additional keyword arguments are forwarded verbatim to
        subprocess.Popen.

    Returns
    -------
    subprocess.Popen
        The running server process.
    """
    kwargs = dict(popen_kwargs)
    kwargs.setdefault("stdout", stdout)
    kwargs.setdefault("stderr", stderr)
    if text:
        kwargs["text"] = True
    if cwd is not None:
        kwargs["cwd"] = cwd
    if env is not None:
        kwargs["env"] = env

    if _IS_WINDOWS:
        # Use a new process group so we can send CTRL_BREAK_EVENT to it.
        creationflags = kwargs.get("creationflags", 0)
        creationflags |= getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
        kwargs["creationflags"] = creationflags
    else:
        # start_new_session=True calls setsid() in the child so it becomes
        # the leader of a new process group (and session). Its PID equals
        # the new pgid.
        kwargs.setdefault("start_new_session", True)

    return subprocess.Popen(list(cmd), **kwargs)


def _kill_process_group(process: subprocess.Popen, sig: int) -> None:
    """Best-effort send `sig` to the entire process group of `process`."""
    if process.poll() is not None:
        return
    try:
        if _IS_WINDOWS:
            # Windows: CTRL_BREAK_EVENT for graceful, terminate()/kill() else.
            if sig in (signal.SIGTERM, getattr(signal, "SIGINT", signal.SIGTERM)):
                try:
                    process.send_signal(signal.CTRL_BREAK_EVENT)  # type: ignore[attr-defined]
                    return
                except Exception:
                    pass
                process.terminate()
            else:
                process.kill()
            return

        pgid = os.getpgid(process.pid)
        os.killpg(pgid, sig)
    except (ProcessLookupError, OSError):
        # Process or group already gone.
        pass


def stop_server(
    process: subprocess.Popen,
    *,
    timeout: float = 3.0,
    kill_timeout: float = 2.0,
) -> int | None:
    """Stop a server started with `start_server`.

    Sends SIGTERM to the process group, waits up to `timeout` seconds, and
    escalates to SIGKILL if the process is still alive. Returns the process
    exit code (or None if it could not be reaped).
    """
    if process is None:
        return None

    # Already exited?
    if process.poll() is not None:
        return process.returncode

    # 1) Polite termination of the whole group.
    _kill_process_group(process, signal.SIGTERM)
    try:
        return process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        pass

    # 2) Escalate to SIGKILL.
    _kill_process_group(process, signal.SIGKILL)
    try:
        return process.wait(timeout=kill_timeout)
    except subprocess.TimeoutExpired:
        # Last resort: direct kill on the leader pid.
        try:
            process.kill()
        except Exception:
            pass
        try:
            return process.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            return None
    except Exception:
        return process.returncode


__all__ = ["start_server", "stop_server", "wait_for_udp_port"]


def wait_for_udp_port(
    port: int,
    *,
    timeout: float = 10.0,
    proc: subprocess.Popen | None = None,
    fallback_delay: float = 1.0,
) -> float:
    """Wait until a UDP socket is bound to `port` (best effort).

    Replaces the fixed ``sleep(1)`` callers used to do after spawning a QUIC
    server: a slow server startup (cert loading, cold page cache) occasionally
    raced that fixed delay, leaving the client to hit an unbound port. On
    Linux this polls /proc/net/udp{,6} for the port, so the client can start
    as soon as the server has actually bound. On platforms without /proc it
    falls back to sleeping ``fallback_delay`` seconds — the previous
    behaviour, no worse.

    If ``proc`` is supplied and it exits while we are waiting, raise
    ``RuntimeError`` immediately: the server crashed during startup and
    waiting longer is pointless.

    Returns the monotonic timestamp once the port is (assumed) ready.
    Raises ``TimeoutError`` if the port is not bound within ``timeout``.
    """
    deadline = time.monotonic() + timeout
    can_poll = (not _IS_WINDOWS) and os.path.exists("/proc/net/udp")
    needle = f"{port:04X}"

    def _check_proc() -> None:
        if proc is not None and proc.poll() is not None:
            raise RuntimeError(
                f"server exited during startup (code {proc.returncode})"
            )

    if not can_poll:
        time.sleep(fallback_delay)
        _check_proc()
        return time.monotonic()

    while True:
        _check_proc()
        for path in ("/proc/net/udp", "/proc/net/udp6"):
            try:
                with open(path, "r", errors="replace") as f:
                    for line in f:
                        cols = line.split()
                        # cols[1] is the local address, e.g. "0100007F:1B64".
                        if len(cols) > 1 and cols[1].split(":")[-1] == needle:
                            return time.monotonic()
            except OSError:
                continue
        if time.monotonic() >= deadline:
            raise TimeoutError(f"UDP port {port} not bound within {timeout}s")
        time.sleep(0.05)
