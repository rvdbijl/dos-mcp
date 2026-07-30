"""Logical target contract used by MCP handlers."""

from __future__ import annotations

from typing import Protocol

from .models import (
    Capabilities,
    FileContents,
    FileReceipt,
    GraphicsScreen,
    KeyReceipt,
    MachineStatus,
    TextScreen,
)


class Backend(Protocol):
    """A transport-independent machine target."""

    def get_status(self) -> MachineStatus: ...

    def get_capabilities(self) -> Capabilities: ...

    def capture_screen(self) -> TextScreen: ...

    def capture_graphics(self) -> GraphicsScreen: ...

    def send_keys(
        self,
        *,
        text: str,
        keys: tuple[str, ...],
        inter_key_delay_ms: int,
        settle_ms: int,
    ) -> KeyReceipt: ...

    def download_file(self, *, path: str) -> FileContents: ...

    def upload_file(
        self,
        *,
        path: str,
        data: bytes,
        overwrite: bool,
    ) -> FileReceipt: ...

    def close(self) -> None: ...
