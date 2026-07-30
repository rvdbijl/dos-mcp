"""Authenticated reliable request/response backend for a DOS UDP agent."""

from __future__ import annotations

import secrets
import socket
import threading
import time

from dos_mcp.backend import Backend
from dos_mcp.models import Capabilities, Cursor, KeyReceipt, MachineStatus, TextScreen
from dos_mcp.protocol import (
    Adapter,
    CapabilitiesMessage,
    Capability,
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
from dos_mcp.protocol.constants import KEY_NAMES, MAX_DATAGRAM
from dos_mcp.protocol.errors import decode_error


class AgentOperationError(RuntimeError):
    pass


class UdpBackend(Backend):
    def __init__(
        self,
        *,
        target: tuple[str, int],
        key: bytes,
        timeout: float = 0.35,
        retries: int = 3,
    ) -> None:
        if len(key) != 16:
            raise ValueError("UDP backend key must be 16 bytes")
        if timeout <= 0 or retries < 0:
            raise ValueError("invalid timeout or retry count")
        self.target = (socket.gethostbyname(target[0]), target[1])
        self._base_key = key
        self._timeout = timeout
        self._retries = retries
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.bind(("0.0.0.0", 0))
        self._session_id = 0
        self._session_key: bytes | None = None
        self._request_id = secrets.randbelow(0xFFFF) or 1
        self._closed = False
        self._lock = threading.Lock()

    def get_status(self) -> MachineStatus:
        message = StatusMessage.decode(self._request(Opcode.GET_STATUS))
        phase = {
            Phase.STARTING: "starting",
            Phase.OBSERVE_READY: "observe_ready",
            Phase.AGENT_SHELL_READY: "agent_shell_ready",
            Phase.CHILD_RUNNING: "child_running",
            Phase.DOS_BUSY: "dos_busy",
            Phase.AWAITING_APPROVAL: "awaiting_approval",
            Phase.HOST_UNRESPONSIVE: "host_unresponsive",
        }[message.phase]
        return MachineStatus(
            connected=True,
            phase=phase,
            backend="dos-agent",
            transport="packet-driver-udp",
            identity=f"{self.target[0]}:{self.target[1]}",
            operating_system=f"DOS {message.dos_major}.{message.dos_minor}",
            architecture="8088" if message.cpu_class == 0 else f"x86-class-{message.cpu_class}",
            agent_version=f"{message.agent_major}.{message.agent_minor}",
            uptime_seconds=round(message.bios_ticks / 18.2065, 3),
        )

    def get_capabilities(self) -> Capabilities:
        message = CapabilitiesMessage.decode(self._request(Opcode.GET_CAPABILITIES))
        flags = message.capabilities
        return Capabilities(
            backend="dos-agent",
            transport="packet-driver-udp",
            status=bool(flags & Capability.STATUS),
            text_capture=bool(flags & Capability.TEXT_CAPTURE),
            graphics_capture=(),
            keyboard_injection=(
                "bios-queue" if flags & Capability.KEYBOARD else None
            ),
            screen_columns=message.columns,
            screen_rows=message.rows,
            max_text_bytes=4096,
            max_keys_per_request=message.max_keys,
            filesystem_read=bool(flags & Capability.FILESYSTEM_READ),
            filesystem_write=bool(flags & Capability.FILESYSTEM_WRITE),
            command_execution=bool(flags & Capability.EXECUTION),
            memory_read=bool(flags & Capability.MEMORY_READ),
            memory_write=bool(flags & Capability.MEMORY_WRITE),
            port_read=bool(flags & Capability.PORT_READ),
            port_write=bool(flags & Capability.PORT_WRITE),
            reboot=bool(flags & Capability.REBOOT),
        )

    def capture_screen(self) -> TextScreen:
        message = ScreenMessage.decode(self._request(Opcode.CAPTURE_TEXT_SCREEN))
        rows: list[str] = []
        attributes: list[tuple[int, ...]] = []
        for row_index in range(message.rows):
            start = row_index * message.columns * 2
            cells = message.cells[start : start + message.columns * 2]
            rows.append(bytes(cells[0::2]).decode("cp437"))
            attributes.append(tuple(cells[1::2]))
        adapter = {
            Adapter.MDA: "MDA",
            Adapter.CGA: "CGA",
            Adapter.EGA: "EGA",
            Adapter.VGA: "VGA",
            Adapter.LINUX_PTY: "linux-pty",
            Adapter.UNKNOWN: "unknown",
        }[message.adapter]
        return TextScreen(
            columns=message.columns,
            rows=message.rows,
            text=tuple(rows),
            attributes=tuple(attributes),
            cursor=Cursor(
                row=message.cursor_row,
                column=message.cursor_column,
                visible=message.cursor_start <= message.cursor_end,
                start_scanline=message.cursor_start,
                end_scanline=message.cursor_end,
            ),
            generation=message.generation,
            adapter=adapter,
            video_mode=None if message.video_mode == 0xFF else message.video_mode,
            active_page=message.active_page,
            code_page=f"CP{message.code_page}",
            blink_enabled=True,
        )

    def send_keys(
        self,
        *,
        text: str,
        keys: tuple[str, ...],
        inter_key_delay_ms: int,
        settle_ms: int,
    ) -> KeyReceipt:
        try:
            encoded_text = text.encode("cp437")
        except UnicodeEncodeError as exc:
            raise ValueError("text contains characters outside CP437") from exc
        try:
            encoded_keys = tuple(KEY_NAMES[key.upper()] for key in keys)
        except KeyError as exc:
            raise ValueError(f"unsupported named key: {exc.args[0]}") from exc
        request = KeyRequest(
            text=encoded_text,
            keys=encoded_keys,
            inter_key_delay_ms=inter_key_delay_ms,
        )
        response = KeyResponse.decode(self._request(Opcode.SEND_KEYS, request.encode()))
        if settle_ms:
            time.sleep(settle_ms / 1000)
        return KeyReceipt(
            accepted_text_bytes=response.accepted_text_bytes,
            accepted_keys=response.accepted_keys,
            keys=tuple(key.name for key in encoded_keys[: response.accepted_keys]),
            screen_generation=response.generation,
        )

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self._socket.close()

    def _request(self, opcode: Opcode, payload: bytes = b"") -> bytes:
        with self._lock:
            self._ensure_open()
            if self._session_key is None:
                self._handshake()
            self._request_id = (self._request_id + 1) & 0xFFFF or 1
            packets = fragment_message(
                kind=MessageKind.REQUEST,
                opcode=opcode,
                session_id=self._session_id,
                request_id=self._request_id,
                payload=payload,
            )
            return self._exchange(packets, self._session_key)

    def _handshake(self) -> None:
        client_nonce = secrets.randbits(32)
        self._request_id = (self._request_id + 1) & 0xFFFF or 1
        request = Packet(
            kind=MessageKind.REQUEST,
            opcode=Opcode.HELLO,
            session_id=0,
            request_id=self._request_id,
            fragment_index=0,
            fragment_count=1,
            payload=HelloRequest(client_nonce).encode(),
        )
        response_payload, response_packet = self._exchange_with_packet(
            (request,), self._base_key
        )
        response = HelloResponse.decode(response_payload)
        if (
            response.client_nonce != client_nonce
            or response.session_id != response_packet.session_id
        ):
            raise AgentOperationError("invalid hello response")
        self._session_id = response.session_id
        self._session_key = derive_session_key(
            self._base_key, client_nonce, response.server_nonce
        )

    def _exchange(self, packets: tuple[Packet, ...], key: bytes) -> bytes:
        payload, _ = self._exchange_with_packet(packets, key)
        return payload

    def _exchange_with_packet(
        self, packets: tuple[Packet, ...], key: bytes
    ) -> tuple[bytes, Packet]:
        encoded = tuple(packet.encode(key) for packet in packets)
        expected = packets[0]
        for _ in range(self._retries + 1):
            for datagram in encoded:
                self._socket.sendto(datagram, self.target)
            received: dict[int, Packet] = {}
            deadline = time.monotonic() + self._timeout
            while time.monotonic() < deadline:
                self._socket.settimeout(max(0.001, deadline - time.monotonic()))
                try:
                    datagram, address = self._socket.recvfrom(MAX_DATAGRAM + 1)
                except TimeoutError:
                    break
                if address != self.target or len(datagram) > MAX_DATAGRAM:
                    continue
                try:
                    packet = Packet.decode(datagram, key)
                except PacketError:
                    continue
                if (
                    packet.request_id != expected.request_id
                    or packet.opcode is not expected.opcode
                    or (
                        expected.opcode is not Opcode.HELLO
                        and packet.session_id != expected.session_id
                    )
                ):
                    continue
                if packet.kind is MessageKind.ERROR:
                    code, message = decode_error(packet.payload)
                    raise AgentOperationError(f"{code.name}: {message}")
                if packet.kind is not MessageKind.RESPONSE:
                    continue
                received[packet.fragment_index] = packet
                if len(received) == packet.fragment_count:
                    return reassemble_packets(received.values()), packet
        raise TimeoutError(
            f"DOS agent did not answer {expected.opcode.name} after {self._retries + 1} attempts"
        )

    def _ensure_open(self) -> None:
        if self._closed:
            raise RuntimeError("UDP backend is closed")
