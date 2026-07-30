from pathlib import Path

import pytest

from dos_mcp.backends.linux import LinuxTerminalBackend


def test_linux_backend_runs_shell_and_captures_output(tmp_path: Path) -> None:
    backend = LinuxTerminalBackend(root=tmp_path)
    try:
        status = backend.get_status()
        receipt = backend.send_keys(
            text="printf 'DOS_MCP_READY\\n'",
            keys=("ENTER",),
            inter_key_delay_ms=0,
            settle_ms=300,
        )
        screen = backend.capture_screen()
    finally:
        backend.close()

    assert status.connected is True
    assert status.backend == "linux-terminal"
    assert receipt.accepted_keys == 1
    assert receipt.accepted_text_bytes > 0
    assert any("DOS_MCP_READY" in row for row in screen.text)
    assert screen.columns == 80
    assert screen.rows == 25


def test_linux_backend_rejects_unknown_key(tmp_path: Path) -> None:
    backend = LinuxTerminalBackend(root=tmp_path)
    try:
        with pytest.raises(ValueError, match="unsupported keys"):
            backend.send_keys(
                text="",
                keys=("POWER",),
                inter_key_delay_ms=0,
                settle_ms=0,
            )
    finally:
        backend.close()


def test_linux_backend_bounds_total_input_delay(tmp_path: Path) -> None:
    backend = LinuxTerminalBackend(root=tmp_path)
    try:
        with pytest.raises(ValueError, match="total inter-key delay"):
            backend.send_keys(
                text="x" * 31,
                keys=(),
                inter_key_delay_ms=1000,
                settle_ms=0,
            )
    finally:
        backend.close()


def test_linux_backend_close_is_idempotent(tmp_path: Path) -> None:
    backend = LinuxTerminalBackend(root=tmp_path)
    backend.get_status()

    backend.close()
    backend.close()

    with pytest.raises(RuntimeError, match="closed"):
        backend.capture_screen()
