from __future__ import annotations

import threading
from typing import Any

import pytest

from dos_mcp.agent_server import UdpAgentServer
from dos_mcp.backends.udp import UdpBackend
from dos_mcp.models import Capabilities, Cursor, KeyReceipt, MachineStatus, TextScreen
from dos_mcp.protocol import OPEN_MODE_KEY

KEY = bytes.fromhex("00112233445566778899aabbccddeeff")


class FakeBackend:
    def __init__(self) -> None:
        self.key_calls: list[dict[str, Any]] = []
        self.closed = False

    def get_status(self) -> MachineStatus:
        return MachineStatus(
            connected=True,
            phase="observe_ready",
            backend="fake",
            transport="memory",
            identity="fake-dos",
            operating_system="DOS 6.22",
            architecture="8088",
            agent_version="0.1",
            uptime_seconds=10,
        )

    def get_capabilities(self) -> Capabilities:
        return Capabilities(
            backend="fake",
            transport="memory",
            status=True,
            text_capture=True,
            graphics_capture=(),
            keyboard_injection="bios-queue",
            screen_columns=80,
            screen_rows=25,
            max_text_bytes=4096,
            max_keys_per_request=15,
        )

    def capture_screen(self) -> TextScreen:
        first = "C:\\>DIR".ljust(80)
        return TextScreen(
            columns=80,
            rows=25,
            text=(first,) + (" " * 80,) * 24,
            attributes=((7,) * 80,) * 25,
            cursor=Cursor(0, 7, True, 6, 7),
            generation=3,
            adapter="CGA",
            video_mode=3,
            active_page=0,
            code_page="CP437",
            blink_enabled=True,
        )

    def send_keys(
        self,
        *,
        text: str,
        keys: tuple[str, ...],
        inter_key_delay_ms: int,
        settle_ms: int,
    ) -> KeyReceipt:
        self.key_calls.append(
            {
                "text": text,
                "keys": keys,
                "inter_key_delay_ms": inter_key_delay_ms,
                "settle_ms": settle_ms,
            }
        )
        return KeyReceipt(len(text.encode("cp437")), len(keys), keys, 4)

    def close(self) -> None:
        self.closed = True


class RunningServer:
    def __init__(
        self,
        *,
        key: bytes = KEY,
        drop_first_response: bool = False,
    ) -> None:
        self.backend = FakeBackend()
        self.server = UdpAgentServer(
            self.backend,
            key=key,
            bind=("127.0.0.1", 0),
            nonce_factory=lambda: 0x12345678,
            drop_first_response=drop_first_response,
        )
        self.stop = threading.Event()
        self.thread = threading.Thread(
            target=self.server.serve_forever,
            args=(self.stop,),
            daemon=True,
        )

    def __enter__(self) -> RunningServer:
        self.thread.start()
        return self

    def __exit__(self, *_: object) -> None:
        self.stop.set()
        self.thread.join(timeout=1)
        self.server.close()


def test_udp_backend_status_capabilities_and_fragmented_screen() -> None:
    with RunningServer() as running:
        backend = UdpBackend(target=running.server.address, key=KEY)
        try:
            status = backend.get_status()
            capabilities = backend.get_capabilities()
            screen = backend.capture_screen()
        finally:
            backend.close()

    assert status.connected is True
    assert status.transport == "packet-driver-udp"
    assert capabilities.text_capture is True
    assert capabilities.keyboard_injection == "bios-queue"
    assert screen.text[0].startswith("C:\\>DIR")
    assert screen.cursor.column == 7
    assert screen.adapter == "CGA"


def test_udp_backend_operates_with_public_open_mode_key() -> None:
    with RunningServer(key=OPEN_MODE_KEY) as running:
        backend = UdpBackend(target=running.server.address, key=OPEN_MODE_KEY)
        try:
            status = backend.get_status()
        finally:
            backend.close()

    assert status.connected is True


def test_udp_retry_does_not_repeat_keyboard_mutation() -> None:
    with RunningServer(drop_first_response=True) as running:
        backend = UdpBackend(
            target=running.server.address,
            key=KEY,
            timeout=0.05,
            retries=2,
        )
        try:
            receipt = backend.send_keys(
                text="DIR",
                keys=("ENTER",),
                inter_key_delay_ms=5,
                settle_ms=100,
            )
        finally:
            backend.close()

    assert receipt.accepted_text_bytes == 3
    assert running.backend.key_calls == [
        {
            "text": "DIR",
            "keys": ("ENTER",),
            "inter_key_delay_ms": 5,
            "settle_ms": 100,
        }
    ]


def test_udp_backend_with_wrong_key_times_out() -> None:
    with RunningServer() as running:
        backend = UdpBackend(
            target=running.server.address,
            key=bytes(reversed(KEY)),
            timeout=0.03,
            retries=0,
        )
        try:
            with pytest.raises(TimeoutError, match="HELLO"):
                backend.get_status()
        finally:
            backend.close()
