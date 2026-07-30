"""UDP agent protocol server used by the Linux simulator."""

from __future__ import annotations

import secrets
import socket
import struct
import threading
from collections.abc import Callable
from dataclasses import dataclass, field

from .backend import Backend
from .models import Capabilities, MachineStatus, TextScreen
from .protocol import (
    Adapter,
    CapabilitiesMessage,
    Capability,
    ErrorCode,
    HelloRequest,
    HelloResponse,
    KeyRequest,
    KeyResponse,
    MessageKind,
    Opcode,
    Packet,
    PacketError,
    Phase,
    ScreenMessage,
    StatusMessage,
    derive_session_key,
    fragment_message,
    reassemble_packets,
)
from .protocol.constants import MAX_DATAGRAM
from .protocol.errors import encode_error
from .protocol.packet import HEADER_SIZE

_UNAUTHENTICATED_FIELDS = struct.Struct("<2sBBBBHH")


@dataclass(slots=True)
class _Session:
    address: tuple[str, int]
    session_id: int
    key: bytes
    last_request_id: int | None = None
    response_cache: dict[int, tuple[bytes, ...]] = field(default_factory=dict)
    pending: dict[int, dict[int, Packet]] = field(default_factory=dict)


class UdpAgentServer:
    """Serve one authenticated bridge session over UDP."""

    def __init__(
        self,
        backend: Backend,
        *,
        key: bytes,
        bind: tuple[str, int] = ("127.0.0.1", 21300),
        nonce_factory: Callable[[], int] | None = None,
        drop_first_response: bool = False,
    ) -> None:
        if len(key) != 16:
            raise ValueError("agent key must be 16 bytes")
        self.backend = backend
        self.key = key
        self._nonce_factory = nonce_factory or (lambda: secrets.randbits(32))
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.bind(bind)
        self._socket.settimeout(0.1)
        self._session: _Session | None = None
        self._closed = False
        self._drop_first_response = drop_first_response
        self._dropped_response = False

    @property
    def address(self) -> tuple[str, int]:
        host, port = self._socket.getsockname()
        return str(host), int(port)

    def serve_forever(self, stop: threading.Event | None = None) -> None:
        stop = stop or threading.Event()
        while not stop.is_set() and not self._closed:
            try:
                datagram, address = self._socket.recvfrom(MAX_DATAGRAM + 1)
            except TimeoutError:
                continue
            except OSError:
                if self._closed:
                    break
                raise
            self.handle_datagram(datagram, address)

    def handle_datagram(self, datagram: bytes, address: tuple[str, int]) -> None:
        if len(datagram) > MAX_DATAGRAM or len(datagram) < HEADER_SIZE:
            return
        try:
            _, _, raw_kind, raw_opcode, _, session_id, request_id = (
                _UNAUTHENTICATED_FIELDS.unpack_from(datagram)
            )
        except struct.error:
            return
        if raw_kind != MessageKind.REQUEST:
            return

        if raw_opcode == Opcode.HELLO and session_id == 0:
            self._handle_hello(datagram, address)
            return

        session = self._session
        if (
            session is None
            or session.address != address
            or session.session_id != session_id
        ):
            return
        try:
            packet = Packet.decode(datagram, session.key)
        except PacketError:
            return
        if packet.request_id in session.response_cache:
            self._send_cached(session, packet.request_id)
            return
        if session.last_request_id is not None and not _request_is_newer(
            packet.request_id, session.last_request_id
        ):
            self._send_error(
                session,
                packet.opcode,
                packet.request_id,
                ErrorCode.REPLAYED_REQUEST,
                "request ID is outside the replay window",
            )
            return

        if request_id not in session.pending and len(session.pending) >= 4:
            session.pending.pop(next(iter(session.pending)))
        pending = session.pending.setdefault(request_id, {})
        pending[packet.fragment_index] = packet
        if len(pending) != packet.fragment_count:
            return
        try:
            payload = reassemble_packets(pending.values())
        except PacketError:
            session.pending.pop(request_id, None)
            return
        session.pending.pop(request_id, None)
        self._dispatch(session, packet.opcode, request_id, payload)

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self._socket.close()
        self.backend.close()

    def _handle_hello(self, datagram: bytes, address: tuple[str, int]) -> None:
        try:
            packet = Packet.decode(datagram, self.key)
            request = HelloRequest.decode(packet.payload)
        except (PacketError, ValueError):
            return
        server_nonce = self._nonce_factory() & 0xFFFFFFFF
        session_id = (server_nonce ^ request.client_nonce) & 0xFFFF
        if session_id == 0:
            session_id = 1
        response = HelloResponse(
            client_nonce=request.client_nonce,
            server_nonce=server_nonce,
            session_id=session_id,
        )
        response_packet = Packet(
            kind=MessageKind.RESPONSE,
            opcode=Opcode.HELLO,
            session_id=session_id,
            request_id=packet.request_id,
            fragment_index=0,
            fragment_count=1,
            payload=response.encode(),
        )
        self._socket.sendto(response_packet.encode(self.key), address)
        self._session = _Session(
            address=address,
            session_id=session_id,
            key=derive_session_key(self.key, request.client_nonce, server_nonce),
        )

    def _dispatch(
        self,
        session: _Session,
        opcode: Opcode,
        request_id: int,
        payload: bytes,
    ) -> None:
        try:
            if opcode is Opcode.GET_STATUS:
                _require_empty(payload)
                response = _status_message(self.backend.get_status()).encode()
            elif opcode is Opcode.GET_CAPABILITIES:
                _require_empty(payload)
                response = _capabilities_message(
                    self.backend.get_capabilities()
                ).encode()
            elif opcode is Opcode.CAPTURE_TEXT_SCREEN:
                _require_empty(payload)
                response = _screen_message(self.backend.capture_screen()).encode()
            elif opcode is Opcode.SEND_KEYS:
                request = KeyRequest.decode(payload)
                receipt = self.backend.send_keys(
                    text=request.text.decode("cp437"),
                    keys=tuple(key.name for key in request.keys),
                    inter_key_delay_ms=request.inter_key_delay_ms,
                    settle_ms=100,
                )
                response = KeyResponse(
                    receipt.accepted_text_bytes,
                    receipt.accepted_keys,
                    receipt.screen_generation & 0xFFFF,
                ).encode()
            elif opcode is Opcode.PING:
                response = payload
            else:
                self._send_error(
                    session,
                    opcode,
                    request_id,
                    ErrorCode.UNSUPPORTED_OPERATION,
                    "operation is not implemented",
                )
                return
        except (UnicodeError, ValueError) as exc:
            self._send_error(
                session,
                opcode,
                request_id,
                ErrorCode.INVALID_ARGUMENT,
                str(exc),
            )
            return
        except Exception:
            self._send_error(
                session,
                opcode,
                request_id,
                ErrorCode.INTERNAL_FAILURE,
                "target operation failed",
            )
            return
        self._send_response(session, opcode, request_id, response)

    def _send_response(
        self,
        session: _Session,
        opcode: Opcode,
        request_id: int,
        payload: bytes,
        *,
        kind: MessageKind = MessageKind.RESPONSE,
    ) -> None:
        packets = fragment_message(
            kind=kind,
            opcode=opcode,
            session_id=session.session_id,
            request_id=request_id,
            payload=payload,
        )
        encoded = tuple(packet.encode(session.key) for packet in packets)
        session.last_request_id = request_id
        session.response_cache[request_id] = encoded
        while len(session.response_cache) > 4:
            session.response_cache.pop(next(iter(session.response_cache)))
        if self._drop_first_response and not self._dropped_response:
            self._dropped_response = True
            return
        for datagram in encoded:
            self._socket.sendto(datagram, session.address)

    def _send_cached(self, session: _Session, request_id: int) -> None:
        for datagram in session.response_cache[request_id]:
            self._socket.sendto(datagram, session.address)

    def _send_error(
        self,
        session: _Session,
        opcode: Opcode,
        request_id: int,
        code: ErrorCode,
        message: str,
    ) -> None:
        self._send_response(
            session,
            opcode,
            request_id,
            encode_error(code, message),
            kind=MessageKind.ERROR,
        )


def _request_is_newer(candidate: int, previous: int) -> bool:
    distance = (candidate - previous) & 0xFFFF
    return 0 < distance < 0x8000


def _require_empty(payload: bytes) -> None:
    if payload:
        raise ValueError("operation takes no payload")


def _status_message(status: MachineStatus) -> StatusMessage:
    phase = {
        "starting": Phase.STARTING,
        "observe_ready": Phase.OBSERVE_READY,
        "agent_shell_ready": Phase.AGENT_SHELL_READY,
        "child_running": Phase.CHILD_RUNNING,
        "dos_busy": Phase.DOS_BUSY,
        "awaiting_approval": Phase.AWAITING_APPROVAL,
        "host_unresponsive": Phase.HOST_UNRESPONSIVE,
    }.get(status.phase, Phase.OBSERVE_READY)
    return StatusMessage(
        agent_major=0,
        agent_minor=1,
        dos_major=0,
        dos_minor=0,
        cpu_class=0xFF,
        phase=phase,
        conventional_kb=640,
        bios_ticks=int(status.uptime_seconds * 18.2) & 0xFFFFFFFF,
    )


def _capabilities_message(capabilities: Capabilities) -> CapabilitiesMessage:
    flags = Capability(0)
    mapping = (
        ("status", Capability.STATUS),
        ("text_capture", Capability.TEXT_CAPTURE),
        ("filesystem_read", Capability.FILESYSTEM_READ),
        ("filesystem_write", Capability.FILESYSTEM_WRITE),
        ("command_execution", Capability.EXECUTION),
        ("memory_read", Capability.MEMORY_READ),
        ("memory_write", Capability.MEMORY_WRITE),
        ("port_read", Capability.PORT_READ),
        ("port_write", Capability.PORT_WRITE),
        ("reboot", Capability.REBOOT),
    )
    for name, flag in mapping:
        if getattr(capabilities, name):
            flags |= flag
    if capabilities.keyboard_injection:
        flags |= Capability.KEYBOARD
    adapter = (
        Adapter.LINUX_PTY
        if capabilities.backend == "linux-terminal"
        else Adapter.UNKNOWN
    )
    return CapabilitiesMessage(
        capabilities=flags,
        columns=capabilities.screen_columns,
        rows=capabilities.screen_rows,
        adapter=adapter,
        max_fragment_payload=1024,
        max_keys=capabilities.max_keys_per_request,
    )


def _screen_message(screen: TextScreen) -> ScreenMessage:
    cells = bytearray()
    for row, attributes in zip(screen.text, screen.attributes, strict=True):
        encoded = row.encode("cp437", errors="replace")
        for character, attribute in zip(encoded, attributes, strict=True):
            cells.extend((character, attribute))
    adapter = {
        "MDA": Adapter.MDA,
        "CGA": Adapter.CGA,
        "EGA": Adapter.EGA,
        "VGA": Adapter.VGA,
        "linux-pty": Adapter.LINUX_PTY,
    }.get(screen.adapter, Adapter.UNKNOWN)
    return ScreenMessage(
        columns=screen.columns,
        rows=screen.rows,
        video_mode=0xFF if screen.video_mode is None else screen.video_mode,
        active_page=screen.active_page,
        cursor_row=screen.cursor.row,
        cursor_column=screen.cursor.column,
        cursor_start=(
            0 if screen.cursor.start_scanline is None else screen.cursor.start_scanline
        ),
        cursor_end=0 if screen.cursor.end_scanline is None else screen.cursor.end_scanline,
        adapter=adapter,
        code_page=437,
        generation=screen.generation & 0xFFFF,
        cells=bytes(cells),
    )
