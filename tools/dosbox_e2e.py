"""Exercise the real 16-bit foreground agent through a DOSBox UDP forward."""

from __future__ import annotations

import argparse
import time

from dos_mcp.backends.udp import UdpBackend
from dos_mcp.protocol import OPEN_MODE_KEY, derive_password_key, parse_key


def connect(target: tuple[str, int], key: bytes, deadline: float) -> UdpBackend:
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        backend = UdpBackend(target=target, key=key, timeout=0.5, retries=1)
        try:
            backend.get_status()
            return backend
        except (OSError, TimeoutError) as exc:
            last_error = exc
            backend.close()
            time.sleep(0.1)
    raise RuntimeError("DOS agent did not become ready") from last_error


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=21300)
    credentials = parser.add_mutually_exclusive_group()
    credentials.add_argument("--key")
    credentials.add_argument("--password", default="dosbox-test")
    args = parser.parse_args()

    if args.key is not None:
        key = parse_key(args.key)
    elif args.password is not None:
        key = derive_password_key(args.password)
    else:
        key = OPEN_MODE_KEY
    backend = connect(
        (args.host, args.port),
        key,
        time.monotonic() + 10,
    )
    try:
        status = backend.get_status()
        capabilities = backend.get_capabilities()
        before = backend.capture_screen()
        if not any("RAGENT>" in row for row in before.text):
            raise AssertionError("foreground agent prompt was not captured")
        receipt = backend.send_keys(
            text="VER",
            keys=("ENTER",),
            inter_key_delay_ms=5,
            settle_ms=500,
        )
        after = backend.capture_screen()
        if receipt.accepted_text_bytes != 3 or receipt.accepted_keys != 1:
            raise AssertionError(f"incomplete BIOS key receipt: {receipt}")
        if not any("Reported DOS version" in row for row in after.text):
            raise AssertionError("VER output was not captured")
        if after.adapter != "VGA":
            raise AssertionError(f"expected DOSBox VGA adapter, got {after.adapter}")
        if status.uptime_seconds > 30:
            raise AssertionError("agent uptime is not relative to agent start")
        print(
            "PASS: password-derived authentication, status, capabilities, "
            "fragmented VGA text capture, BIOS keys, and VER output"
        )
        print(
            f"DOS={status.operating_system} adapter={after.adapter} "
            f"screen={after.columns}x{after.rows} "
            f"keyboard={capabilities.keyboard_injection}"
        )
    finally:
        backend.close()


if __name__ == "__main__":
    main()
