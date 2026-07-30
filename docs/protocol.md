# Agent protocol version 1

## Status and responsibilities

Version 1 is implemented by the Python bridge/simulator and the 16-bit DOS
foreground agent. It is independent of MCP and JSON. The modern bridge owns
timeouts, retransmission, response reassembly, MCP conversion, and user-facing
policy. The DOS endpoint uses fixed buffers and retains one completed response
so a retried mutation is not repeated.

Implemented operations are `HELLO`, `GET_STATUS`, `GET_CAPABILITIES`,
`CAPTURE_TEXT_SCREEN`, `SEND_KEYS`, and `PING`. `CANCEL` is reserved but not
yet acted on.

## Datagram envelope

All integers are little-endian. The fixed header is 20 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | magic, ASCII `DM` |
| 2 | 1 | protocol version, `1` |
| 3 | 1 | kind: request `1`, response `2`, error `3` |
| 4 | 1 | opcode |
| 5 | 1 | flags, currently zero |
| 6 | 2 | session ID |
| 8 | 2 | request ID |
| 10 | 1 | zero-based fragment index |
| 11 | 1 | fragment count, 1–32 |
| 12 | 2 | payload length, 0–1,024 |
| 14 | 2 | CRC-16/CCITT-FALSE |
| 16 | 4 | truncated packet authentication tag |
| 20 | N | payload |

The CRC covers bytes 0–13 followed by the payload. The tag covers bytes 0–15
followed by the payload. Exact datagram length must equal `20 + payload
length`. A logical message is at most 32,768 bytes.

The deterministic packet vector for key
`000102030405060708090a0b0c0d0e0f` is:

```text
444d010102003412785600010600e059ba981128737461747573
```

Both Python and `PROTOCHK.EXE` verify this vector. The CRC check vector for
ASCII `123456789` is `29B1`; the XTEA block vector is
`93009913e1c4f785`.

## Authentication and sessions

The configured key is exactly 16 nonzero bytes. `HELLO` carries a 32-bit
client nonce and is authenticated by the configured key. Its response echoes
that nonce and carries a 32-bit server nonce, nonzero 16-bit session ID, and
maximum fragment payload. Both peers derive a 16-byte session key by applying
XTEA to two nonce-derived blocks.

Ordinary datagrams use a length-strengthened XTEA CBC-MAC, folded to a 32-bit
tag. Tags are compared in constant time on the modern bridge. This provides
authentication and integrity, not confidentiality. The deliberately short
tag is an 8088/trusted-LAN tradeoff: use a unique random key per machine, bind
only a private network, and never forward the UDP port from the Internet.

Requests use monotonically advancing 16-bit IDs with half-range wrap
comparison. The DOS agent binds a session to source MAC, IP, and UDP port.
It rejects old IDs. A duplicate of the last completed request resends the
cached encoded response, which gives `SEND_KEYS` at-most-once behavior within
the session.

## Fragmentation and reliability

The bridge sends every request fragment, waits a bounded interval, and
retransmits the same encoded request on timeout. Responses may arrive in any
order. Every response fragment must match kind, opcode, session, request ID,
and fragment count before reassembly. CRC and authentication are checked per
fragment.

The foreground DOS agent has one 1,518-byte Ethernet receive buffer, one
4,229-byte request buffer, and five cached response datagrams. It accepts
only full-sized non-final fragments. A complete 80×25 screen response is
4,013 payload bytes and crosses four UDP fragments.

There is no selective-fragment NACK in version 1. Loss is recovered by
retransmitting the complete request, after which the agent resends its cached
complete response.

## Operation payloads

`HELLO` request is `<client_nonce:u32>`. Response is
`<client_nonce:u32, server_nonce:u32, session_id:u16,
max_fragment_payload:u16>`.

`GET_STATUS` has an empty request and returns:

```text
agent major:u8, agent minor:u8
DOS major:u8, DOS minor:u8
CPU class:u8, phase:u8
conventional memory KiB:u16
agent-running BIOS ticks:u32
```

`GET_CAPABILITIES` has an empty request and returns:

```text
capability flags:u32
columns:u8, rows:u8, adapter:u8
max fragment payload:u16
maximum BIOS queue words:u16
```

`CAPTURE_TEXT_SCREEN` has an empty request and returns a 13-byte prefix:

```text
columns:u8, rows:u8, BIOS mode:u8, active page:u8
cursor row:u8, cursor column:u8
cursor start scanline:u8, cursor end scanline:u8
adapter:u8, code page:u16, generation:u16
```

The prefix is followed by exactly `columns * rows * 2` interleaved
character/attribute bytes.

`SEND_KEYS` carries:

```text
text byte count:u16
named key count:u8
inter-key delay milliseconds:u16
CP437 text bytes
named key codes
```

The response is `<accepted_text_bytes:u16, accepted_named_keys:u8,
generation:u16>`. Acceptance means insertion into and consumption from the
foreground agent's BIOS queue; it does not promise compatibility with
software that directly reads keyboard-controller ports.

`PING` echoes its bounded payload.

Errors carry a stable `u16` error code followed by at most 120 diagnostic
bytes. Diagnostic text is untrusted.

## Validation and tests

Python tests cover truncation, bounds, invalid enums, CRC/MAC corruption,
fragment identity and ordering, request retries, duplicate mutation
suppression, and wrong keys. The same crypto and packet vectors compile
natively as C89 and run in a 16-bit DOS executable. The optional DOSBox-X
harness validates the real packet-driver path.

Still required before claiming hostile-network suitability are parser
fuzzing, measured 8088 MAC cost, key rotation/provisioning, and a larger
replay window. Version 1 is for a trusted private LAN only.
