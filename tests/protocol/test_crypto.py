import pytest

from dos_mcp.protocol.crypto import (
    OPEN_MODE_KEY,
    credential_key,
    derive_password_key,
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


def test_password_derivation_has_stable_128_bit_vector() -> None:
    assert derive_password_key("correct horse battery staple").hex() == (
        "caa789fd210bf861dd7568249c843518"
    )


def test_password_derivation_accepts_long_input() -> None:
    assert len(derive_password_key("a" * 10_000)) == 16


def test_credential_accepts_explicit_and_backward_compatible_forms() -> None:
    raw = "00112233445566778899AABBCCDDEEFF"

    assert credential_key(raw) == bytes.fromhex(raw)
    assert credential_key(f"key:{raw}") == bytes.fromhex(raw)
    assert credential_key(f"pass:{raw}") == derive_password_key(raw)
    assert credential_key("a password of arbitrary length") == derive_password_key(
        "a password of arbitrary length"
    )


def test_missing_credential_selects_public_open_mode_key() -> None:
    assert OPEN_MODE_KEY.hex() == "b151cb0f3cdc527f03ed9aa537136ade"
    assert credential_key(None) == OPEN_MODE_KEY
    assert credential_key("-") == OPEN_MODE_KEY


@pytest.mark.parametrize("value", ["", "pass:"])
def test_empty_password_is_rejected(value: str) -> None:
    with pytest.raises(ValueError):
        credential_key(value)


@pytest.mark.parametrize(
    "value",
    ["00" * 16, "ab", "not-hex", "00 " * 16, f"0x{'12' * 15}"],
)
def test_parse_key_rejects_invalid_keys(value: str) -> None:
    with pytest.raises(ValueError):
        parse_key(value)
