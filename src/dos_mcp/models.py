"""Transport-independent values exchanged between MCP tools and backends."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Any


@dataclass(frozen=True, slots=True)
class Cursor:
    row: int
    column: int
    visible: bool = True
    start_scanline: int | None = None
    end_scanline: int | None = None

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True, slots=True)
class TextScreen:
    columns: int
    rows: int
    text: tuple[str, ...]
    attributes: tuple[tuple[int, ...], ...]
    cursor: Cursor
    generation: int
    adapter: str
    video_mode: int | None
    active_page: int
    code_page: str
    blink_enabled: bool

    def __post_init__(self) -> None:
        if self.columns < 1 or self.rows < 1:
            raise ValueError("screen dimensions must be positive")
        if len(self.text) != self.rows:
            raise ValueError("screen row count does not match metadata")
        if len(self.attributes) != self.rows:
            raise ValueError("attribute row count does not match metadata")
        if any(len(row) != self.columns for row in self.text):
            raise ValueError("text rows must retain exactly columns characters")
        if any(len(row) != self.columns for row in self.attributes):
            raise ValueError("attribute rows must contain exactly columns cells")
        if not (0 <= self.cursor.row < self.rows):
            raise ValueError("cursor row is outside the screen")
        if not (0 <= self.cursor.column < self.columns):
            raise ValueError("cursor column is outside the screen")
        if any(not 0 <= value <= 0xFF for row in self.attributes for value in row):
            raise ValueError("cell attributes must be bytes")

    def to_dict(self) -> dict[str, Any]:
        return {
            "kind": "text",
            "columns": self.columns,
            "rows": self.rows,
            "text": list(self.text),
            "attributes": [list(row) for row in self.attributes],
            "cursor": self.cursor.to_dict(),
            "generation": self.generation,
            "adapter": self.adapter,
            "video_mode": self.video_mode,
            "active_page": self.active_page,
            "code_page": self.code_page,
            "blink_enabled": self.blink_enabled,
        }


@dataclass(frozen=True, slots=True)
class Capabilities:
    backend: str
    transport: str
    status: bool
    text_capture: bool
    graphics_capture: tuple[str, ...]
    keyboard_injection: str | None
    screen_columns: int
    screen_rows: int
    max_text_bytes: int
    max_keys_per_request: int
    filesystem_read: bool = False
    filesystem_write: bool = False
    command_execution: bool = False
    memory_read: bool = False
    memory_write: bool = False
    port_read: bool = False
    port_write: bool = False
    reboot: bool = False

    def to_dict(self) -> dict[str, Any]:
        value = asdict(self)
        value["graphics_capture"] = list(self.graphics_capture)
        return value


@dataclass(frozen=True, slots=True)
class MachineStatus:
    connected: bool
    phase: str
    backend: str
    transport: str
    identity: str
    operating_system: str
    architecture: str
    agent_version: str
    uptime_seconds: float

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True, slots=True)
class KeyReceipt:
    accepted_text_bytes: int
    accepted_keys: int
    keys: tuple[str, ...]
    screen_generation: int

    def to_dict(self) -> dict[str, Any]:
        value = asdict(self)
        value["keys"] = list(self.keys)
        return value
