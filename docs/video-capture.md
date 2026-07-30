# Video capture

## Modern representation

The bridge normalizes a text capture as:

- adapter and BIOS mode when applicable;
- columns, rows, and active page;
- one fixed-width string per row;
- one attribute byte per cell;
- cursor row, column, visibility, and shape;
- code page;
- blink/intensity state;
- monotonically increasing screen generation.

Rows retain trailing spaces so cell coordinates remain stable. Text and attributes are separate: characters alone are not a lossless DOS screen.

The Linux development backend uses Unicode/UTF-8 and marks itself accordingly. It validates the representation and MCP flow but is not a CP437 or BIOS-video fidelity test.

## DOS text capture

Identify mode, adapter, page, dimensions, and framebuffer address before reading. Do not assume `B800:0000`; monochrome text commonly uses `B000:0000`, and active pages change offsets.

For a typical 80×25 screen, capture 4,000 interleaved character/attribute bytes plus metadata. The bridge converts CP437 bytes to Unicode for structured text and retains original byte/attribute information in the transport/domain model where needed.

The foreground agent implements this 80×25 path. It reads BIOS Data Area
mode, columns, active-page offset, cursor position, and cursor shape; uses
BIOS display-combination function `INT 10h AX=1A00h` when available to
distinguish EGA/VGA; selects `B000h` for mode 7 and `B800h` otherwise; and
copies interleaved cells into a bounded response. DOSBox-X verifies the VGA
case. MDA/CGA behavior still needs physical or additional emulator fixtures.

## Cursor

Report position and scanline start/end separately from visibility. Validate the cursor against dimensions, but preserve off-screen or disabled hardware states as explicit metadata rather than silently moving it.

## Graphics sequence

Graphics support is deferred in this order:

1. CGA 320×200 four-color and 640×200 monochrome;
2. Hercules 720×348;
3. EGA planar modes;
4. VGA planar/packed modes and DAC palette.

The DOS agent captures raw memory plus the exact registers needed to reconstruct it. The modern bridge renders images. Any temporarily selected EGA/VGA planes or registers must be restored.

## Change and stability

Initial capture is request-driven. A later `wait_for_change` compares generations or capture checksums on the bridge. `wait_for_stable` samples until the screen remains unchanged for a bounded interval. Text matching occurs on the modern bridge after CP437 conversion.

Packet loss must not create a plausible but corrupted framebuffer. Delta capture may be added only after a complete baseline, generation linkage, and integrity check are defined.

## Fixtures

Required fixtures include:

- color and monochrome 80×25 text with cursor variants;
- every CP437 byte and representative attributes;
- alternate pages and non-80-column modes;
- blink versus high-background interpretation;
- CGA even/odd bank patterns;
- Hercules four-way interleave;
- truncated and inconsistent captures rejected by the bridge.
