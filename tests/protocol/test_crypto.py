import pytest

from dos_mcp.protocol.crypto import (
    derive_session_key,
    packet_mac,
    parse_key,
    xtea_encrypt,
)

KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")


def test_xtea_is_deterministic_and_keyed() -> None:
    block = bytes.fromhex("0011223344556677")

    encrypted = xtea_encrypt(block, KEY)

    assert encrypted.hex() == "93009913e1c4f785"
    assert encrypted != xtea_encrypt(block, bytes(reversed(KEY)))


def test_packet_mac_changes_with_message_and_length() -> None:
    assert packet_mac(b"abc", KEY) == packet_mac(b"abc", KEY)
    assert packet_mac(b"abc", KEY) != packet_mac(b"abd", KEY)
    assert packet_mac(b"abc", KEY) != packet_mac(b"abc\x00", KEY)


def test_session_key_uses_both_nonces() -> None:
    first = derive_session_key(KEY, 1, 2)

    assert len(first) == 16
    assert first != derive_session_key(KEY, 2, 1)


@pytest.mark.parametrize("value", ["00" * 16, "ab", "not-hex"])
def test_parse_key_rejects_invalid_keys(value: str) -> None:
    with pytest.raises(ValueError):
        parse_key(value)
