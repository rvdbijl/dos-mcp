"""XTEA-based packet MAC and session-key derivation.

This is authentication and replay defense, not confidentiality. The 32-bit
truncated tag is a deliberate first-generation 8088 tradeoff and requires a
trusted local network plus a unique key per managed machine.
"""

from __future__ import annotations

import hmac
import struct

_BLOCK = struct.Struct("<II")
_KEY = struct.Struct("<IIII")
_MASK = 0xFFFFFFFF
_DELTA = 0x9E3779B9


def parse_key(value: str) -> bytes:
    try:
        key = bytes.fromhex(value)
    except ValueError as exc:
        raise ValueError("protocol key must be 32 hexadecimal characters") from exc
    if len(key) != 16:
        raise ValueError("protocol key must encode exactly 16 bytes")
    if not any(key):
        raise ValueError("protocol key must not be all zeroes")
    return key


def xtea_encrypt(block: bytes, key: bytes) -> bytes:
    if len(block) != 8 or len(key) != 16:
        raise ValueError("XTEA requires an 8-byte block and 16-byte key")
    left, right = _BLOCK.unpack(block)
    words = _KEY.unpack(key)
    total = 0
    for _ in range(32):
        left = (
            left
            + (
                (((right << 4) ^ (right >> 5)) + right)
                ^ (total + words[total & 3])
            )
        ) & _MASK
        total = (total + _DELTA) & _MASK
        right = (
            right
            + (
                (((left << 4) ^ (left >> 5)) + left)
                ^ (total + words[(total >> 11) & 3])
            )
        ) & _MASK
    return _BLOCK.pack(left, right)


def derive_session_key(key: bytes, client_nonce: int, server_nonce: int) -> bytes:
    first = _BLOCK.pack(client_nonce & _MASK, server_nonce & _MASK)
    second = _BLOCK.pack(server_nonce ^ 0xA5A5A5A5, client_nonce ^ 0x5A5A5A5A)
    return xtea_encrypt(first, key) + xtea_encrypt(second, key)


def packet_mac(data: bytes, key: bytes) -> int:
    """Return a 32-bit XTEA CBC-MAC with length strengthening."""
    if len(key) != 16:
        raise ValueError("packet MAC requires a 16-byte key")
    padded = data + b"\x80"
    padded += b"\x00" * ((8 - (len(padded) % 8)) % 8)
    padded += _BLOCK.pack(len(data), len(data) ^ 0xFFFFFFFF)
    state = b"\x00" * 8
    for offset in range(0, len(padded), 8):
        block = bytes(a ^ b for a, b in zip(state, padded[offset : offset + 8], strict=True))
        state = xtea_encrypt(block, key)
    left, right = _BLOCK.unpack(state)
    return (left ^ right) & _MASK


def tags_equal(left: int, right: int) -> bool:
    return hmac.compare_digest(struct.pack("<I", left), struct.pack("<I", right))
