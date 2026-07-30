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


def test_linux_backend_file_policy_and_atomic_round_trip(tmp_path: Path) -> None:
    backend = LinuxTerminalBackend(
        root=tmp_path,
        allow_file_read=True,
        allow_file_write=True,
    )
    content = b"\x00\xffDOS-MCP"
    try:
        receipt = backend.upload_file(
            path="ROUNDTRP.BIN",
            data=content,
            overwrite=False,
        )
        downloaded = backend.download_file(path="ROUNDTRP.BIN")
        with pytest.raises(FileExistsError):
            backend.upload_file(
                path="ROUNDTRP.BIN",
                data=b"replacement",
                overwrite=False,
            )
    finally:
        backend.close()

    assert receipt.size == len(content)
    assert downloaded.data == content
    assert not list(tmp_path.glob("DOSMCP-*.TMP"))


def test_linux_backend_file_paths_cannot_escape_root(tmp_path: Path) -> None:
    backend = LinuxTerminalBackend(
        root=tmp_path,
        allow_file_read=True,
        allow_file_write=True,
    )
    try:
        with pytest.raises(ValueError, match="escapes"):
            backend.download_file(path="../outside")
        with pytest.raises(ValueError, match="relative"):
            backend.upload_file(path="/tmp/outside", data=b"x", overwrite=False)
    finally:
        backend.close()
