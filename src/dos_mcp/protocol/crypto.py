"""XTEA-based packet MAC and session-key derivation.

With a secret credential this provides authentication and replay defense, not
confidentiality. The 32-bit truncated tag is a deliberate first-generation
8088 tradeoff and requires a trusted local network plus a unique credential
per managed machine. Open mode deliberately provides no authentication.
"""

from __future__ import annotations

import hashlib
import hmac
import struct

_BLOCK = struct.Struct("<II")
_KEY = struct.Struct("<IIII")
_MASK = 0xFFFFFFFF
_DELTA = 0x9E3779B9
_PASSWORD_DOMAIN = b"DOS-MCP credential v1\x00"
_OPEN_MODE_DOMAIN = b"DOS-MCP open mode v1\x00"
OPEN_MODE_KEY = hashlib.sha256(_OPEN_MODE_DOMAIN).digest()[:16]
_HEX_DIGITS = frozenset("0123456789abcdefABCDEF")


def parse_key(value: str) -> bytes:
    if len(value) != 32 or any(character not in _HEX_DIGITS for character in value):
        raise ValueError("protocol key must be exactly 32 hexadecimal characters")
    key = bytes.fromhex(value)
    if not any(key):
        raise ValueError("protocol key must not be all zeroes")
    return key


def derive_password_key(password: str) -> bytes:
    """Derive a protocol key from a nonempty UTF-8 passphrase.

    This deliberately uses a lightweight, deterministic SHA-256 derivation so
    the 16-bit DOS agent can calculate the same key. It is not a slow password
    hash, so callers should use a high-entropy passphrase.
    """
    if not password:
        raise ValueError("protocol password must not be empty")
    return hashlib.sha256(_PASSWORD_DOMAIN + password.encode()).digest()[:16]


def credential_key(value: str | None) -> bytes:
    """Resolve a DOS-agent credential into the protocol's 128-bit key.

    ``None`` and ``-`` select the public open-mode key. ``key:`` forces a raw
    hexadecimal key, while ``pass:`` forces password derivation. A bare 32-digit
    hexadecimal value remains a raw key for backward compatibility; every other
    nonempty value is a passphrase.
    """
    if value is None or value == "-":
        return OPEN_MODE_KEY
    if value.startswith("key:"):
        return parse_key(value[4:])
    if value.startswith("pass:"):
        return derive_password_key(value[5:])
    if len(value) == 32:
        try:
            int(value, 16)
        except ValueError:
            pass
        else:
            return parse_key(value)
    return derive_password_key(value)


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
