"""Exercise RA-TSR through DOSBox-X, including file transfer and unload."""

from __future__ import annotations

import time
from contextlib import suppress

from dos_mcp.backends.udp import AgentOperationError, UdpBackend
from dos_mcp.protocol import Opcode, ResidentDiagnosticFlag, derive_password_key


def connect(deadline: float) -> UdpBackend:
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        backend = UdpBackend(
            target=("127.0.0.1", 21300),
            key=derive_password_key("dosbox-test"),
            timeout=0.5,
            retries=1,
            allow_file_read=True,
            allow_file_write=True,
        )
        try:
            backend.get_status()
            return backend
        except (OSError, TimeoutError) as exc:
            last_error = exc
            backend.close()
            time.sleep(0.1)
    raise RuntimeError("RA-TSR did not become ready") from last_error


def type_all(backend: UdpBackend, text: str, *, enter: bool = False) -> None:
    remaining = text
    stalled = 0
    while remaining:
        receipt = backend.send_keys(
            text=remaining,
            keys=(),
            inter_key_delay_ms=0,
            settle_ms=100,
        )
        if receipt.accepted_text_bytes == 0:
            stalled += 1
            if stalled == 10:
                raise AssertionError("BIOS keyboard queue did not accept input")
            time.sleep(0.1)
            continue
        stalled = 0
        remaining = remaining[receipt.accepted_text_bytes :]
    if enter:
        backend.send_keys(
            text="",
            keys=("ENTER",),
            inter_key_delay_ms=0,
            settle_ms=500,
        )


def main() -> None:
    backend = connect(time.monotonic() + 10)
    backend._timeout = 5.0
    content = bytes(range(256)) * 10
    try:
        status = backend.get_status()
        capabilities = backend.get_capabilities()
        if status.phase != "observe_ready":
            raise AssertionError(f"unexpected TSR phase: {status.phase}")
        if not capabilities.filesystem_read or not capabilities.filesystem_write:
            raise AssertionError("TSR file capabilities were not advertised")
        if not capabilities.graphics_capture:
            raise AssertionError("TSR graphics capability was not advertised")
        diagnostics = backend.get_resident_diagnostics()
        expected_vectors = (
            ResidentDiagnosticFlag.OWNS_INT08
            | ResidentDiagnosticFlag.OWNS_INT1C
            | ResidentDiagnosticFlag.OWNS_INT28
            | ResidentDiagnosticFlag.OWNS_INT2F
        )
        if diagnostics.flags & expected_vectors != expected_vectors:
            raise AssertionError("TSR does not own all installed interrupt vectors")
        backend.send_keys(
            text="",
            keys=(),
            inter_key_delay_ms=0,
            settle_ms=0,
        )
        if backend._request(Opcode.PING, b"x" * 32) != b"x" * 32:
            raise AssertionError("32-byte TSR response failed")
        if backend._request(Opcode.PING, b"y" * 256) != b"y" * 256:
            raise AssertionError("256-byte TSR response failed")
        before = backend.capture_screen()
        if not any("TSRHOST>" in row for row in before.text):
            raise AssertionError("foreground TSR test host was not captured")
        type_all(backend, "VER", enter=True)
        after = backend.capture_screen()
        if not any("DOS version" in row for row in after.text):
            raise AssertionError("VER output was not captured through the TSR")

        type_all(backend, "CUT1C", enter=True)
        cut_before = backend.get_resident_diagnostics()
        time.sleep(0.25)
        if backend._request(Opcode.PING, b"watchdog") != b"watchdog":
            raise AssertionError("INT 08h watchdog did not service a request")
        cut_after = backend.get_resident_diagnostics()
        if cut_after.flags & ResidentDiagnosticFlag.OWNS_INT1C:
            raise AssertionError("INT 1Ch suppression fixture did not take ownership")
        if not cut_after.flags & ResidentDiagnosticFlag.OWNS_INT08:
            raise AssertionError("RA-TSR lost INT 08h during watchdog test")
        if cut_after.int1c_entries != cut_before.int1c_entries:
            raise AssertionError("suppressed INT 1Ch unexpectedly continued")
        if cut_after.fallback_runs <= cut_before.fallback_runs:
            raise AssertionError("INT 08h watchdog fallback did not run")
        type_all(backend, "REST1C", enter=True)
        restored = backend.get_resident_diagnostics()
        if not restored.flags & ResidentDiagnosticFlag.OWNS_INT1C:
            raise AssertionError("INT 1Ch was not restored after watchdog test")

        type_all(backend, "GFX13", enter=True)
        time.sleep(0.2)
        graphics = backend.capture_graphics()
        expected_graphics = bytes(
            (index ^ (index >> 8)) & 0xFF for index in range(64000)
        )
        if (
            graphics.video_mode != 0x13
            or graphics.layout != "packed-8bpp"
            or graphics.width != 320
            or graphics.height != 200
            or graphics.data != expected_graphics
        ):
            raise AssertionError("VGA mode 13h framebuffer capture failed")
        type_all(backend, "TEXT", enter=True)
        time.sleep(0.2)

        try:
            backend.download_file(path="ROUNDTRP.BIN")
        except AgentOperationError:
            pass
        else:
            raise AssertionError("ALL mode accepted a relative DOS path")
        path = "C:\\REMOTE\\ROUNDTRP.BIN"
        receipt = backend.upload_file(
            path=path,
            data=content,
            overwrite=True,
        )
        downloaded = backend.download_file(path=path)
        if receipt.size != len(content) or downloaded.data != content:
            raise AssertionError("TSR binary file round trip failed")

        type_all(backend, "EXIT")
        # TSRHOST exits as soon as it consumes ENTER. The enclosing batch
        # immediately unloads RA-TSR, so its key receipt may intentionally
        # disappear with the resident network endpoint.
        with suppress(TimeoutError):
            backend.send_keys(
                text="",
                keys=("ENTER",),
                inter_key_delay_ms=0,
                settle_ms=0,
            )
        print(
            "PASS: resident status, INT08 watchdog, text/VGA capture, BIOS keys, "
            "binary upload/download, and unload"
        )
    finally:
        backend.close()


if __name__ == "__main__":
    main()
