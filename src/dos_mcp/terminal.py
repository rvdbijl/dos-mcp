"""Small bounded terminal model for the Linux development backend."""

from __future__ import annotations

import codecs

from .models import Cursor, TextScreen


class TerminalBuffer:
    """Render a conservative ANSI subset into a fixed-size cell buffer."""

    def __init__(self, columns: int = 80, rows: int = 25) -> None:
        if columns < 1 or rows < 1:
            raise ValueError("terminal dimensions must be positive")
        self.columns = columns
        self.rows = rows
        self._chars = [[" "] * columns for _ in range(rows)]
        self._attrs = [[0x07] * columns for _ in range(rows)]
        self.row = 0
        self.column = 0
        self.visible = True
        self.generation = 0
        self._foreground = 7
        self._background = 0
        self._bold = False
        self._blink = False
        self._state = "normal"
        self._sequence = ""
        self._decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")

    @property
    def attribute(self) -> int:
        foreground = self._foreground | (0x08 if self._bold else 0)
        background = self._background << 4
        return foreground | background | (0x80 if self._blink else 0)

    def feed_bytes(self, data: bytes) -> None:
        self.feed(self._decoder.decode(data))

    def feed(self, value: str) -> None:
        if not value:
            return
        before = self.generation
        for character in value:
            if self._state == "normal":
                self._normal(character)
            elif self._state == "escape":
                self._escape(character)
            elif self._state == "csi":
                self._csi(character)
            elif self._state == "csi_discard":
                if "@" <= character <= "~":
                    self._state = "normal"
            elif self._state == "osc":
                if character == "\x07":
                    self._state = "normal"
                elif character == "\x1b":
                    self._state = "osc_escape"
            elif self._state == "osc_escape":
                self._state = "normal" if character == "\\" else "osc"
        if self.generation == before:
            self.generation += 1

    def snapshot(self) -> TextScreen:
        return TextScreen(
            columns=self.columns,
            rows=self.rows,
            text=tuple("".join(row) for row in self._chars),
            attributes=tuple(tuple(row) for row in self._attrs),
            cursor=Cursor(row=self.row, column=self.column, visible=self.visible),
            generation=self.generation,
            adapter="linux-pty",
            video_mode=None,
            active_page=0,
            code_page="UTF-8",
            blink_enabled=False,
        )

    def _normal(self, character: str) -> None:
        if character == "\x1b":
            self._state = "escape"
        elif character == "\r":
            self.column = 0
        elif character in {"\n", "\v", "\f"}:
            self._line_feed()
        elif character == "\b":
            self.column = max(0, self.column - 1)
        elif character == "\t":
            next_stop = min(self.columns - 1, ((self.column // 8) + 1) * 8)
            self.column = next_stop
        elif character == "\x07" or ord(character) < 0x20 or character == "\x7f":
            return
        else:
            self._put(character)

    def _escape(self, character: str) -> None:
        if character == "[":
            self._state = "csi"
            self._sequence = ""
        elif character == "]":
            self._state = "osc"
            self._sequence = ""
        elif character in {"7", "8"}:
            self._state = "normal"
        else:
            self._state = "normal"

    def _csi(self, character: str) -> None:
        if "@" <= character <= "~":
            sequence = self._sequence
            self._sequence = ""
            self._state = "normal"
            self._apply_csi(sequence, character)
        elif len(self._sequence) < 64:
            self._sequence += character
        else:
            self._sequence = ""
            self._state = "csi_discard"

    def _apply_csi(self, raw: str, final: str) -> None:
        private = raw.startswith("?")
        cleaned = raw[1:] if private else raw
        params = [int(part) if part.isdigit() else 0 for part in cleaned.split(";")]
        if not params:
            params = [0]

        if final in {"H", "f"}:
            row = (params[0] or 1) - 1
            column = (params[1] if len(params) > 1 else 1) - 1
            self.row = min(max(row, 0), self.rows - 1)
            self.column = min(max(column, 0), self.columns - 1)
        elif final == "A":
            self.row = max(0, self.row - (params[0] or 1))
        elif final == "B":
            self.row = min(self.rows - 1, self.row + (params[0] or 1))
        elif final == "C":
            self.column = min(self.columns - 1, self.column + (params[0] or 1))
        elif final == "D":
            self.column = max(0, self.column - (params[0] or 1))
        elif final == "G":
            self.column = min(self.columns - 1, max(0, (params[0] or 1) - 1))
        elif final == "d":
            self.row = min(self.rows - 1, max(0, (params[0] or 1) - 1))
        elif final == "J":
            self._erase_display(params[0])
        elif final == "K":
            self._erase_line(params[0])
        elif final == "m":
            self._sgr(params)
        elif private and final in {"h", "l"} and 25 in params:
            self.visible = final == "h"

    def _put(self, character: str) -> None:
        self._chars[self.row][self.column] = character
        self._attrs[self.row][self.column] = self.attribute
        self.generation += 1
        self.column += 1
        if self.column >= self.columns:
            self.column = 0
            self._line_feed()

    def _line_feed(self) -> None:
        self.row += 1
        if self.row >= self.rows:
            self._chars.pop(0)
            self._chars.append([" "] * self.columns)
            self._attrs.pop(0)
            self._attrs.append([self.attribute] * self.columns)
            self.row = self.rows - 1
        self.generation += 1

    def _erase_display(self, mode: int) -> None:
        if mode == 2:
            for row in range(self.rows):
                self._erase_cells(row, 0, self.columns)
        elif mode == 1:
            for row in range(self.row):
                self._erase_cells(row, 0, self.columns)
            self._erase_cells(self.row, 0, self.column + 1)
        else:
            self._erase_cells(self.row, self.column, self.columns)
            for row in range(self.row + 1, self.rows):
                self._erase_cells(row, 0, self.columns)

    def _erase_line(self, mode: int) -> None:
        if mode == 2:
            self._erase_cells(self.row, 0, self.columns)
        elif mode == 1:
            self._erase_cells(self.row, 0, self.column + 1)
        else:
            self._erase_cells(self.row, self.column, self.columns)

    def _erase_cells(self, row: int, start: int, end: int) -> None:
        for column in range(start, end):
            self._chars[row][column] = " "
            self._attrs[row][column] = self.attribute
        self.generation += 1

    def _sgr(self, params: list[int]) -> None:
        ansi_to_dos = (0, 4, 2, 6, 1, 5, 3, 7)
        for code in params:
            if code == 0:
                self._foreground = 7
                self._background = 0
                self._bold = False
                self._blink = False
            elif code == 1:
                self._bold = True
            elif code == 5:
                self._blink = True
            elif code == 22:
                self._bold = False
            elif code == 25:
                self._blink = False
            elif 30 <= code <= 37:
                self._foreground = ansi_to_dos[code - 30]
            elif 40 <= code <= 47:
                self._background = ansi_to_dos[code - 40]
