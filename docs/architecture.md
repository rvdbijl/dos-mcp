# Architecture

## Decision summary

The project is split into four layers:

```text
MCP host
   │ stdio
   ▼
MCP presentation
   │ logical Backend calls
   ▼
target backend
   │ transport-specific operations
   ▼
Linux PTY or authenticated UDP
   │
   ▼
Python simulator or 16-bit DOS foreground agent
```

MCP handlers translate validated tool arguments into transport-independent domain operations. A backend reports its own capabilities and owns its lifecycle. The initial `LinuxTerminalBackend` is a development target: it runs a shell in a bounded pseudo-terminal and makes the result look like the text-console subset expected from DOS.

The Linux backend is not a DOS emulator and identifies itself accurately. It
validates MCP behavior and remains the default zero-configuration development
target. `UdpBackend` implements bounded retries, session establishment,
fragment reassembly, duplicate-safe mutations, and CP437 conversion without
changing MCP tool semantics.

The foreground DOS endpoint is built with Open Watcom C and one register-
sensitive assembly upcall. Its layers are:

```text
RAGENT foreground shell and operation dispatcher
   │
version-1 authenticated request/response protocol
   │
minimal UDP / IPv4 / ARP
   │
FTP/Crynwr packet-driver API
   │
Ethernet adapter
```

The packet callback only returns a preallocated buffer or records its length.
All parsing, authentication, screen copying, BIOS queue work, and shell work
occur later in the foreground loop.

## Initial tool surface

| MCP tool | Backend operation | Mutation |
|---|---|---|
| `dos.get_status` | `get_status()` | No |
| `dos.get_capabilities` | `get_capabilities()` | No |
| `dos.capture_screen` | `capture_screen()` | No |
| `dos.send_keys` | `send_keys()` | Yes: terminal input |

No filesystem, command-execution, memory-write, port-write, reboot, or image-management tool is initially registered.

## Backend contract

Backends return typed domain values:

- `MachineStatus`: backend identity, connection phase, platform facts, and uptime.
- `Capabilities`: explicit operation and display support.
- `TextScreen`: dimensions, rows, cursor, attributes, code page, and generation.
- `KeyReceipt`: accepted byte/key counts and resulting screen generation.

The contract is synchronous because both local PTY operations and UDP
exchanges are deadline-bounded. MCP serializes backend operations behind an
async lock so the synchronous transport state cannot be raced.

## Process and resource lifecycle

The Linux PTY starts lazily on the first backend operation. It uses a fixed terminal size, nonblocking reads, a restricted working directory, and an explicit environment. `close()` is idempotent and terminates only the child process created by the backend.

The MCP module holds a lazy runtime so importing the server for tests or inspection does not start a shell.

## Language and toolchain decision

The modern bridge uses Python 3.12 and the official MCP Python SDK 2.x.

Python is the first-phase choice because it provides:

- the shortest path to a correct MCP server and in-process protocol tests;
- mature standard-library PTY, sockets, binary packing, and image options;
- fast iteration on an unsettled DOS wire protocol;
- straightforward fixtures and fault injection.

The tradeoff is a larger runtime and weaker single-binary distribution than Rust or Go. Those costs live entirely on the modern host. Revisit Rust only after the logical API and UDP protocol stabilize; do not rewrite merely for prospective performance.

The DOS client uses Open Watcom C plus a small 8086 assembly receive upcall.
It builds with `-0 -ms -os -s -wx`. `PROTOCHK.EXE` has run inside DOSBox-X,
and `RAGENT.EXE` has completed the end-to-end NE2000 test. The current
executable is approximately 27 KB on disk; real 4.77 MHz timing and resident
memory measurements remain hardware-verification work.

## Implemented selection

`BackendRuntime` selects `LinuxTerminalBackend` by default. Setting
`DOS_MCP_TARGET` and `DOS_MCP_KEY` selects `UdpBackend`, which can talk to
either `dos-mcp-simulator` or `RAGENT.EXE`.

## Deferred architecture

A TSR, graphics capture, direct filesystem/execution operations, and
policy-controlled memory/port diagnostics are later generic milestones.
PicoMEM integration remains outside the implementation scope.
