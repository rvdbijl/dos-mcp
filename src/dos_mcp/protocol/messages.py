"""Operation payload codecs with strict, bounded validation."""

from __future__ import annotations

import struct
from dataclasses import dataclass

from .constants import (
    MAX_FRAGMENT_PAYLOAD,
    Adapter,
    Capability,
    KeyCode,
    Phase,
)

_HELLO_REQUEST = struct.Struct("<I")
_HELLO_RESPONSE = struct.Struct("<IIHH")
_STATUS = struct.Struct("<BBBBBBHI")
_CAPABILITIES = struct.Struct("<IBBBHH")
_SCREEN = struct.Struct("<BBBBBBBBBHH")
_KEY_REQUEST = struct.Struct("<HBH")
_KEY_RESPONSE = struct.Struct("<HBH")


@dataclass(frozen=True, slots=True)
class HelloRequest:
    client_nonce: int

    def encode(self) -> bytes:
        return _HELLO_REQUEST.pack(self.client_nonce)

    @classmethod
    def decode(cls, payload: bytes) -> HelloRequest:
        _require_size(payload, _HELLO_REQUEST.size, "hello request")
        return cls(*_HELLO_REQUEST.unpack(payload))


@dataclass(frozen=True, slots=True)
class HelloResponse:
    client_nonce: int
    server_nonce: int
    session_id: int
    max_fragment_payload: int = MAX_FRAGMENT_PAYLOAD

    def encode(self) -> bytes:
        return _HELLO_RESPONSE.pack(
            self.client_nonce,
            self.server_nonce,
            self.session_id,
            self.max_fragment_payload,
        )

    @classmethod
    def decode(cls, payload: bytes) -> HelloResponse:
        _require_size(payload, _HELLO_RESPONSE.size, "hello response")
        value = cls(*_HELLO_RESPONSE.unpack(payload))
        if not 64 <= value.max_fragment_payload <= MAX_FRAGMENT_PAYLOAD:
            raise ValueError("invalid negotiated fragment payload")
        if value.session_id == 0:
            raise ValueError("session ID zero is reserved")
        return value


@dataclass(frozen=True, slots=True)
class StatusMessage:
    agent_major: int
    agent_minor: int
    dos_major: int
    dos_minor: int
    cpu_class: int
    phase: Phase
    conventional_kb: int
    bios_ticks: int

    def encode(self) -> bytes:
        return _STATUS.pack(
            self.agent_major,
            self.agent_minor,
            self.dos_major,
            self.dos_minor,
            self.cpu_class,
            int(self.phase),
            self.conventional_kb,
            self.bios_ticks,
        )

    @classmethod
    def decode(cls, payload: bytes) -> StatusMessage:
        _require_size(payload, _STATUS.size, "status")
        values = list(_STATUS.unpack(payload))
        values[5] = Phase(values[5])
        return cls(*values)


@dataclass(frozen=True, slots=True)
class CapabilitiesMessage:
    capabilities: Capability
    columns: int
    rows: int
    adapter: Adapter
    max_fragment_payload: int
    max_keys: int

    def encode(self) -> bytes:
        return _CAPABILITIES.pack(
            int(self.capabilities),
            self.columns,
            self.rows,
            int(self.adapter),
            self.max_fragment_payload,
            self.max_keys,
        )

    @classmethod
    def decode(cls, payload: bytes) -> CapabilitiesMessage:
        _require_size(payload, _CAPABILITIES.size, "capabilities")
        raw = _CAPABILITIES.unpack(payload)
        value = cls(
            capabilities=Capability(raw[0]),
            columns=raw[1],
            rows=raw[2],
            adapter=Adapter(raw[3]),
            max_fragment_payload=raw[4],
            max_keys=raw[5],
        )
        if not 1 <= value.columns <= 255 or not 1 <= value.rows <= 255:
            raise ValueError("invalid screen dimensions")
        if not 64 <= value.max_fragment_payload <= MAX_FRAGMENT_PAYLOAD:
            raise ValueError("invalid maximum fragment payload")
        return value


@dataclass(frozen=True, slots=True)
class ScreenMessage:
    columns: int
    rows: int
    video_mode: int
    active_page: int
    cursor_row: int
    cursor_column: int
    cursor_start: int
    cursor_end: int
    adapter: Adapter
    code_page: int
    generation: int
    cells: bytes

    def encode(self) -> bytes:
        self._validate()
        return _SCREEN.pack(
            self.columns,
            self.rows,
            self.video_mode,
            self.active_page,
            self.cursor_row,
            self.cursor_column,
            self.cursor_start,
            self.cursor_end,
            int(self.adapter),
            self.code_page,
            self.generation,
        ) + self.cells

    @classmethod
    def decode(cls, payload: bytes) -> ScreenMessage:
        if len(payload) < _SCREEN.size:
            raise ValueError("truncated screen message")
        raw = _SCREEN.unpack_from(payload)
        value = cls(
            columns=raw[0],
            rows=raw[1],
            video_mode=raw[2],
            active_page=raw[3],
            cursor_row=raw[4],
            cursor_column=raw[5],
            cursor_start=raw[6],
            cursor_end=raw[7],
            adapter=Adapter(raw[8]),
            code_page=raw[9],
            generation=raw[10],
            cells=payload[_SCREEN.size :],
        )
        value._validate()
        return value

    def _validate(self) -> None:
        if not 1 <= self.columns <= 255 or not 1 <= self.rows <= 255:
            raise ValueError("invalid screen dimensions")
        expected = self.columns * self.rows * 2
        if len(self.cells) != expected:
            raise ValueError(f"screen requires exactly {expected} cell bytes")
        if self.cursor_row >= self.rows or self.cursor_column >= self.columns:
            raise ValueError("cursor is outside screen")


@dataclass(frozen=True, slots=True)
class KeyRequest:
    text: bytes
    keys: tuple[KeyCode, ...]
    inter_key_delay_ms: int

    def encode(self) -> bytes:
        if len(self.text) > 4096:
            raise ValueError("key text exceeds 4096 bytes")
        if len(self.keys) > 128:
            raise ValueError("too many named keys")
        if not 0 <= self.inter_key_delay_ms <= 1000:
            raise ValueError("invalid inter-key delay")
        return (
            _KEY_REQUEST.pack(len(self.text), len(self.keys), self.inter_key_delay_ms)
            + self.text
            + bytes(self.keys)
        )

    @classmethod
    def decode(cls, payload: bytes) -> KeyRequest:
        if len(payload) < _KEY_REQUEST.size:
            raise ValueError("truncated key request")
        text_length, key_count, delay = _KEY_REQUEST.unpack_from(payload)
        expected = _KEY_REQUEST.size + text_length + key_count
        _require_size(payload, expected, "key request")
        text_end = _KEY_REQUEST.size + text_length
        return cls(
            text=payload[_KEY_REQUEST.size : text_end],
            keys=tuple(KeyCode(value) for value in payload[text_end:]),
            inter_key_delay_ms=delay,
        )


@dataclass(frozen=True, slots=True)
class KeyResponse:
    accepted_text_bytes: int
    accepted_keys: int
    generation: int

    def encode(self) -> bytes:
        return _KEY_RESPONSE.pack(
            self.accepted_text_bytes,
            self.accepted_keys,
            self.generation,
        )

    @classmethod
    def decode(cls, payload: bytes) -> KeyResponse:
        _require_size(payload, _KEY_RESPONSE.size, "key response")
        return cls(*_KEY_RESPONSE.unpack(payload))


def _require_size(payload: bytes, expected: int, name: str) -> None:
    if len(payload) != expected:
        raise ValueError(f"{name} requires exactly {expected} bytes")
