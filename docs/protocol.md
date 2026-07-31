# Agent protocol version 2

## Scope and responsibilities

Protocol version 2 is implemented by the Python bridge/simulator, foreground
`RAGENT.EXE`, and resident `RA-TSR.EXE`. It is a bounded binary
request/response protocol independent of MCP and JSON. The modern bridge owns
timeouts, retries, complete-response reassembly, policy, rendering, target
selection, and MCP conversion.

Operations:

| Value | Name | Availability |
|---:|---|---|
| 1 | `HELLO` | all UDP targets |
| 2 | `GET_STATUS` | all |
| 3 | `GET_CAPABILITIES` | all |
| 4 | `CAPTURE_TEXT_SCREEN` | all |
| 5 | `SEND_KEYS` | all |
| 6 | `PING` | protocol/testing |
| 7 | `CANCEL` | reserved |
| 8–10 | `FILE_READ_BEGIN/BLOCK/END` | RA-TSR and opted-in simulator |
| 11–13 | `FILE_WRITE_BEGIN/BLOCK/COMMIT` | RA-TSR and opted-in simulator |
| 14 | `FILE_ABORT` | active file transfer |
| 15–17 | `GRAPHICS_BEGIN/BLOCK/END` | RA-TSR and capable simulator |
| 18 | `GET_DIAGNOSTICS` | RA-TSR commissioning |

## Datagram envelope

All integers are little-endian. The fixed header is 20 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | magic, ASCII `DM` |
| 2 | 1 | protocol version, `2` |
| 3 | 1 | kind: request `1`, response `2`, error `3` |
| 4 | 1 | opcode |
| 5 | 1 | flags, currently zero |
| 6 | 2 | session ID |
| 8 | 2 | request ID |
| 10 | 1 | zero-based fragment index |
| 11 | 1 | fragment count, 1–32 |
| 12 | 2 | payload length, 0–1,024 |
| 14 | 2 | CRC-16/CCITT-FALSE |
| 16 | 4 | packet authentication tag |
| 20 | N | payload |

The CRC covers bytes 0–13 followed by the payload. The tag covers bytes 0–15
followed by the payload. A logical message is at most 32,768 bytes.

Protocol v2 permits exactly one trailing zero transport byte after the
declared packet. It is excluded from CRC and authentication. Modern senders
append it to odd-sized DOS-bound packets because some 16-bit NE2000 drivers
perform word-wide DMA. Receivers reject nonzero or multiple trailing bytes.

The packet vector for key
`000102030405060708090a0b0c0d0e0f`, request kind, `GET_STATUS`, session
`1234h`, request `5678h`, and payload `status` is:

```text
444d0201020034127856000106009259b7707354737461747573
```

Python and `PROTOCHK.EXE` verify this vector. The CRC vector for ASCII
`123456789` is `29B1`; the retained XTEA block vector is
`93009913e1c4f785`.

## Credentials and key derivation

Credential selection is configuration, not an unauthenticated wire field.
Every mode produces a 16-byte base key:

- `key:32hex` or a legacy bare 32-hex value supplies the bytes directly;
- `pass:text` or any other nonempty bare value derives a key;
- omitted credential or `-` selects the public open-mode key.

Password derivation:

```text
SHA-256("DOS-MCP credential v1" || NUL || UTF-8(password))[0:16]
```

Open-mode key:

```text
SHA-256("DOS-MCP open mode v1" || NUL)[0:16]
```

The DOS derivation accepts any nonempty command-tail passphrase length that
fits DOS's command-line limit. It is intentionally fast enough for an 8088,
not a slow password hash. Use a generated high-entropy passphrase.

## Session establishment and authentication

`HELLO` is authenticated with the base key and contains a 32-bit client
nonce. Its response echoes the nonce and includes a 32-bit server nonce,
nonzero 16-bit session ID, and target maximum fragment payload. The peers
derive 16 session-key bytes by XTEA-encrypting two nonce-derived blocks.
XTEA runs only during handshake/key derivation, not for every packet block.

Packet tags use a 32-bit, length-strengthened Speck32/64 CBC-MAC:

1. append `80h`;
2. zero-pad to a four-byte boundary;
3. append the original 32-bit byte length;
4. CBC-chain Speck32/64 with key bytes 0–7;
5. encrypt the final chain once with key bytes 8–15;
6. transmit the resulting 32-bit block.

The DOS implementation expands each 22-round 16-bit Speck schedule once per
packet. This replaced v1's 32-bit XTEA-per-block MAC, whose measured emulator
cost was unsuitable for timer-driven 8088 operation.

With a secret credential, the protocol authenticates and integrity-checks
traffic but does not encrypt it. The 32-bit tag is a trusted-LAN/8088
tradeoff, not an Internet-grade construction. Open mode uses a public key and
therefore provides no peer authentication.

The DOS target binds a session to source MAC, IPv4 address, and UDP port.
Ordinary requests use advancing 16-bit request IDs with half-range wrap
comparison. Old IDs are rejected. A duplicate of the last completed request
resends its result rather than repeating a keyboard or file mutation.
RA-TSR expires an idle session after approximately 30 seconds.

## Fragmentation and reliability

The bridge retransmits an identical request after a bounded timeout.
Responses may arrive out of order and must agree on kind, opcode, session,
request ID, fragment count, CRC, and tag before reassembly.

Foreground RAGENT can cache a complete small multi-fragment response.
RA-TSR holds bounded logical response metadata and emits at most one
256-byte response fragment per resident worker entry. Text VRAM is read
directly into each fragment, which avoids a monolithic 4 KiB interrupt-time
copy. File and graphics data use sequential block operations so the TSR never
holds an entire large object.

## Common payloads

### `HELLO`

Request:

```text
client_nonce:u32
```

Response:

```text
client_nonce:u32
server_nonce:u32
session_id:u16
max_fragment_payload:u16
```

### `GET_STATUS`

Empty request. Response:

```text
agent_major:u8, agent_minor:u8
DOS_major:u8, DOS_minor:u8
CPU_class:u8, phase:u8
conventional_memory_KiB:u16
agent_running_BIOS_ticks:u32
```

RA-TSR reports phase `observe_ready`; the foreground shell reports
`agent_shell_ready`.

### `GET_CAPABILITIES`

Empty request. Response:

```text
capability_flags:u32
columns:u8, rows:u8, adapter:u8
max_fragment_payload:u16
maximum_BIOS_queue_words:u16
```

Capability bits currently include status, text, keyboard, filesystem read,
filesystem write, and graphics. Direct execution, raw memory, ports, and
reboot remain false.

### `CAPTURE_TEXT_SCREEN`

Empty request. Response prefix:

```text
columns:u8, rows:u8, BIOS_mode:u8, active_page:u8
cursor_row:u8, cursor_column:u8
cursor_start:u8, cursor_end:u8
adapter:u8, code_page:u16, generation:u16
```

Exactly `columns * rows * 2` interleaved character/attribute bytes follow.

### `SEND_KEYS`

Request:

```text
text_byte_count:u16
named_key_count:u8
inter_key_delay_ms:u16
CP437_text_bytes
named_key_codes
```

Response:

```text
accepted_text_bytes:u16
accepted_named_keys:u8
generation:u16
```

RA-TSR inserts BIOS words but does not call DOS or consume them itself.
Software must use BIOS/DOS keyboard input for compatibility.

## File-transfer payloads

File paths are 1–80 bytes and are interpreted by the target's local path
policy. A normal RA-TSR root requires relative paths. The explicit `ALL` root
requires absolute drive-qualified paths. This policy is selected locally at
TSR installation and is not controllable through the wire protocol.

### Download

`FILE_READ_BEGIN` request:

```text
path_length:u8, path_bytes
```

Response:

```text
transfer_id:u16, total_size:u32
```

`FILE_READ_BLOCK` request:

```text
transfer_id:u16, expected_offset:u32, requested_length:u16
```

Length is 1–900. Response:

```text
transfer_id:u16, offset:u32, data_length:u16
running_crc32:u32, data
```

`FILE_READ_END` request is `transfer_id:u16`. Response is:

```text
total_size:u32, final_crc32:u32
```

### Upload

`FILE_WRITE_BEGIN` request:

```text
overwrite:u8
declared_size:u32
declared_crc32:u32
path_length:u8
path_bytes
```

Response is `transfer_id:u16, declared_size:u32`.

`FILE_WRITE_BLOCK` request:

```text
transfer_id:u16, expected_offset:u32, data_length:u16, data
```

The response uses the common block-response prefix and reports the running
CRC. `FILE_WRITE_COMMIT` carries `transfer_id:u16` and succeeds only when
declared size and CRC match. Its response contains final size and CRC.

`FILE_ABORT` carries the active transfer ID and discards temporary upload
state. Transfer offsets are strictly sequential; overlaps, gaps, unknown
IDs, oversized blocks, and extra data are rejected.

The modern backend and RA-TSR each impose a 1 MiB file limit even if a DOS
filesystem could hold more.

## Graphics-transfer payloads

`GRAPHICS_BEGIN` has an empty request. Response:

```text
transfer_id:u16
adapter:u8, BIOS_mode:u8, layout:u8, planes:u8
width:u16, height:u16
total_size:u32, bytes_per_plane:u32
```

`GRAPHICS_BLOCK` uses the same sequential block request/response as file
download. `GRAPHICS_END` uses the same final size/CRC response.

Layouts:

| Value | Layout |
|---:|---|
| 1 | CGA 2-bpp even/odd scanline interleave |
| 2 | CGA 1-bpp even/odd scanline interleave |
| 3 | Hercules 1-bpp four-way interleave |
| 4 | four concatenated EGA/VGA planes |
| 5 | one planar monochrome plane |
| 6 | packed 8-bpp bytes |

The wire carries raw framebuffer bytes, not a PNG or rendered image.

## Resident diagnostics

`GET_DIAGNOSTICS` has an empty request and is implemented only by RA-TSR.
Its fixed version-1 response is:

```text
diagnostics_version:u8 (=1), state_flags:u8
int08_entries:u16, int1c_entries:u16, int28_entries:u16
worker_runs:u16, fallback_runs:u16, busy_skips:u16
receive_allocations:u16, receive_completions:u16, receive_drops:u16
send_attempts:u16, send_failures:u16
last_receive_BIOS_tick_low:u16, last_worker_BIOS_tick_low:u16
worker_ticks:u16, receive_length:u16
master_PIC_mask:u8, slave_PIC_mask:u8
last_protocol_result:i16, last_send_result:i16
last_opcode:u8, reserved:u8 (=0)
```

State bits `0..3` report current ownership of `INT 08h`, `INT 1Ch`, `INT 28h`,
and `INT 2Fh`. Bits `4..7` report resident enabled, receive-buffer ready,
session active, and response pending. Counters and tick lows wrap at 16 bits;
consumers compare modular deltas rather than treating them as lifetime totals.
PIC masks are diagnostic snapshots and do not identify the NIC hardware IRQ.

## Errors

Error payload:

```text
error_code:u16, diagnostic_bytes[0..120]
```

Stable codes distinguish malformed input, unsupported versions/operations,
authentication/replay failures, invalid arguments, policy denial, busy state,
timeouts, cancellation, integrity failure, and internal failure. Diagnostic
text is untrusted.

## Local discovery

Disconnected RA-TSR discovery is a separate unauthenticated datagram and is
never accepted as an operation packet. Its exact format and threat boundary
are in [Local discovery](discovery.md).

## Verification status

Tests cover packet bounds, padding, corruption, authentication, fragment
identity, retry behavior, duplicate mutation suppression, file size/offset/
CRC checks, graphics metadata, discovery validation, wrong keys, and backend
shutdown. `PROTOCHK.EXE` verifies the C/Python crypto and packet vectors in
16-bit DOS. DOSBox-X verifies the packet-driver path, resident load/unload,
text capture, BIOS keys, exact binary file round-trip, and exact 64 KiB VGA
mode 13h framebuffer capture.

Still required for hostile networks or broad hardware claims: fuzzing,
independent C review, physical 4.77 MHz measurements, representative packet
drivers, credential rotation, rate limiting, and a wider authentication tag
or secure outer tunnel.
