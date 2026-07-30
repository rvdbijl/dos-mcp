# Architecture

## Layering

```text
MCP client
   │ stdio
   ▼
MCP presentation (server.py)
   │ typed logical operations + optional target selector
   ▼
BackendRuntime target registry
   ├── LinuxTerminalBackend
   └── one or more UdpBackend instances
           │ protocol v2
           ├── Python simulator
           ├── RAGENT foreground DOS endpoint
           └── RA-TSR resident DOS endpoint
                   │ minimal UDP / IPv4 / ARP
                   ▼
              packet-driver API / Ethernet
```

MCP, JSON, credential policy, retries, complete transfer state, target
selection, CP437 conversion, rendering, and user-facing errors stay on the
modern host. DOS implements only bounded target work.

Tool handlers do not contain UDP, packet-driver, or TSR logic. They resolve a
logical target and call the `Backend` protocol. Backends return typed
`MachineStatus`, `Capabilities`, `TextScreen`, `GraphicsScreen`,
`KeyReceipt`, `FileContents`, and `FileReceipt` values.

## Target registry

`BackendRuntime` owns target resources and resolves selectors:

- an injected/default backend for tests and embedding;
- one backward-compatible `DOS_MCP_TARGET`;
- named static targets from `DOS_MCP_TARGETS`;
- validated local RA-TSR discovery records;
- the Linux PTY fallback when no UDP mode is configured.

Discovery records use `name@adapter-id` as their canonical selector. A bare
name works only when unique. Omitting `target` works only when exactly one
record exists; this fail-closed rule prevents mutations reaching an arbitrary
machine after a second target appears.

Each UDP target receives its own socket, session ID, request counter,
fragment state, transfer state, and idempotent `close()`. The MCP server
serializes operations behind an async lock. This is conservative but keeps
DOS sessions and filesystem transfers from being interleaved.

## Modern backends

`LinuxTerminalBackend` is a development target, not a DOS emulator. It owns a
bounded PTY child and accurately reports Linux identity/UTF-8 semantics.
Optional file access applies a resolved-root boundary and explicit read/write
flags.

`UdpBackend` owns authentication, request IDs, retransmission, response
reassembly, CRC/MAC validation, sequential file/graphics blocks, and final
CRC32 verification. Its logical methods do not care whether the peer is the
Python simulator, RAGENT, or RA-TSR.

## Foreground DOS endpoint

`RAGENT.EXE` is preserved as the simple foreground endpoint. Its assembly
packet callback only supplies a fixed receive buffer and records completion.
The foreground loop parses packets, captures text, queues BIOS keys, and
consumes those keys as a command shell. Child commands temporarily prevent
network service, which avoids DOS reentrancy.

## Resident DOS endpoint

`RA-TSR.EXE` is built from the shared dispatcher/protocol/network code with a
resident-specific assembly hook and policies:

```text
packet IRQ/upcall
   │ bounded frame copy + ready flag only
   ▼
resident fixed receive buffer
   │
INT 1Ch timer entry / INT 28h DOS-idle entry
   │ shared nested-work guard + private stack
   │
   ├── session timeout / disconnected announcement
   ├── one paced response fragment
   └── one validated request
          ├── BDA/VRAM observation
          ├── BIOS ring insertion
          ├── gated DOS file transaction
          └── raw graphics block read
```

The old `INT 1Ch`, `INT 28h`, and `INT 2Fh` vectors are retained and chained.
`INT 2Fh` provides installed-state discovery and a local unload handshake.
Unload refuses if any vector is no longer owned by RA-TSR.

The private resident stack prevents the interrupted application's stack from
being consumed by protocol code. An active flag prevents nested workers.
Large text responses are generated from VRAM one 256-byte fragment at a time.
Files and graphics are sequential 900-byte transactions rather than resident
whole-object buffers.

Timer entries never run DOS file functions. File opcodes remain queued for a
DOS `INT 28h` idle entry, which also checks critical-error state. This is a
deliberately limited compatibility design, not a claim that arbitrary games,
protected-mode extenders, Windows, or every DOS/packet-driver combination is
reentrant.

## Authentication decision

Passphrases derive the same 128-bit base key in Python and 16-bit C.
Handshake/session derivation retains XTEA. Protocol v2 uses a native-16-bit
Speck32/64 CBC-MAC per packet because the v1 XTEA-per-block MAC was measured
to be too costly in the emulator's 8088-class resident path.

The tag remains 32 bits and traffic is not encrypted. This is an explicit
trusted-private-LAN compromise documented in the security model.

## Discovery decision

RA-TSR discovery is outside the authenticated operation protocol. It is a
TTL-1 limited broadcast containing bounded identity/capability metadata and a
CRC. The bridge treats it as an untrusted candidate endpoint and requires a
real credentialed handshake before use. Discovery never changes a tool
handler or bypasses the `UdpBackend`.

## Build and language choices

The modern bridge uses Python 3.12 and the official MCP Python SDK 2.x.
Backend core, protocol, discovery, sockets, PTY, CRC, and file logic use the
standard library.

DOS uses Open Watcom C plus register-sensitive assembly. `-0 -ms -os -s -wx`
selects 8086/8088 instructions, small model, size optimization, stack checks
off, and warnings. `RAGENT.EXE`, `RA-TSR.EXE`, `PROTOCHK.EXE`, and the
DOSBox-only `TSRHOST.EXE` are built by the checked Makefile.

## Explicitly excluded

- PicoMEM/PicoMEM2-specific code or assumptions;
- MCP/JSON/authentication policy running on DOS;
- stealth, autorun persistence, or hidden local control;
- arbitrary direct execution, memory writes, port writes, reboot, or disk
  sector tools;
- claims of physical timing/hardware behavior without measurement.
