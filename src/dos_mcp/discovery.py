"""Strict codec and nonblocking listener for local RA-TSR announcements."""

from __future__ import annotations

import socket
import struct
import time
from dataclasses import dataclass

from .protocol.constants import VERSION, Capability
from .protocol.crc import crc16_ccitt

DISCOVERY_PORT = 21301
DISCOVERY_MAGIC = b"DMD2"
DISCOVERY_VERSION = 1
MAX_TARGET_NAME_BYTES = 31
_HEADER = struct.Struct("<4sBBBBHI6s")
_CRC = struct.Struct("<H")
_KNOWN_CAPABILITIES = sum(int(value) for value in Capability)


@dataclass(frozen=True, slots=True)
class DiscoveryAdvertisement:
    name: str
    port: int
    capabilities: Capability
    agent_id: bytes
    open_mode: bool = False
    protocol_version: int = VERSION

    def __post_init__(self) -> None:
        encoded_name = self.name.encode("ascii")
        if not 1 <= len(encoded_name) <= MAX_TARGET_NAME_BYTES:
            raise ValueError("target name must contain 1-31 ASCII bytes")
        if any(value < 33 or value > 126 for value in encoded_name):
            raise ValueError("target name must contain visible ASCII without spaces")
        if not 1 <= self.port <= 0xFFFF:
            raise ValueError("target port is outside 1-65535")
        if len(self.agent_id) != 6:
            raise ValueError("agent_id must be the six-byte adapter address")
        if not 0 <= self.protocol_version <= 0xFF:
            raise ValueError("protocol version must fit in one byte")

    @property
    def stable_id(self) -> str:
        return self.agent_id.hex()

    @property
    def selector(self) -> str:
        return f"{self.name}@{self.stable_id}"

    def encode(self) -> bytes:
        name = self.name.encode("ascii")
        header = _HEADER.pack(
            DISCOVERY_MAGIC,
            DISCOVERY_VERSION,
            self.protocol_version,
            int(self.open_mode),
            len(name),
            self.port,
            int(self.capabilities),
            self.agent_id,
        )
        body = header + name
        return body + _CRC.pack(crc16_ccitt(body))

    @classmethod
    def decode(cls, datagram: bytes) -> DiscoveryAdvertisement:
        if len(datagram) < _HEADER.size + _CRC.size:
            raise ValueError("truncated discovery advertisement")
        (
            magic,
            discovery_version,
            protocol_version,
            flags,
            name_length,
            port,
            raw_capabilities,
            agent_id,
        ) = _HEADER.unpack_from(datagram)
        if magic != DISCOVERY_MAGIC:
            raise ValueError("invalid discovery magic")
        if discovery_version != DISCOVERY_VERSION:
            raise ValueError("unsupported discovery format")
        if protocol_version != VERSION:
            raise ValueError("unsupported agent protocol version")
        if flags & ~1:
            raise ValueError("unknown discovery flags")
        if raw_capabilities & ~_KNOWN_CAPABILITIES:
            raise ValueError("unknown discovery capability flags")
        if not 1 <= name_length <= MAX_TARGET_NAME_BYTES:
            raise ValueError("invalid discovery name length")
        if len(datagram) != _HEADER.size + name_length + _CRC.size:
            raise ValueError("discovery datagram length mismatch")
        body = datagram[:-2]
        if crc16_ccitt(body) != _CRC.unpack_from(datagram, len(body))[0]:
            raise ValueError("discovery CRC mismatch")
        try:
            name = datagram[_HEADER.size : -2].decode("ascii")
            capabilities = Capability(raw_capabilities)
        except (UnicodeDecodeError, ValueError) as exc:
            raise ValueError("invalid discovery fields") from exc
        return cls(
            name=name,
            port=port,
            capabilities=capabilities,
            agent_id=agent_id,
            open_mode=bool(flags & 1),
            protocol_version=protocol_version,
        )


@dataclass(frozen=True, slots=True)
class DiscoveredTarget:
    advertisement: DiscoveryAdvertisement
    address: tuple[str, int]
    last_seen: float

    def to_dict(self) -> dict[str, object]:
        return {
            "selector": self.advertisement.selector,
            "name": self.advertisement.name,
            "agent_id": self.advertisement.stable_id,
            "host": self.address[0],
            "port": self.address[1],
            "protocol_version": self.advertisement.protocol_version,
            "open_mode": self.advertisement.open_mode,
            "capabilities": int(self.advertisement.capabilities),
            "last_seen_seconds_ago": max(0.0, time.monotonic() - self.last_seen),
            "source": "discovery",
        }


class DiscoveryListener:
    """Own a local UDP listener and drain validated announcements on demand."""

    def __init__(self, *, port: int = DISCOVERY_PORT) -> None:
        if not 1 <= port <= 0xFFFF:
            raise ValueError("discovery port is outside 1-65535")
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._socket.bind(("", port))
        self._socket.setblocking(False)
        self._closed = False

    def drain(self) -> tuple[DiscoveredTarget, ...]:
        if self._closed:
            return ()
        targets: list[DiscoveredTarget] = []
        while True:
            try:
                datagram, source = self._socket.recvfrom(256)
            except BlockingIOError:
                break
            try:
                advertisement = DiscoveryAdvertisement.decode(datagram)
            except ValueError:
                continue
            # The UDP source is authoritative for routing. The payload port is
            # the RA-TSR service port, not the ephemeral/broadcast destination.
            targets.append(
                DiscoveredTarget(
                    advertisement,
                    (source[0], advertisement.port),
                    time.monotonic(),
                )
            )
        return tuple(targets)

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self._socket.close()
