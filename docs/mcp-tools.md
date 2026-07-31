# MCP tools

The tool surface is transport-independent. Every target operation accepts an
optional `target` selector. Omit it only when `dos.list_targets` reports one
record; with multiple systems the server rejects an omitted selector.

## `dos.list_targets`

Read-only, no arguments. Returns configured/discovered records:

```json
{
  "targets": [
    {
      "selector": "WORKBENCH-386@acde48444d02",
      "name": "WORKBENCH-386",
      "source": "discovery",
      "host": "192.168.10.38",
      "port": 21300,
      "agent_id": "acde48444d02",
      "protocol_version": 2,
      "open_mode": false,
      "connected": false
    }
  ]
}
```

Discovery metadata is advisory. A target is not authenticated until an
operation performs `HELLO`.

## `dos.get_status`

Read-only. Argument: optional `target`.

Reports connection, phase, backend/transport, identity, OS, architecture,
agent version, and uptime. RA-TSR normally reports `observe_ready`; foreground
RAGENT reports `agent_shell_ready`.

## `dos.get_capabilities`

Read-only. Argument: optional `target`.

Reports only operations enabled by both implementation and local policy.
Important fields:

- `text_capture`;
- `graphics_capture` adapter families;
- `keyboard_injection`;
- `filesystem_read` and `filesystem_write`;
- explicit false values for direct execution, memory/port writes, and reboot.

Always query capabilities rather than inferring them from the target name.

## `dos.capture_screen`

Read-only. Argument: optional `target`.

Returns a complete fixed-width text screen:

- `columns`, `rows`, `text`, and per-cell `attributes`;
- cursor position/shape;
- generation, adapter, video mode, page, code page, and blink flag.

Rows retain trailing spaces. The bridge returns the result only after every
fragment passes packet identity, CRC, authentication, and dimensional
validation.

## `dos.capture_graphics`

Read-only. Argument: optional `target`.

Returns raw framebuffer metadata and strict base64 bytes:

```json
{
  "kind": "graphics",
  "adapter": "VGA",
  "video_mode": 19,
  "layout": "packed-8bpp",
  "width": 320,
  "height": 200,
  "planes": 1,
  "bytes_per_plane": 64000,
  "size": 64000,
  "crc32": "1234abcd",
  "data_base64": "..."
}
```

The tool rejects text, unknown, VESA/SVGA, Mode X, and undocumented layouts.
See [Video capture](video-capture.md).

## `dos.send_keys`

Mutating, non-idempotent at the MCP layer:

| Argument | Range |
|---|---|
| `target` | optional selector |
| `text` | at most 4,096 encoded bytes |
| `keys` | at most 128 named keys |
| `inter_key_delay_ms` | 0–1,000 |
| `settle_ms` | 0–2,000 |

Named keys:

```text
ENTER ESC TAB BACKSPACE
UP DOWN RIGHT LEFT HOME END DELETE PAGE_UP PAGE_DOWN
F1 F2 F3 F4 F5 F6 F7 F8 F9 F10 F11 F12
CTRL_C CTRL_D
```

Result reports accepted text bytes, named keys, and generation. Acceptance is
queue insertion, not proof of application behavior. Capture before and after.

The UDP layer retries an identical request ID and the target reuses its
completed receipt instead of repeating a mutation.

## `dos.download_file`

Read-only from the target, but disabled by default.

Arguments:

- `target`: optional selector;
- `path`: 1–80 byte path relative to a normal target file root, or an absolute
  drive path when that target was explicitly loaded with root `ALL`.

Result contains path, size, CRC32, and strict base64 content. Maximum modern
backend size is 1 MiB. RA-TSR also requires load-time `R` or `RW`.

## `dos.upload_file`

Mutating and marked destructive.

Arguments:

- `target`: optional selector;
- `path`: bounded target path: relative for a normal root, absolute and
  drive-qualified for root `ALL`;
- `content_base64`: strict RFC 4648 content, decoded size ≤1 MiB;
- `overwrite`: explicit permission to replace an existing file.

The bridge and RA-TSR must both enable writes. RA-TSR writes a temporary,
verifies declared size/CRC, and renames on commit. Result reports path, size,
and CRC32.

## Recommended operating sequence

1. call `dos.list_targets`;
2. choose and retain the exact selector;
3. call status and capabilities;
4. capture the relevant text/graphics state;
5. perform the smallest intended mutation;
6. recapture and verify;
7. stop on unexpected phase, target, screen, receipt, or checksum.
