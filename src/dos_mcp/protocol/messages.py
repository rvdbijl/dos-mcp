"""Operation payload codecs with strict, bounded validation."""

from __future__ import annotations

import struct
from dataclasses import dataclass

from .constants import (
    MAX_FRAGMENT_PAYLOAD,
    Adapter,
    Capability,
    GraphicsLayout,
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
_TRANSFER_BLOCK_REQUEST = struct.Struct("<HIH")
_TRANSFER_BLOCK_RESPONSE = struct.Struct("<HIHI")
_TRANSFER_END_REQUEST = struct.Struct("<H")
_TRANSFER_END_RESPONSE = struct.Struct("<II")
_FILE_WRITE_BEGIN = struct.Struct("<BIIB")
_GRAPHICS_BEGIN_RESPONSE = struct.Struct("<HBBBBHHII")

MAX_TRANSFER_BLOCK = 900
MAX_DOS_PATH_BYTES = 80
_KNOWN_CAPABILITIES = sum(int(value) for value in Capability)


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
        if raw[0] & ~_KNOWN_CAPABILITIES:
            raise ValueError("unknown capability flags")
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


@dataclass(frozen=True, slots=True)
class FileReadBeginRequest:
    path: bytes

    def encode(self) -> bytes:
        _validate_path(self.path)
        return bytes((len(self.path),)) + self.path

    @classmethod
    def decode(cls, payload: bytes) -> FileReadBeginRequest:
        if not payload:
            raise ValueError("truncated file-read request")
        _require_size(payload, payload[0] + 1, "file-read request")
        value = cls(payload[1:])
        _validate_path(value.path)
        return value


@dataclass(frozen=True, slots=True)
class FileWriteBeginRequest:
    path: bytes
    total_size: int
    crc32: int
    overwrite: bool

    def encode(self) -> bytes:
        _validate_path(self.path)
        if not 0 <= self.total_size <= 0xFFFFFFFF:
            raise ValueError("invalid file size")
        return _FILE_WRITE_BEGIN.pack(
            int(self.overwrite), self.total_size, self.crc32, len(self.path)
        ) + self.path

    @classmethod
    def decode(cls, payload: bytes) -> FileWriteBeginRequest:
        if len(payload) < _FILE_WRITE_BEGIN.size:
            raise ValueError("truncated file-write request")
        overwrite, total_size, crc32, path_length = _FILE_WRITE_BEGIN.unpack_from(
            payload
        )
        _require_size(
            payload, _FILE_WRITE_BEGIN.size + path_length, "file-write request"
        )
        if overwrite > 1:
            raise ValueError("invalid overwrite flag")
        value = cls(
            path=payload[_FILE_WRITE_BEGIN.size :],
            total_size=total_size,
            crc32=crc32,
            overwrite=bool(overwrite),
        )
        _validate_path(value.path)
        return value


@dataclass(frozen=True, slots=True)
class TransferBeginResponse:
    transfer_id: int
    total_size: int

    def encode(self) -> bytes:
        return struct.pack("<HI", self.transfer_id, self.total_size)

    @classmethod
    def decode(cls, payload: bytes) -> TransferBeginResponse:
        _require_size(payload, 6, "transfer-begin response")
        value = cls(*struct.unpack("<HI", payload))
        if value.transfer_id == 0:
            raise ValueError("transfer ID zero is reserved")
        return value


@dataclass(frozen=True, slots=True)
class TransferBlockRequest:
    transfer_id: int
    offset: int
    length: int
    data: bytes = b""

    def encode(self) -> bytes:
        if self.transfer_id == 0:
            raise ValueError("transfer ID zero is reserved")
        if not 1 <= self.length <= MAX_TRANSFER_BLOCK:
            raise ValueError("invalid transfer block length")
        if self.data and len(self.data) != self.length:
            raise ValueError("transfer data length mismatch")
        return _TRANSFER_BLOCK_REQUEST.pack(
            self.transfer_id, self.offset, self.length
        ) + self.data

    @classmethod
    def decode(cls, payload: bytes, *, has_data: bool) -> TransferBlockRequest:
        if len(payload) < _TRANSFER_BLOCK_REQUEST.size:
            raise ValueError("truncated transfer block")
        transfer_id, offset, length = _TRANSFER_BLOCK_REQUEST.unpack_from(payload)
        expected = _TRANSFER_BLOCK_REQUEST.size + (length if has_data else 0)
        _require_size(payload, expected, "transfer block")
        value = cls(
            transfer_id,
            offset,
            length,
            payload[_TRANSFER_BLOCK_REQUEST.size :] if has_data else b"",
        )
        value.encode()
        return value


@dataclass(frozen=True, slots=True)
class TransferBlockResponse:
    transfer_id: int
    offset: int
    data: bytes
    rolling_crc32: int

    def encode(self) -> bytes:
        if len(self.data) > MAX_TRANSFER_BLOCK:
            raise ValueError("transfer response block is too large")
        return _TRANSFER_BLOCK_RESPONSE.pack(
            self.transfer_id, self.offset, len(self.data), self.rolling_crc32
        ) + self.data

    @classmethod
    def decode(cls, payload: bytes) -> TransferBlockResponse:
        if len(payload) < _TRANSFER_BLOCK_RESPONSE.size:
            raise ValueError("truncated transfer-block response")
        transfer_id, offset, length, crc32 = _TRANSFER_BLOCK_RESPONSE.unpack_from(
            payload
        )
        _require_size(
            payload, _TRANSFER_BLOCK_RESPONSE.size + length, "transfer-block response"
        )
        if length > MAX_TRANSFER_BLOCK:
            raise ValueError("transfer response block is too large")
        return cls(
            transfer_id,
            offset,
            payload[_TRANSFER_BLOCK_RESPONSE.size :],
            crc32,
        )


@dataclass(frozen=True, slots=True)
class TransferEndRequest:
    transfer_id: int

    def encode(self) -> bytes:
        if self.transfer_id == 0:
            raise ValueError("transfer ID zero is reserved")
        return _TRANSFER_END_REQUEST.pack(self.transfer_id)

    @classmethod
    def decode(cls, payload: bytes) -> TransferEndRequest:
        _require_size(payload, _TRANSFER_END_REQUEST.size, "transfer-end request")
        return cls(*_TRANSFER_END_REQUEST.unpack(payload))


@dataclass(frozen=True, slots=True)
class TransferEndResponse:
    total_size: int
    crc32: int

    def encode(self) -> bytes:
        return _TRANSFER_END_RESPONSE.pack(self.total_size, self.crc32)

    @classmethod
    def decode(cls, payload: bytes) -> TransferEndResponse:
        _require_size(payload, _TRANSFER_END_RESPONSE.size, "transfer-end response")
        return cls(*_TRANSFER_END_RESPONSE.unpack(payload))


@dataclass(frozen=True, slots=True)
class GraphicsBeginResponse:
    transfer_id: int
    adapter: Adapter
    video_mode: int
    layout: GraphicsLayout
    planes: int
    width: int
    height: int
    total_size: int
    bytes_per_plane: int

    def encode(self) -> bytes:
        return _GRAPHICS_BEGIN_RESPONSE.pack(
            self.transfer_id,
            int(self.adapter),
            self.video_mode,
            int(self.layout),
            self.planes,
            self.width,
            self.height,
            self.total_size,
            self.bytes_per_plane,
        )

    @classmethod
    def decode(cls, payload: bytes) -> GraphicsBeginResponse:
        _require_size(payload, _GRAPHICS_BEGIN_RESPONSE.size, "graphics-begin response")
        raw = _GRAPHICS_BEGIN_RESPONSE.unpack(payload)
        value = cls(
            raw[0],
            Adapter(raw[1]),
            raw[2],
            GraphicsLayout(raw[3]),
            raw[4],
            raw[5],
            raw[6],
            raw[7],
            raw[8],
        )
        if (
            value.transfer_id == 0
            or not 1 <= value.planes <= 4
            or not 1 <= value.width <= 2048
            or not 1 <= value.height <= 2048
            or value.total_size == 0
            or value.bytes_per_plane == 0
            or value.total_size != value.planes * value.bytes_per_plane
            or (
                value.layout is GraphicsLayout.PLANAR_4BPP
                and value.planes != 4
            )
            or (
                value.layout is not GraphicsLayout.PLANAR_4BPP
                and value.planes != 1
            )
        ):
            raise ValueError("invalid graphics metadata")
        return value


def _require_size(payload: bytes, expected: int, name: str) -> None:
    if len(payload) != expected:
        raise ValueError(f"{name} requires exactly {expected} bytes")


def _validate_path(path: bytes) -> None:
    if not 1 <= len(path) <= MAX_DOS_PATH_BYTES or b"\x00" in path:
        raise ValueError("DOS path must contain 1 to 80 non-NUL bytes")
