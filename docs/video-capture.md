# Video capture

## Text representation

The bridge returns:

- adapter, BIOS mode, active page, dimensions, and code page;
- one fixed-width Unicode row per screen row;
- one attribute byte per cell;
- cursor position, visibility/shape, and generation.

Rows retain trailing spaces. Text and attributes remain separate because
characters alone are not a lossless DOS screen.

RAGENT copies the active text page in foreground context. RA-TSR snapshots BDA
metadata and reads VRAM into paced 256-byte response fragments. It uses the
install-time adapter classification, avoiding a video-BIOS call from the
timer worker. Mode 7 maps to `B000h`; other standard text modes map to
`B800h` plus the BDA active-page offset.

The current DOS representation is bounded to at most 80×25. Cursor and active
page values are validated by the Python message/domain model before MCP
results are returned. Programs such as CheckIt may hide the hardware cursor by
placing its BDA position just below the screen. The DOS endpoint normalizes
that convention to an invisible in-bounds cursor at `(0,0)` before sending it.

## Raw graphics result

`dos.capture_graphics` returns metadata and strict base64 raw bytes:

```text
adapter, BIOS mode, layout
width, height, plane count
bytes per plane, total size, CRC32
raw framebuffer bytes
```

The bridge does not pretend these bytes are a rendered image. Palette/DAC
capture and PNG rendering remain modern-host follow-up work.

Only RA-TSR advertises the generic DOS graphics capability. Capture begins
only in a recognized standard graphics mode and uses an explicit sequential
block transfer with final size/CRC32 verification.

## Supported standard modes

| BIOS mode | Adapter family | Dimensions | Layout | Raw bytes |
|---:|---|---:|---|---:|
| `04h`, `05h` | CGA | 320×200 | 2-bpp even/odd scanline banks | 16,384 |
| `06h` | CGA | 640×200 | 1-bpp even/odd scanline banks | 16,384 |
| `07h` with Hercules graphics enabled | Hercules | 720×348 | 1-bpp four-way interleave | 32,768 |
| `0Dh` | EGA/VGA | 320×200 | four concatenated planes | 32,000 |
| `0Eh` | EGA/VGA | 640×200 | four concatenated planes | 64,000 |
| `0Fh` | EGA/VGA | 640×350 | one planar monochrome plane | 28,000 |
| `10h` | EGA/VGA | 640×350 | four concatenated planes | 112,000 |
| `11h` | VGA | 640×480 | one planar monochrome plane | 38,400 |
| `12h` | VGA | 640×480 | four concatenated planes | 153,600 |
| `13h` | VGA | 320×200 | packed 8-bpp | 64,000 |

These are raw aperture sizes/layouts, not necessarily the minimum number of
displayed pixel bytes. CGA/Hercules include bank padding. For planar modes,
all bytes of plane 0 precede plane 1 and so on.

## CGA

Modes 4/5 and 6 use the standard 16 KiB `B800h` aperture. Scanlines alternate
between the two 8 KiB banks. Consumers reconstruct rows using the layout
identifier rather than assuming linear pixels. Overscan, composite-artifact
color, and palette-register interpretation are not captured.

## Hercules

BIOS mode 7 is also monochrome text, so RA-TSR additionally checks the
Hercules control port graphics-enable bit before accepting it as graphics.
The 32 KiB `B000h` aperture uses four scanline banks. Hercules clones with
different control semantics require hardware-specific verification.

## EGA and planar VGA

The graphics controller's read-map select chooses each plane. For every
bounded block RA-TSR:

1. saves the selected graphics-controller index;
2. selects read-map register 4;
3. saves the current plane value;
4. selects the required plane for each logical offset;
5. reads from `A000h`;
6. restores plane value and original index.

The output is plane-major. Modes marked planar monochrome expose one plane.
This preserves the raw representation selected by the current implementation;
it does not yet include palette registers, attribute-controller state, CRTC
timing, latches, or nonstandard memory maps.

## VGA mode 13h

Mode 13h reads the linear 64,000-byte `A000h` aperture. The DOSBox-X resident
integration fills all bytes with a deterministic pattern and verifies exact
metadata and byte-for-byte recovery through the authenticated multi-block
protocol. This is the strongest current graphics integration fixture.

## Stability and consistency

Graphics capture is not atomic relative to the running application. A program
may draw while blocks are read, yielding a temporally torn but
integrity-correct frame. CRC32 detects transport corruption, not concurrent
video changes.

Future stable capture can sample generation/checksum state or capture twice
on the modern host. It must remain bounded and cannot freeze arbitrary games
from a timer callback.

## Unsupported modes

Capture rejects:

- text modes through the graphics tool;
- unrecognized BIOS modes;
- VESA/SVGA banked or linear-framebuffer modes;
- Mode X and other register-programmed variants of mode 13h;
- undocumented game/demo layouts;
- Windows/protected-mode ownership assumptions.

Unsupported mode errors are preferable to returning plausible but wrongly
interpreted bytes.

## Remaining fixtures

Physical or emulator fixtures are still needed for:

- CGA mode 4/5 palette variants and even/odd bank patterns;
- CGA mode 6;
- Hercules four-bank patterns;
- every EGA/VGA plane with distinct data;
- mode changes during a transfer;
- register restoration checks;
- MDA/color adapter detection on early BIOSes.
