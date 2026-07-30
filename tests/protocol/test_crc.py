from dos_mcp.protocol.crc import crc16_ccitt


def test_crc16_standard_vector() -> None:
    assert crc16_ccitt(b"123456789") == 0x29B1
