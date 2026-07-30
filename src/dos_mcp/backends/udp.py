"""Authenticated reliable request/response backend for a DOS UDP agent."""

from __future__ import annotations

import secrets
import socket
import threading
import time
import zlib
from contextlib import suppress

from dos_mcp.backend import Backend
from dos_mcp.models import (
    Capabilities,
    Cursor,
    FileContents,
    FileReceipt,
    GraphicsScreen,
    KeyReceipt,
    MachineStatus,
    TextScreen,
)
from dos_mcp.protocol import (
    Adapter,
    CapabilitiesMessage,
    Capability,
    FileReadBeginRequest,
    FileWriteBeginRequest,
    GraphicsBeginResponse,
    GraphicsLayout,
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
    TransferBeginResponse,
    TransferBlockRequest,
    TransferBlockResponse,
    TransferEndRequest,
    TransferEndResponse,
    derive_session_key,
    fragment_message,
    reassemble_packets,
)
from dos_mcp.protocol.constants import KEY_NAMES, MAX_DATAGRAM
from dos_mcp.protocol.errors import decode_error


def _dos_transport_datagram(datagram: bytes) -> bytes:
    """Pad odd requests for packet drivers that use word-wide NIC DMA."""
    return datagram if len(datagram) % 2 == 0 else datagram + b"\x00"


class AgentOperationError(RuntimeError):
    pass


MAX_FILE_SIZE = 1_048_576
MAX_GRAPHICS_SIZE = 262_144
TRANSFER_BLOCK_SIZE = 900


class UdpBackend(Backend):
    def __init__(
        self,
        *,
        target: tuple[str, int],
        key: bytes,
        timeout: float = 0.35,
        retries: int = 3,
        allow_file_read: bool = False,
        allow_file_write: bool = False,
    ) -> None:
        if len(key) != 16:
            raise ValueError("UDP backend key must be 16 bytes")
        if timeout <= 0 or retries < 0:
            raise ValueError("invalid timeout or retry count")
        self.target = (socket.gethostbyname(target[0]), target[1])
        self._base_key = key
        self._timeout = timeout
        self._retries = retries
        self._allow_file_read = allow_file_read
        self._allow_file_write = allow_file_write
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
            graphics_capture=(
                ("CGA", "Hercules", "EGA", "VGA")
                if flags & Capability.GRAPHICS_CAPTURE
                else ()
            ),
            keyboard_injection=(
                "bios-queue" if flags & Capability.KEYBOARD else None
            ),
            screen_columns=message.columns,
            screen_rows=message.rows,
            max_text_bytes=4096,
            max_keys_per_request=message.max_keys,
            filesystem_read=bool(flags & Capability.FILESYSTEM_READ)
            and self._allow_file_read,
            filesystem_write=bool(flags & Capability.FILESYSTEM_WRITE)
            and self._allow_file_write,
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

    def capture_graphics(self) -> GraphicsScreen:
        metadata = GraphicsBeginResponse.decode(self._request(Opcode.GRAPHICS_BEGIN))
        if metadata.total_size > MAX_GRAPHICS_SIZE:
            self._abort_transfer(metadata.transfer_id)
            raise ValueError("graphics capture exceeds the bridge size limit")
        try:
            data, crc32 = self._download_blocks(
                opcode=Opcode.GRAPHICS_BLOCK,
                transfer_id=metadata.transfer_id,
                total_size=metadata.total_size,
            )
            end = TransferEndResponse.decode(
                self._request(
                    Opcode.GRAPHICS_END,
                    TransferEndRequest(metadata.transfer_id).encode(),
                )
            )
        except BaseException:
            self._abort_transfer(metadata.transfer_id)
            raise
        if end.total_size != len(data) or end.crc32 != crc32:
            raise AgentOperationError("graphics capture checksum mismatch")
        adapter = {
            Adapter.MDA: "MDA",
            Adapter.CGA: "CGA",
            Adapter.EGA: "EGA",
            Adapter.VGA: "VGA",
            Adapter.UNKNOWN: "unknown",
            Adapter.LINUX_PTY: "linux-pty",
        }[metadata.adapter]
        layout = {
            GraphicsLayout.CGA_2BPP_INTERLEAVED: "cga-2bpp-interleaved",
            GraphicsLayout.CGA_1BPP_INTERLEAVED: "cga-1bpp-interleaved",
            GraphicsLayout.HERCULES_1BPP_INTERLEAVED: "hercules-1bpp-interleaved",
            GraphicsLayout.PLANAR_4BPP: "planar-4bpp",
            GraphicsLayout.PLANAR_1BPP: "planar-1bpp",
            GraphicsLayout.PACKED_8BPP: "packed-8bpp",
        }[metadata.layout]
        return GraphicsScreen(
            adapter=adapter,
            video_mode=metadata.video_mode,
            layout=layout,
            width=metadata.width,
            height=metadata.height,
            planes=metadata.planes,
            bytes_per_plane=metadata.bytes_per_plane,
            data=data,
            crc32=crc32,
        )

    def download_file(self, *, path: str) -> FileContents:
        if not self._allow_file_read:
            raise PermissionError("DOS file downloads are disabled by bridge policy")
        encoded_path = _encode_dos_path(path)
        begin = TransferBeginResponse.decode(
            self._request(
                Opcode.FILE_READ_BEGIN,
                FileReadBeginRequest(encoded_path).encode(),
            )
        )
        if begin.total_size > MAX_FILE_SIZE:
            self._abort_transfer(begin.transfer_id)
            raise ValueError("file exceeds the bridge download limit")
        try:
            data, crc32 = self._download_blocks(
                opcode=Opcode.FILE_READ_BLOCK,
                transfer_id=begin.transfer_id,
                total_size=begin.total_size,
            )
            end = TransferEndResponse.decode(
                self._request(
                    Opcode.FILE_READ_END,
                    TransferEndRequest(begin.transfer_id).encode(),
                )
            )
        except BaseException:
            self._abort_transfer(begin.transfer_id)
            raise
        if end.total_size != len(data) or end.crc32 != crc32:
            raise AgentOperationError("download checksum mismatch")
        return FileContents(path=path, data=data, crc32=crc32)

    def upload_file(
        self,
        *,
        path: str,
        data: bytes,
        overwrite: bool,
    ) -> FileReceipt:
        if not self._allow_file_write:
            raise PermissionError("DOS file uploads are disabled by bridge policy")
        if len(data) > MAX_FILE_SIZE:
            raise ValueError("file exceeds the bridge upload limit")
        expected_crc = zlib.crc32(data)
        begin = TransferBeginResponse.decode(
            self._request(
                Opcode.FILE_WRITE_BEGIN,
                FileWriteBeginRequest(
                    path=_encode_dos_path(path),
                    total_size=len(data),
                    crc32=expected_crc,
                    overwrite=overwrite,
                ).encode(),
            )
        )
        try:
            rolling_crc = 0
            for offset in range(0, len(data), TRANSFER_BLOCK_SIZE):
                block = data[offset : offset + TRANSFER_BLOCK_SIZE]
                response = TransferBlockResponse.decode(
                    self._request(
                        Opcode.FILE_WRITE_BLOCK,
                        TransferBlockRequest(
                            begin.transfer_id,
                            offset,
                            len(block),
                            block,
                        ).encode(),
                    )
                )
                rolling_crc = zlib.crc32(block, rolling_crc)
                if (
                    response.transfer_id != begin.transfer_id
                    or response.offset != offset
                    or response.data
                    or response.rolling_crc32 != rolling_crc
                ):
                    raise AgentOperationError("invalid upload block acknowledgement")
            end = TransferEndResponse.decode(
                self._request(
                    Opcode.FILE_WRITE_COMMIT,
                    TransferEndRequest(begin.transfer_id).encode(),
                )
            )
        except BaseException:
            self._abort_transfer(begin.transfer_id)
            raise
        if end.total_size != len(data) or end.crc32 != expected_crc:
            raise AgentOperationError("upload checksum mismatch")
        return FileReceipt(path=path, size=end.total_size, crc32=end.crc32)

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
        encoded = tuple(_dos_transport_datagram(packet.encode(key)) for packet in packets)
        expected = packets[0]
        received: dict[int, Packet] = {}
        ignored = 0
        last_ignored = ""
        for _ in range(self._retries + 1):
            for datagram in encoded:
                self._socket.sendto(datagram, self.target)
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
                except PacketError as exc:
                    ignored += 1
                    last_ignored = str(exc)
                    continue
                if (
                    packet.request_id != expected.request_id
                    or packet.opcode is not expected.opcode
                    or (
                        expected.opcode is not Opcode.HELLO
                        and packet.session_id != expected.session_id
                    )
                ):
                    ignored += 1
                    last_ignored = (
                        f"unexpected packet {packet.opcode.name}/"
                        f"{packet.request_id}/{packet.session_id}"
                    )
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
            f"DOS agent did not complete {expected.opcode.name} after "
            f"{self._retries + 1} attempts "
            f"({len(received)} response fragments received, {ignored} ignored"
            f"{f': {last_ignored}' if last_ignored else ''})"
        )

    def _ensure_open(self) -> None:
        if self._closed:
            raise RuntimeError("UDP backend is closed")

    def _download_blocks(
        self,
        *,
        opcode: Opcode,
        transfer_id: int,
        total_size: int,
    ) -> tuple[bytes, int]:
        output = bytearray()
        rolling_crc = 0
        while len(output) < total_size:
            length = min(TRANSFER_BLOCK_SIZE, total_size - len(output))
            response = TransferBlockResponse.decode(
                self._request(
                    opcode,
                    TransferBlockRequest(transfer_id, len(output), length).encode(),
                )
            )
            if (
                response.transfer_id != transfer_id
                or response.offset != len(output)
                or not response.data
                or len(response.data) > length
            ):
                raise AgentOperationError("invalid download block response")
            output.extend(response.data)
            rolling_crc = zlib.crc32(response.data, rolling_crc)
            if response.rolling_crc32 != rolling_crc:
                raise AgentOperationError("download block checksum mismatch")
        return bytes(output), rolling_crc

    def _abort_transfer(self, transfer_id: int) -> None:
        with suppress(OSError, RuntimeError, TimeoutError):
            self._request(
                Opcode.FILE_ABORT,
                TransferEndRequest(transfer_id).encode(),
            )


def _encode_dos_path(path: str) -> bytes:
    try:
        encoded = path.encode("cp437")
    except UnicodeEncodeError as exc:
        raise ValueError("DOS path contains characters outside CP437") from exc
    if not encoded:
        raise ValueError("DOS path must not be empty")
    return encoded
