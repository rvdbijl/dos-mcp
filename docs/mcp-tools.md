# MCP tools

The server exposes four transport-independent tools. Tool names and result
shapes stay stable whether the backend is Linux PTY, the UDP simulator, or
the foreground DOS agent.

Call status and capabilities first. A client should not infer DOS merely
because the server is named DOS MCP.

## `dos.get_status`

Read-only. Takes no arguments.

Example result:

```json
{
  "connected": true,
  "phase": "agent_shell_ready",
  "backend": "dos-agent",
  "transport": "packet-driver-udp",
  "identity": "192.168.10.55:21300",
  "operating_system": "DOS 5.0",
  "architecture": "8088",
  "agent_version": "0.1",
  "uptime_seconds": 12.64
}
```

Phases currently understood by the bridge are:

- `starting`
- `observe_ready`
- `agent_shell_ready`
- `child_running`
- `dos_busy`
- `awaiting_approval`
- `host_unresponsive`

The foreground agent normally reports `agent_shell_ready`. It cannot answer
while a child command is running because it deliberately remains a
foreground, non-TSR program.

## `dos.get_capabilities`

Read-only. Takes no arguments.

Example foreground-agent result:

```json
{
  "backend": "dos-agent",
  "transport": "packet-driver-udp",
  "status": true,
  "text_capture": true,
  "graphics_capture": [],
  "keyboard_injection": "bios-queue",
  "screen_columns": 80,
  "screen_rows": 25,
  "max_text_bytes": 4096,
  "max_keys_per_request": 15,
  "filesystem_read": false,
  "filesystem_write": false,
  "command_execution": false,
  "memory_read": false,
  "memory_write": false,
  "port_read": false,
  "port_write": false,
  "reboot": false
}
```

The `command_execution` field describes a future direct operation. It remains
false even though entering text and Enter in the foreground shell can run a
DOS command. Keyboard input is therefore always a meaningful mutation.

## `dos.capture_screen`

Read-only. Takes no arguments. Returns a complete text-screen snapshot.

Important fields:

| Field | Meaning |
|---|---|
| `kind` | Currently always `text` |
| `columns`, `rows` | Fixed dimensions of this snapshot |
| `text` | Exactly one fixed-width Unicode string per row |
| `attributes` | One DOS/terminal attribute byte per cell |
| `cursor` | Position, visibility, and optional scanline shape |
| `generation` | Backend-local monotonically advancing snapshot value |
| `adapter` | `MDA`, `CGA`, `EGA`, `VGA`, or `linux-pty` |
| `video_mode` | BIOS mode for DOS, otherwise null |
| `active_page` | DOS video page |
| `code_page` | `CP437` for the DOS UDP backend |
| `blink_enabled` | Current representation flag |

Rows retain trailing spaces. A client should use the dimensions rather than
trimming or wrapping rows before coordinate-based interpretation.

The foreground screen payload is about 4 KB and is fragmented across
multiple credentialed UDP datagrams (or open-mode datagrams when explicitly
configured). The bridge exposes a snapshot only
after every fragment has passed identity, CRC, and MAC validation.

## `dos.send_keys`

Mutating and non-idempotent at the MCP level. Arguments:

| Argument | Type | Range | Meaning |
|---|---|---|---|
| `text` | string | at most 4096 encoded bytes | Text sent before named keys |
| `keys` | list of strings | at most 128 entries | Named non-text keys |
| `inter_key_delay_ms` | integer | 0–1000 | Delay between input items |
| `settle_ms` | integer | 0–2000 | Bridge wait/collection time after acceptance |

Supported named keys:

```text
ENTER ESC TAB BACKSPACE
UP DOWN RIGHT LEFT HOME END DELETE PAGE_UP PAGE_DOWN
F1 F2 F3 F4 F5 F6 F7 F8 F9 F10 F11 F12
CTRL_C CTRL_D
```

Names are case-insensitive.

Example call arguments:

```json
{
  "text": "DIR",
  "keys": ["ENTER"],
  "inter_key_delay_ms": 5,
  "settle_ms": 500
}
```

Example result:

```json
{
  "accepted_text_bytes": 3,
  "accepted_keys": 1,
  "keys": ["ENTER"],
  "screen_generation": 7
}
```

Acceptance means the backend accepted the input, not that the application
produced the expected effect. Capture the screen afterward and verify it.

The UDP protocol gives each keyboard request a request ID and caches the last
completed response. If a response is lost, the bridge retries the same
request and the target returns the cached receipt instead of injecting the
keys again.

## Recommended interaction pattern

For a command-oriented foreground session:

1. capture the current screen;
2. confirm the prompt and target identity;
3. send a short text sequence plus `ENTER`;
4. wait an appropriate settle interval;
5. capture the screen again;
6. verify the output before issuing another mutation.

Avoid sending speculative input when the phase or screen is unexpected.
