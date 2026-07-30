from dos_mcp.terminal import TerminalBuffer


def test_terminal_tracks_text_and_cursor() -> None:
    terminal = TerminalBuffer(columns=10, rows=3)

    terminal.feed("Hello\r\nDOS")
    screen = terminal.snapshot()

    assert screen.text[0] == "Hello     "
    assert screen.text[1] == "DOS       "
    assert screen.cursor.row == 1
    assert screen.cursor.column == 3


def test_terminal_scrolls_without_changing_dimensions() -> None:
    terminal = TerminalBuffer(columns=8, rows=2)

    terminal.feed("first\r\nsecond\r\nthird")
    screen = terminal.snapshot()

    assert screen.text == ("second  ", "third   ")
    assert screen.cursor.row == 1
    assert screen.cursor.column == 5


def test_terminal_applies_basic_ansi_color_and_erase() -> None:
    terminal = TerminalBuffer(columns=8, rows=2)

    terminal.feed("\x1b[31mR\x1b[0mX")
    colored = terminal.snapshot()
    terminal.feed("\x1b[2J\x1b[H")
    cleared = terminal.snapshot()

    assert colored.attributes[0][0] == 0x04
    assert colored.attributes[0][1] == 0x07
    assert cleared.text == (" " * 8, " " * 8)
    assert cleared.cursor.row == 0
    assert cleared.cursor.column == 0


def test_terminal_ignores_oversized_escape_sequence() -> None:
    terminal = TerminalBuffer(columns=8, rows=2)

    terminal.feed("\x1b[" + ("1" * 80) + "Z")

    assert terminal.snapshot().text == (" " * 8, " " * 8)
