import pytest

from dos_mcp.models import Cursor, TextScreen


def test_text_screen_requires_fixed_width_rows() -> None:
    with pytest.raises(ValueError, match="exactly columns"):
        TextScreen(
            columns=3,
            rows=1,
            text=("ab",),
            attributes=((7, 7, 7),),
            cursor=Cursor(0, 0),
            generation=0,
            adapter="test",
            video_mode=3,
            active_page=0,
            code_page="CP437",
            blink_enabled=True,
        )


def test_text_screen_serializes_tuples_for_mcp() -> None:
    screen = TextScreen(
        columns=2,
        rows=1,
        text=("A ",),
        attributes=((0x1F, 0x07),),
        cursor=Cursor(0, 1),
        generation=4,
        adapter="CGA",
        video_mode=3,
        active_page=0,
        code_page="CP437",
        blink_enabled=True,
    )

    value = screen.to_dict()

    assert value["text"] == ["A "]
    assert value["attributes"] == [[0x1F, 0x07]]
    assert value["cursor"]["column"] == 1
