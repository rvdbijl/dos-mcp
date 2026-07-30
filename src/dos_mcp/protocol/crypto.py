"""Lightweight packet authentication and XTEA session-key derivation.

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
_SPECK_BLOCK = struct.Struct("<HH")
_SPECK_KEY = struct.Struct("<HHHH")
_MASK = 0xFFFFFFFF
_MASK16 = 0xFFFF
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


def _ror16(value: int, count: int) -> int:
    return ((value >> count) | (value << (16 - count))) & _MASK16


def _rol16(value: int, count: int) -> int:
    return ((value << count) | (value >> (16 - count))) & _MASK16


def _speck32_encrypt(block: bytes, key: bytes) -> bytes:
    """Encrypt one 32-bit block with Speck32/64."""
    if len(block) != 4 or len(key) != 8:
        raise ValueError("Speck32/64 requires a 4-byte block and 8-byte key")
    left, right = _SPECK_BLOCK.unpack(block)
    round_key, *schedule = _SPECK_KEY.unpack(key)
    for round_index in range(22):
        left = ((_ror16(left, 7) + right) & _MASK16) ^ round_key
        right = _rol16(right, 2) ^ left
        if round_index != 21:
            slot = round_index % 3
            next_word = (
                (_ror16(schedule[slot], 7) + round_key) & _MASK16
            ) ^ round_index
            schedule[slot] = next_word
            round_key = _rol16(round_key, 2) ^ next_word
    return _SPECK_BLOCK.pack(left, right)


def packet_mac(data: bytes, key: bytes) -> int:
    """Return a 32-bit Speck32/64 CBC-MAC with length strengthening.

    Speck32 uses native 16-bit operations on an 8088. The first key half
    authenticates the CBC chain and the second half finalizes it for domain
    separation. The protocol's replay/session rules remain separate.
    """
    if len(key) != 16:
        raise ValueError("packet MAC requires a 16-byte key")
    padded = data + b"\x80"
    padded += b"\x00" * ((4 - (len(padded) % 4)) % 4)
    padded += struct.pack("<I", len(data))
    state = b"\x00" * 4
    for offset in range(0, len(padded), 4):
        block = bytes(
            a ^ b
            for a, b in zip(state, padded[offset : offset + 4], strict=True)
        )
        state = _speck32_encrypt(block, key[:8])
    state = _speck32_encrypt(state, key[8:])
    return struct.unpack("<I", state)[0]


def tags_equal(left: int, right: int) -> bool:
    return hmac.compare_digest(struct.pack("<I", left), struct.pack("<I", right))
