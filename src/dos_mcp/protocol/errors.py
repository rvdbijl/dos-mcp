"""Wire error payload helpers."""

from __future__ import annotations

import struct

from .constants import ErrorCode

_ERROR = struct.Struct("<H")


def encode_error(code: ErrorCode, message: str) -> bytes:
    encoded = message.encode("ascii", errors="replace")[:120]
    return _ERROR.pack(int(code)) + encoded


def decode_error(payload: bytes) -> tuple[ErrorCode, str]:
    if len(payload) < _ERROR.size:
        raise ValueError("truncated error payload")
    code = ErrorCode(_ERROR.unpack_from(payload)[0])
    message = payload[_ERROR.size :].decode("ascii", errors="replace")
    return code, message
