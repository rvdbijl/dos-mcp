"""Compact authenticated protocol shared by bridge and DOS agent."""

from .constants import (
    Adapter,
    Capability,
    ErrorCode,
    KeyCode,
    MessageKind,
    Opcode,
    Phase,
)
from .crypto import derive_session_key, parse_key
from .messages import (
    CapabilitiesMessage,
    HelloRequest,
    HelloResponse,
    KeyRequest,
    KeyResponse,
    ScreenMessage,
    StatusMessage,
)
from .packet import Packet, PacketError, fragment_message, reassemble_packets

__all__ = [
    "Adapter",
    "CapabilitiesMessage",
    "Capability",
    "ErrorCode",
    "HelloRequest",
    "HelloResponse",
    "KeyCode",
    "KeyRequest",
    "KeyResponse",
    "MessageKind",
    "Opcode",
    "Packet",
    "PacketError",
    "Phase",
    "ScreenMessage",
    "StatusMessage",
    "derive_session_key",
    "fragment_message",
    "parse_key",
    "reassemble_packets",
]
