"""Version-1 datagram encoding, validation, fragmentation, and reassembly."""

from __future__ import annotations

import struct
from collections.abc import Iterable
from dataclasses import dataclass

from .constants import (
    MAGIC,
    MAX_FRAGMENT_PAYLOAD,
    MAX_FRAGMENTS,
    MAX_MESSAGE_SIZE,
    VERSION,
    MessageKind,
    Opcode,
)
from .crc import crc16_ccitt
from .crypto import packet_mac, tags_equal

_PREFIX = struct.Struct("<2sBBBBHHBBH")
_AUTH = struct.Struct("<2sBBBBHHBBHH")
_HEADER = struct.Struct("<2sBBBBHHBBHHI")
HEADER_SIZE = _HEADER.size


class PacketError(ValueError):
    pass


@dataclass(frozen=True, slots=True)
class Packet:
    kind: MessageKind
    opcode: Opcode
    session_id: int
    request_id: int
    fragment_index: int
    fragment_count: int
    payload: bytes = b""
    flags: int = 0

    def __post_init__(self) -> None:
        if not 0 <= self.flags <= 0xFF:
            raise ValueError("flags must fit in one byte")
        if not 0 <= self.session_id <= 0xFFFF:
            raise ValueError("session_id must fit in two bytes")
        if not 0 <= self.request_id <= 0xFFFF:
            raise ValueError("request_id must fit in two bytes")
        if not 1 <= self.fragment_count <= MAX_FRAGMENTS:
            raise ValueError("invalid fragment_count")
        if not 0 <= self.fragment_index < self.fragment_count:
            raise ValueError("fragment_index is outside fragment_count")
        if len(self.payload) > MAX_FRAGMENT_PAYLOAD:
            raise ValueError("fragment payload is too large")

    def encode(self, key: bytes) -> bytes:
        prefix = _PREFIX.pack(
            MAGIC,
            VERSION,
            int(self.kind),
            int(self.opcode),
            self.flags,
            self.session_id,
            self.request_id,
            self.fragment_index,
            self.fragment_count,
            len(self.payload),
        )
        checksum = crc16_ccitt(prefix + self.payload)
        authenticated = _AUTH.pack(
            MAGIC,
            VERSION,
            int(self.kind),
            int(self.opcode),
            self.flags,
            self.session_id,
            self.request_id,
            self.fragment_index,
            self.fragment_count,
            len(self.payload),
            checksum,
        )
        tag = packet_mac(authenticated + self.payload, key)
        return authenticated + struct.pack("<I", tag) + self.payload

    @classmethod
    def decode(cls, datagram: bytes, key: bytes) -> Packet:
        if len(datagram) < HEADER_SIZE:
            raise PacketError("truncated packet header")
        (
            magic,
            version,
            raw_kind,
            raw_opcode,
            flags,
            session_id,
            request_id,
            fragment_index,
            fragment_count,
            payload_length,
            checksum,
            tag,
        ) = _HEADER.unpack_from(datagram)
        if magic != MAGIC:
            raise PacketError("invalid packet magic")
        if version != VERSION:
            raise PacketError(f"unsupported protocol version {version}")
        if payload_length > MAX_FRAGMENT_PAYLOAD:
            raise PacketError("declared payload exceeds fragment limit")
        if len(datagram) != HEADER_SIZE + payload_length:
            raise PacketError("datagram length does not match payload length")
        if not 1 <= fragment_count <= MAX_FRAGMENTS:
            raise PacketError("invalid fragment count")
        if fragment_index >= fragment_count:
            raise PacketError("invalid fragment index")
        try:
            kind = MessageKind(raw_kind)
            opcode = Opcode(raw_opcode)
        except ValueError as exc:
            raise PacketError("unknown message kind or opcode") from exc
        payload = datagram[HEADER_SIZE:]
        prefix = datagram[: _PREFIX.size]
        if crc16_ccitt(prefix + payload) != checksum:
            raise PacketError("packet CRC mismatch")
        authenticated = datagram[: _AUTH.size]
        expected_tag = packet_mac(authenticated + payload, key)
        if not tags_equal(tag, expected_tag):
            raise PacketError("packet authentication failed")
        return cls(
            kind=kind,
            opcode=opcode,
            flags=flags,
            session_id=session_id,
            request_id=request_id,
            fragment_index=fragment_index,
            fragment_count=fragment_count,
            payload=payload,
        )


def fragment_message(
    *,
    kind: MessageKind,
    opcode: Opcode,
    session_id: int,
    request_id: int,
    payload: bytes,
    flags: int = 0,
) -> tuple[Packet, ...]:
    if len(payload) > MAX_MESSAGE_SIZE:
        raise ValueError("message exceeds maximum reassembled size")
    fragments = max(1, (len(payload) + MAX_FRAGMENT_PAYLOAD - 1) // MAX_FRAGMENT_PAYLOAD)
    return tuple(
        Packet(
            kind=kind,
            opcode=opcode,
            flags=flags,
            session_id=session_id,
            request_id=request_id,
            fragment_index=index,
            fragment_count=fragments,
            payload=payload[
                index * MAX_FRAGMENT_PAYLOAD : (index + 1) * MAX_FRAGMENT_PAYLOAD
            ],
        )
        for index in range(fragments)
    )


def reassemble_packets(packets: Iterable[Packet]) -> bytes:
    values = list(packets)
    if not values:
        raise PacketError("cannot reassemble an empty packet set")
    first = values[0]
    identity = (
        first.kind,
        first.opcode,
        first.session_id,
        first.request_id,
        first.fragment_count,
    )
    if any(
        (
            packet.kind,
            packet.opcode,
            packet.session_id,
            packet.request_id,
            packet.fragment_count,
        )
        != identity
        for packet in values
    ):
        raise PacketError("fragment identity mismatch")
    by_index = {packet.fragment_index: packet for packet in values}
    if len(values) != len(by_index) or len(by_index) != first.fragment_count:
        raise PacketError("message has missing or duplicate fragments")
    return b"".join(by_index[index].payload for index in range(first.fragment_count))
