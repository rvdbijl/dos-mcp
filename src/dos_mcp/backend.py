"""Logical target contract used by MCP handlers."""

from __future__ import annotations

from typing import Protocol

from .models import Capabilities, KeyReceipt, MachineStatus, TextScreen


class Backend(Protocol):
    """A transport-independent machine target."""

    def get_status(self) -> MachineStatus: ...

    def get_capabilities(self) -> Capabilities: ...

    def capture_screen(self) -> TextScreen: ...

    def send_keys(
        self,
        *,
        text: str,
        keys: tuple[str, ...],
        inter_key_delay_ms: int,
        settle_ms: int,
    ) -> KeyReceipt: ...

    def close(self) -> None: ...
