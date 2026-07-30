import pytest

from dos_mcp.protocol import (
    Adapter,
    CapabilitiesMessage,
    Capability,
    HelloRequest,
    HelloResponse,
    KeyCode,
    KeyRequest,
    KeyResponse,
    Phase,
    ScreenMessage,
    StatusMessage,
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


def test_key_request_rejects_unknown_key_code() -> None:
    encoded = KeyRequest(b"", (), 0).encode() + b"\xff"
    encoded = encoded[:2] + b"\x01" + encoded[3:]

    with pytest.raises(ValueError):
        KeyRequest.decode(encoded)
