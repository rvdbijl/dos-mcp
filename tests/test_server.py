from __future__ import annotations

import base64
from typing import Any

import pytest
from mcp import Client

from dos_mcp.models import (
    Capabilities,
    Cursor,
    FileContents,
    FileReceipt,
    GraphicsScreen,
    KeyReceipt,
    MachineStatus,
    TextScreen,
)
from dos_mcp.server import create_server


class FakeBackend:
    def __init__(self) -> None:
        self.calls: list[tuple[str, dict[str, Any]]] = []
        self.closed = False

    def get_status(self) -> MachineStatus:
        return MachineStatus(
            connected=True,
            phase="agent_shell_ready",
            backend="fake",
            transport="memory",
            identity="test",
            operating_system="DOS 6.22",
            architecture="8088",
            agent_version="0.1.0",
            uptime_seconds=1.0,
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
            max_keys_per_request=128,
        )

    def capture_screen(self) -> TextScreen:
        return TextScreen(
            columns=4,
            rows=2,
            text=("C:\\>", "    "),
            attributes=((7, 7, 7, 7), (7, 7, 7, 7)),
            cursor=Cursor(0, 3),
            generation=1,
            adapter="CGA",
            video_mode=3,
            active_page=0,
            code_page="CP437",
            blink_enabled=True,
        )

    def capture_graphics(self) -> GraphicsScreen:
        return GraphicsScreen(
            adapter="CGA",
            video_mode=4,
            layout="cga-2bpp-interleaved",
            width=320,
            height=200,
            planes=1,
            bytes_per_plane=4,
            data=b"\x00\x55\xaa\xff",
            crc32=0x6C,
        )

    def download_file(self, *, path: str) -> FileContents:
        return FileContents(path, b"DOS", 0xEE)

    def upload_file(
        self,
        *,
        path: str,
        data: bytes,
        overwrite: bool,
    ) -> FileReceipt:
        self.calls.append(
            ("upload_file", {"path": path, "data": data, "overwrite": overwrite})
        )
        return FileReceipt(path, len(data), 0xAA)

    def send_keys(
        self,
        *,
        text: str,
        keys: tuple[str, ...],
        inter_key_delay_ms: int,
        settle_ms: int,
    ) -> KeyReceipt:
        self.calls.append(
            (
                "send_keys",
                {
                    "text": text,
                    "keys": keys,
                    "inter_key_delay_ms": inter_key_delay_ms,
                    "settle_ms": settle_ms,
                },
            )
        )
        return KeyReceipt(len(text.encode()), len(keys), keys, 2)

    def close(self) -> None:
        self.closed = True


@pytest.mark.anyio
async def test_server_lists_tsr_tools() -> None:
    server = create_server(FakeBackend())
    async with Client(server, raise_exceptions=True) as client:
        result = await client.list_tools()

    assert {tool.name for tool in result.tools} == {
        "dos.list_targets",
        "dos.get_status",
        "dos.get_capabilities",
        "dos.capture_screen",
        "dos.capture_graphics",
        "dos.send_keys",
        "dos.download_file",
        "dos.upload_file",
    }


@pytest.mark.anyio
async def test_server_accepts_explicit_target_selector() -> None:
    server = create_server(FakeBackend())
    async with Client(server, raise_exceptions=True) as client:
        listed = await client.call_tool("dos.list_targets")
        status = await client.call_tool("dos.get_status", {"target": "default"})

    assert listed.structured_content["targets"][0]["selector"] == "default"
    assert status.structured_content["connected"] is True


@pytest.mark.anyio
async def test_capture_screen_returns_structured_cells() -> None:
    server = create_server(FakeBackend())
    async with Client(server, raise_exceptions=True) as client:
        result = await client.call_tool("dos.capture_screen")

    assert result.is_error is False
    assert result.structured_content["text"] == ["C:\\>", "    "]
    assert result.structured_content["cursor"]["column"] == 3


@pytest.mark.anyio
async def test_send_keys_preserves_order_and_parameters() -> None:
    backend = FakeBackend()
    server = create_server(backend)
    async with Client(server, raise_exceptions=True) as client:
        result = await client.call_tool(
            "dos.send_keys",
            {
                "text": "DIR",
                "keys": ["ENTER"],
                "inter_key_delay_ms": 5,
                "settle_ms": 25,
            },
        )

    assert result.is_error is False
    assert backend.calls == [
        (
            "send_keys",
            {
                "text": "DIR",
                "keys": ("ENTER",),
                "inter_key_delay_ms": 5,
                "settle_ms": 25,
            },
        )
    ]


@pytest.mark.anyio
async def test_file_tools_encode_and_decode_binary_data() -> None:
    backend = FakeBackend()
    server = create_server(backend)
    async with Client(server, raise_exceptions=True) as client:
        downloaded = await client.call_tool(
            "dos.download_file", {"path": "RESULT.BIN"}
        )
        uploaded = await client.call_tool(
            "dos.upload_file",
            {
                "path": "INPUT.BIN",
                "content_base64": base64.b64encode(b"\x00\xff").decode(),
                "overwrite": True,
            },
        )

    assert downloaded.structured_content["content_base64"] == "RE9T"
    assert uploaded.structured_content["size"] == 2
    assert backend.calls[-1] == (
        "upload_file",
        {"path": "INPUT.BIN", "data": b"\x00\xff", "overwrite": True},
    )
