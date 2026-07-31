import pytest

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
    KeyCode,
    KeyRequest,
    KeyResponse,
    Phase,
    ResidentDiagnosticFlag,
    ResidentDiagnosticsMessage,
    ScreenMessage,
    StatusMessage,
    TransferBeginResponse,
    TransferBlockRequest,
    TransferBlockResponse,
    TransferEndRequest,
    TransferEndResponse,
)


@pytest.mark.parametrize(
    "value",
    [
        HelloRequest(123),
        HelloResponse(123, 456, 7),
        StatusMessage(0, 1, 6, 22, 0, Phase.OBSERVE_READY, 640, 987654),
        CapabilitiesMessage(
            Capability.STATUS | Capability.TEXT_CAPTURE | Capability.KEYBOARD,
            80,
            25,
            Adapter.CGA,
            1024,
            15,
        ),
        KeyRequest(b"DIR", (KeyCode.ENTER,), 10),
        KeyResponse(3, 1, 99),
        ResidentDiagnosticsMessage(
            version=1,
            flags=(
                ResidentDiagnosticFlag.OWNS_INT08
                | ResidentDiagnosticFlag.OWNS_INT1C
                | ResidentDiagnosticFlag.ENABLED
            ),
            int08_entries=100,
            int1c_entries=99,
            int28_entries=5,
            worker_runs=104,
            fallback_runs=1,
            busy_skips=2,
            receive_allocations=10,
            receive_completions=9,
            receive_drops=1,
            send_attempts=8,
            send_failures=0,
            last_receive_bios_tick=1234,
            last_worker_bios_tick=1235,
            worker_ticks=104,
            receive_length=60,
            master_pic_mask=0xF8,
            slave_pic_mask=0xFB,
            last_protocol_result=0,
            last_send_result=-2,
            last_opcode=18,
        ),
    ],
)
def test_fixed_messages_round_trip(value: object) -> None:
    assert type(value).decode(value.encode()) == value


def test_screen_round_trip() -> None:
    screen = ScreenMessage(
        columns=2,
        rows=2,
        video_mode=3,
        active_page=0,
        cursor_row=1,
        cursor_column=1,
        cursor_start=6,
        cursor_end=7,
        adapter=Adapter.CGA,
        code_page=437,
        generation=44,
        cells=b"A\x07B\x1fC\x70D\x87",
    )

    assert ScreenMessage.decode(screen.encode()) == screen


def test_screen_rejects_wrong_cell_count() -> None:
    with pytest.raises(ValueError, match="cell bytes"):
        ScreenMessage(
            columns=80,
            rows=25,
            video_mode=3,
            active_page=0,
            cursor_row=0,
            cursor_column=0,
            cursor_start=6,
            cursor_end=7,
            adapter=Adapter.CGA,
            code_page=437,
            generation=1,
            cells=b"",
        ).encode()


@pytest.mark.parametrize(("cursor_row", "cursor_column"), [(2, 0), (0, 2)])
def test_screen_rejects_cursor_outside_grid(
    cursor_row: int,
    cursor_column: int,
) -> None:
    with pytest.raises(ValueError, match="cursor is outside screen"):
        ScreenMessage(
            columns=2,
            rows=2,
            video_mode=3,
            active_page=0,
            cursor_row=cursor_row,
            cursor_column=cursor_column,
            cursor_start=1,
            cursor_end=0,
            adapter=Adapter.CGA,
            code_page=437,
            generation=1,
            cells=b"\0\0" * 4,
        ).encode()


def test_key_request_rejects_unknown_key_code() -> None:
    encoded = KeyRequest(b"", (), 0).encode() + b"\xff"
    encoded = encoded[:2] + b"\x01" + encoded[3:]

    with pytest.raises(ValueError):
        KeyRequest.decode(encoded)


def test_file_and_transfer_messages_round_trip() -> None:
    file_read = FileReadBeginRequest(b"RESULT.BIN")
    file_write = FileWriteBeginRequest(b"INPUT.BIN", 3, 0x12345678, True)
    begin = TransferBeginResponse(7, 3)
    block_response = TransferBlockResponse(7, 0, b"DOS", 0x87654321)
    end_request = TransferEndRequest(7)
    end_response = TransferEndResponse(3, 0x87654321)

    assert FileReadBeginRequest.decode(file_read.encode()) == file_read
    assert FileWriteBeginRequest.decode(file_write.encode()) == file_write
    assert file_write.encode()[:10].hex() == "01030000007856341209"
    assert TransferBeginResponse.decode(begin.encode()) == begin
    assert TransferBlockResponse.decode(block_response.encode()) == block_response
    assert TransferEndRequest.decode(end_request.encode()) == end_request
    assert TransferEndResponse.decode(end_response.encode()) == end_response

    download_request = TransferBlockRequest(7, 0, 3)
    upload_request = TransferBlockRequest(7, 0, 3, b"DOS")
    assert (
        TransferBlockRequest.decode(download_request.encode(), has_data=False)
        == download_request
    )
    assert (
        TransferBlockRequest.decode(upload_request.encode(), has_data=True)
        == upload_request
    )


def test_graphics_metadata_round_trip_and_consistency() -> None:
    value = GraphicsBeginResponse(
        transfer_id=9,
        adapter=Adapter.VGA,
        video_mode=0x12,
        layout=GraphicsLayout.PLANAR_4BPP,
        planes=4,
        width=640,
        height=480,
        total_size=153600,
        bytes_per_plane=38400,
    )

    assert GraphicsBeginResponse.decode(value.encode()) == value

    inconsistent = bytearray(value.encode())
    inconsistent[5] = 1
    with pytest.raises(ValueError, match="graphics metadata"):
        GraphicsBeginResponse.decode(bytes(inconsistent))


def test_capabilities_reject_unknown_flags() -> None:
    encoded = bytearray(
        CapabilitiesMessage(
            Capability.STATUS,
            80,
            25,
            Adapter.CGA,
            1024,
            15,
        ).encode()
    )
    encoded[3] |= 0x80

    with pytest.raises(ValueError, match="capability flags"):
        CapabilitiesMessage.decode(bytes(encoded))
