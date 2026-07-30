import pytest

from dos_mcp.discovery import DiscoveryAdvertisement
from dos_mcp.protocol import Capability
from dos_mcp.protocol.crc import crc16_ccitt


def test_discovery_advertisement_round_trip() -> None:
    value = DiscoveryAdvertisement(
        name="WORKBENCH-386",
        port=21300,
        capabilities=Capability.STATUS | Capability.TEXT_CAPTURE,
        agent_id=bytes.fromhex("acde48444d02"),
    )

    assert DiscoveryAdvertisement.decode(value.encode()) == value
    assert value.selector == "WORKBENCH-386@acde48444d02"


def test_discovery_rejects_corruption_and_unbounded_names() -> None:
    value = DiscoveryAdvertisement(
        name="DOS-PC",
        port=21300,
        capabilities=Capability.STATUS,
        agent_id=b"ABCDEF",
    )
    corrupted = bytearray(value.encode())
    corrupted[-1] ^= 1

    with pytest.raises(ValueError, match="CRC"):
        DiscoveryAdvertisement.decode(bytes(corrupted))
    with pytest.raises(ValueError, match="1-31"):
        DiscoveryAdvertisement(
            name="x" * 32,
            port=21300,
            capabilities=Capability.STATUS,
            agent_id=b"ABCDEF",
        )


def test_discovery_rejects_incompatible_protocol_and_unknown_capability() -> None:
    value = DiscoveryAdvertisement(
        name="DOS-PC",
        port=21300,
        capabilities=Capability.STATUS,
        agent_id=b"ABCDEF",
    )
    incompatible = bytearray(value.encode())
    incompatible[5] = 99
    incompatible[-2:] = value.encode()[-2:]
    unknown_capability = bytearray(value.encode())
    unknown_capability[13] |= 0x80

    # Recompute the CRC so rejection proves field validation, not corruption.
    incompatible[-2:] = crc16_ccitt(incompatible[:-2]).to_bytes(2, "little")
    unknown_capability[-2:] = crc16_ccitt(unknown_capability[:-2]).to_bytes(
        2, "little"
    )
    with pytest.raises(ValueError, match="protocol version"):
        DiscoveryAdvertisement.decode(bytes(incompatible))
    with pytest.raises(ValueError, match="capability"):
        DiscoveryAdvertisement.decode(bytes(unknown_capability))
