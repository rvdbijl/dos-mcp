"""Linux PTY target used to develop the MCP bridge without DOS hardware."""

from __future__ import annotations

import atexit
import fcntl
import os
import platform
import select
import signal
import struct
import subprocess
import termios
import time
from contextlib import suppress
from pathlib import Path

from dos_mcp import __version__
from dos_mcp.models import Capabilities, KeyReceipt, MachineStatus, TextScreen
from dos_mcp.terminal import TerminalBuffer

MAX_TEXT_BYTES = 4096
MAX_KEYS = 128
MAX_SEND_DELAY_MS = 30_000
MAX_DRAIN_BYTES = 1_048_576

KEY_SEQUENCES = {
    "ENTER": b"\r",
    "ESC": b"\x1b",
    "TAB": b"\t",
    "BACKSPACE": b"\x7f",
    "CTRL_C": b"\x03",
    "CTRL_D": b"\x04",
    "UP": b"\x1b[A",
    "DOWN": b"\x1b[B",
    "RIGHT": b"\x1b[C",
    "LEFT": b"\x1b[D",
    "HOME": b"\x1b[H",
    "END": b"\x1b[F",
    "DELETE": b"\x1b[3~",
    "PAGE_UP": b"\x1b[5~",
    "PAGE_DOWN": b"\x1b[6~",
    "F1": b"\x1bOP",
    "F2": b"\x1bOQ",
    "F3": b"\x1bOR",
    "F4": b"\x1bOS",
    "F5": b"\x1b[15~",
    "F6": b"\x1b[17~",
    "F7": b"\x1b[18~",
    "F8": b"\x1b[19~",
    "F9": b"\x1b[20~",
    "F10": b"\x1b[21~",
    "F11": b"\x1b[23~",
    "F12": b"\x1b[24~",
}


class LinuxTerminalBackend:
    """Expose a Linux interactive shell as a bounded text-console target."""

    def __init__(
        self,
        *,
        root: Path,
        shell: str = "/bin/sh",
        columns: int = 80,
        rows: int = 25,
    ) -> None:
        resolved_root = root.expanduser().resolve()
        if not resolved_root.is_dir():
            raise ValueError(f"backend root is not a directory: {resolved_root}")
        if not Path(shell).is_absolute():
            raise ValueError("shell must be an absolute path")
        if not Path(shell).is_file():
            raise ValueError(f"shell does not exist: {shell}")

        self.root = resolved_root
        self.shell = shell
        self.columns = columns
        self.rows = rows
        self._terminal = TerminalBuffer(columns=columns, rows=rows)
        self._master_fd: int | None = None
        self._process: subprocess.Popen[bytes] | None = None
        self._started_at = time.monotonic()
        self._closed = False
        atexit.register(self.close)

    def get_status(self) -> MachineStatus:
        self._ensure_started()
        process = self._process
        running = process is not None and process.poll() is None
        uname = platform.uname()
        return MachineStatus(
            connected=running,
            phase="agent_shell_ready" if running else "host_unresponsive",
            backend="linux-terminal",
            transport="local-pty",
            identity=uname.node or "localhost",
            operating_system=f"{uname.system} {uname.release}",
            architecture=uname.machine,
            agent_version=__version__,
            uptime_seconds=round(time.monotonic() - self._started_at, 3),
        )

    def get_capabilities(self) -> Capabilities:
        self._ensure_started()
        return Capabilities(
            backend="linux-terminal",
            transport="local-pty",
            status=True,
            text_capture=True,
            graphics_capture=(),
            keyboard_injection="terminal-sequences",
            screen_columns=self.columns,
            screen_rows=self.rows,
            max_text_bytes=MAX_TEXT_BYTES,
            max_keys_per_request=MAX_KEYS,
        )

    def capture_screen(self) -> TextScreen:
        self._ensure_started()
        self._drain(0.05)
        return self._terminal.snapshot()

    def send_keys(
        self,
        *,
        text: str,
        keys: tuple[str, ...],
        inter_key_delay_ms: int,
        settle_ms: int,
    ) -> KeyReceipt:
        self._ensure_started()
        encoded = text.encode("utf-8")
        if len(encoded) > MAX_TEXT_BYTES:
            raise ValueError(f"text exceeds {MAX_TEXT_BYTES} encoded bytes")
        if len(keys) > MAX_KEYS:
            raise ValueError(f"keys exceeds {MAX_KEYS} items")
        if not 0 <= inter_key_delay_ms <= 1000:
            raise ValueError("inter_key_delay_ms must be between 0 and 1000")
        if not 0 <= settle_ms <= 2000:
            raise ValueError("settle_ms must be between 0 and 2000")
        delayed_items = len(encoded) + len(keys)
        if delayed_items * inter_key_delay_ms > MAX_SEND_DELAY_MS:
            raise ValueError(f"total inter-key delay exceeds {MAX_SEND_DELAY_MS} milliseconds")

        normalized = tuple(key.upper() for key in keys)
        unknown = [key for key in normalized if key not in KEY_SEQUENCES]
        if unknown:
            allowed = ", ".join(sorted(KEY_SEQUENCES))
            raise ValueError(f"unsupported keys {unknown!r}; allowed keys: {allowed}")

        delay = inter_key_delay_ms / 1000
        if encoded:
            if delay:
                for byte in encoded:
                    self._write(bytes((byte,)))
                    time.sleep(delay)
            else:
                self._write(encoded)
        for key in normalized:
            self._write(KEY_SEQUENCES[key])
            if delay:
                time.sleep(delay)

        self._drain(settle_ms / 1000)
        return KeyReceipt(
            accepted_text_bytes=len(encoded),
            accepted_keys=len(normalized),
            keys=normalized,
            screen_generation=self._terminal.generation,
        )

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        process = self._process
        self._process = None
        if process is not None and process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=0.5)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                if process.poll() is None:
                    with suppress(ProcessLookupError):
                        os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=0.5)
        if self._master_fd is not None:
            os.close(self._master_fd)
            self._master_fd = None

    def _ensure_started(self) -> None:
        if self._closed:
            raise RuntimeError("backend is closed")
        if self._process is not None:
            return

        master_fd, slave_fd = os.openpty()
        size = struct.pack("HHHH", self.rows, self.columns, 0, 0)
        fcntl.ioctl(slave_fd, termios.TIOCSWINSZ, size)
        flags = fcntl.fcntl(master_fd, fcntl.F_GETFL)
        fcntl.fcntl(master_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)

        environment = {
            "HOME": str(self.root),
            "PATH": "/usr/local/bin:/usr/bin:/bin",
            "TERM": "dumb",
            "LANG": "C.UTF-8",
            "LC_ALL": "C.UTF-8",
            "PS1": "LINUX> ",
        }
        try:
            self._process = subprocess.Popen(
                [self.shell, "-i"],
                stdin=slave_fd,
                stdout=slave_fd,
                stderr=slave_fd,
                cwd=self.root,
                env=environment,
                close_fds=True,
                start_new_session=True,
            )
        except BaseException:
            os.close(master_fd)
            raise
        finally:
            os.close(slave_fd)
        self._master_fd = master_fd
        self._drain(0.15)

    def _write(self, value: bytes) -> None:
        if self._master_fd is None:
            raise RuntimeError("backend PTY is not available")
        offset = 0
        while offset < len(value):
            try:
                offset += os.write(self._master_fd, value[offset:])
            except BlockingIOError:
                select.select([], [self._master_fd], [], 0.1)

    def _drain(self, timeout: float) -> None:
        if self._master_fd is None:
            return
        deadline = time.monotonic() + timeout
        consumed = 0
        while time.monotonic() < deadline and consumed < MAX_DRAIN_BYTES:
            remaining = max(0.0, deadline - time.monotonic())
            readable, _, _ = select.select([self._master_fd], [], [], remaining)
            if not readable:
                break
            while time.monotonic() < deadline and consumed < MAX_DRAIN_BYTES:
                try:
                    data = os.read(self._master_fd, 65536)
                except BlockingIOError:
                    break
                except OSError:
                    return
                if not data:
                    return
                self._terminal.feed_bytes(data)
                consumed += len(data)
