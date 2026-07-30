import pytest

from dos_mcp.protocol import (
    MessageKind,
    Opcode,
    Packet,
    PacketError,
    fragment_message,
    reassemble_packets,
)
from dos_mcp.protocol.packet import HEADER_SIZE

KEY = bytes.fromhex("000102030405060708090a0b0c0d0e0f")


def test_packet_golden_vector() -> None:
    packet = Packet(
        kind=MessageKind.REQUEST,
        opcode=Opcode.GET_STATUS,
        session_id=0x1234,
        request_id=0x5678,
        fragment_index=0,
        fragment_count=1,
        payload=b"status",
    )

    encoded = packet.encode(KEY)

    assert encoded.hex() == (
        "444d0201020034127856000106009259b7707354737461747573"
    )
    assert Packet.decode(encoded, KEY) == packet


def test_packet_rejects_corruption() -> None:
    encoded = bytearray(
        Packet(
            kind=MessageKind.REQUEST,
            opcode=Opcode.PING,
            session_id=1,
            request_id=2,
            fragment_index=0,
            fragment_count=1,
            payload=b"hello",
        ).encode(KEY)
    )
    encoded[-1] ^= 1

    with pytest.raises(PacketError, match="CRC"):
        Packet.decode(bytes(encoded), KEY)


def test_packet_rejects_unknown_flags() -> None:
    with pytest.raises(ValueError, match="no packet flags"):
        Packet(
            kind=MessageKind.REQUEST,
            opcode=Opcode.PING,
            session_id=1,
            request_id=2,
            fragment_index=0,
            fragment_count=1,
            flags=1,
        )


def test_packet_accepts_one_zero_transport_padding_byte() -> None:
    packet = Packet(
        kind=MessageKind.REQUEST,
        opcode=Opcode.PING,
        session_id=1,
        request_id=2,
        fragment_index=0,
        fragment_count=1,
        payload=b"odd",
    )

    assert Packet.decode(packet.encode(KEY) + b"\x00", KEY) == packet


def test_packet_rejects_nonzero_or_excess_transport_padding() -> None:
    encoded = Packet(
        kind=MessageKind.REQUEST,
        opcode=Opcode.PING,
        session_id=1,
        request_id=2,
        fragment_index=0,
        fragment_count=1,
    ).encode(KEY)

    with pytest.raises(PacketError, match="length"):
        Packet.decode(encoded + b"\x01", KEY)
    with pytest.raises(PacketError, match="length"):
        Packet.decode(encoded + b"\x00\x00", KEY)


@pytest.mark.parametrize("size", range(HEADER_SIZE))
def test_packet_rejects_every_truncated_header(size: int) -> None:
    with pytest.raises(PacketError, match="truncated"):
        Packet.decode(b"\x00" * size, KEY)


def test_fragmentation_round_trip_out_of_order() -> None:
    payload = bytes(range(256)) * 12
    packets = fragment_message(
        kind=MessageKind.RESPONSE,
        opcode=Opcode.CAPTURE_TEXT_SCREEN,
        session_id=3,
        request_id=9,
        payload=payload,
    )

    assert len(packets) == 3
    assert reassemble_packets(reversed(packets)) == payload


def test_reassembly_rejects_missing_fragment() -> None:
    packets = fragment_message(
        kind=MessageKind.RESPONSE,
        opcode=Opcode.CAPTURE_TEXT_SCREEN,
        session_id=3,
        request_id=9,
        payload=b"x" * 2000,
    )

    with pytest.raises(PacketError, match="missing"):
        reassemble_packets(packets[:1])
