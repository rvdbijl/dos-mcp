# DOS MCP

DOS MCP lets a modern MCP client observe and control DOS systems without
putting MCP, JSON, HTTP, or an AI runtime on the retro machine. The modern
Python bridge owns MCP, credentials, retries, target routing, policy, and
structured results. DOS runs a small packet-driver endpoint.

Two DOS executables are retained:

- `RAGENT.EXE`: the original foreground command-shell endpoint;
- `RA-TSR.EXE`: a loadable/unloadable resident endpoint for background
  observation, BIOS keyboard insertion, sandboxed file transfer, and raw
  standard graphics capture.

PicoMEM/PicoMEM2-specific code is intentionally absent.

## Implemented MCP tools

- `dos.list_targets`
- `dos.get_status`
- `dos.get_capabilities`
- `dos.capture_screen`
- `dos.capture_graphics`
- `dos.send_keys`
- `dos.download_file`
- `dos.upload_file`

All target-taking tools accept an optional `target` selector. It may be
omitted only when the bridge knows exactly one system.

## Target matrix

| Capability | Linux PTY | UDP simulator | RAGENT | RA-TSR |
|---|:---:|:---:|:---:|:---:|
| Status/capabilities | Yes | Yes | Yes | Yes |
| 80×25 text capture | Yes | Yes | Yes | Yes |
| Keyboard input | terminal | UDP | BIOS queue | BIOS queue |
| Sandboxed file read/write | opt-in | opt-in | No | opt-in |
| Raw graphics capture | No | fixture-dependent | No | CGA/Herc/EGA/VGA |
| Background operation | host process | host process | No | Yes |
| Load/unload | process | process | process | DOS TSR |
| Named local discovery | No | No | No | Yes |

RA-TSR is built with 8086 instruction generation for 8088 through 486-class
machines. DOSBox-X verifies the complete resident path, including exact
text/VGA capture, keyboard-driven `VER`, binary upload/download, and unload.
Physical adapter, BIOS, video-card, and 4.77 MHz timing coverage remains an
explicit hardware-verification item.

## Architecture

```text
MCP client
    │ MCP over stdio
    ▼
Python DOS MCP bridge
    │ target registry + transport-independent Backend operations
    ├── Linux PTY backend
    ├── configured UDP target(s)
    └── validated local discovery records
             │ authenticated protocol v2 over UDP
             ├── Linux simulator
             ├── RAGENT.EXE
             └── RA-TSR.EXE
                    │ FTP/Crynwr packet driver
                    └── Ethernet adapter
```

Discovery is only an unauthenticated address hint. Every target operation
still performs the configured credentialed handshake. RA-TSR announcements
use Ethernet/IP limited broadcast and TTL 1, and stop while connected.

## Quick start: local Linux backend

Requirements are Linux, Python 3.12+, and
[`uv`](https://docs.astral.sh/uv/):

```bash
uv sync
uv run dos-mcp
```

Select a shell and starting directory:

```bash
DOS_MCP_ROOT=/path/to/workspace \
DOS_MCP_SHELL=/bin/bash \
uv run dos-mcp
```

The starting directory is not an OS sandbox; the child retains the bridge
user's permissions.

## Quick start: Linux-backed UDP simulator

```bash
# terminal 1
uv run dos-mcp-simulator \
  --bind 127.0.0.1:21300 \
  --password 'local-test-only' \
  --root "$PWD" \
  --allow-file-read \
  --allow-file-write

# terminal 2
DOS_MCP_TARGET=127.0.0.1:21300 \
DOS_MCP_PASSWORD='local-test-only' \
DOS_MCP_ALLOW_FILE_READ=1 \
DOS_MCP_ALLOW_FILE_WRITE=1 \
uv run dos-mcp
```

## Quick start: DOS

For a hardware PC, copy the ready-to-commission [`bin/`](bin/) directory to
`C:\DOSMCP`. It contains both endpoints, offline protocol/configuration tests,
an editable mTCP-style configuration, and load/unload batch files. Edit
`C:\DOSMCP\MTCP.CFG`, load the adapter packet driver, then run:

```dos
CD \DOSMCP
PROTOCHK
CFGCHK
STARTTSR Unique-Lab-Passphrase
```

The example IP addresses in `bin/MTCP.CFG` are documentation-only TEST-NET
addresses and must be replaced. `STARTTSR` sets `MTCPCFG` and uses its
`IPADDR`, `PACKETINT`, and `HOSTNAME` values.

Build with Open Watcom 2:

```bash
make -C dos WATCOM=/path/to/watcom all
```

Regenerate the tracked commissioning bundle after a DOS source change:

```bash
make -C dos WATCOM=/path/to/watcom bin
```

Foreground:

```dos
RAGENT pass:UniqueLabPass 192.168.10.55 21300 0x60
```

Both DOS endpoints can instead share an mTCP configuration:

```dos
SET MTCPCFG=C:\MTCP.CFG
RAGENT pass:UniqueLabPass - 21300 -
```

With `MTCPCFG` set, running a fresh `RA-TSR` without arguments takes its IP,
packet-driver interrupt, and visible name from `IPADDR`, `PACKETINT`, and
`HOSTNAME` (or DHCP's `HOSTNAME_ASSIGNED`). It installs in conspicuous open
mode with file access disabled. The unused default file root does not need to
exist in that mode.

Resident, named, with an explicit file root:

```dos
MD C:\REMOTE
RA-TSR pass:UniqueLabPass 192.168.10.55 21300 0x60 C:\REMOTE RW WORKBENCH-386
```

For deliberate unrestricted access to every DOS drive, use the literal root
`ALL`. Network paths must then be absolute drive paths such as
`C:\CONFIG.SYS`; RA-TSR prints a prominent warning when `ALL` and write access
are enabled:

```dos
RA-TSR pass:UniqueLabPass 192.168.10.55 21300 0x60 ALL RW WORKBENCH-386
```

The Linux bridge still requires its independent file-read and file-write
flags shown below.

Connect directly:

```bash
DOS_MCP_TARGET=192.168.10.55:21300 \
DOS_MCP_PASSWORD=UniqueLabPass \
DOS_MCP_ALLOW_FILE_READ=1 \
DOS_MCP_ALLOW_FILE_WRITE=1 \
uv run dos-mcp
```

Or listen for disconnected RA-TSRs:

```bash
DOS_MCP_DISCOVERY=1 \
DOS_MCP_PASSWORD=UniqueLabPass \
uv run dos-mcp
```

For multiple fixed machines:

```bash
DOS_MCP_TARGETS='{"desk8088":"192.168.10.21","lab386":"192.168.10.38"}' \
DOS_MCP_PASSWORD=UniqueLabPass \
uv run dos-mcp
```

The bridge currently uses one UDP credential per process. Separate bridge
processes are recommended when targets have different secrets.

## Credentials

A password/passphrase of any nonzero length supported by the invoking command
line is deterministically reduced to a 128-bit key. A legacy 32-hex raw key
is still accepted. The credential is optional on both peers; omission selects
conspicuous open mode.

Open mode is unauthenticated and suitable only for an isolated test network.
Credentialed protocol v2 authenticates but does not encrypt traffic and uses
a deliberately short 32-bit packet tag for 8088 feasibility. Use a trusted
private LAN, a unique high-entropy credential per deployment, and never
forward the DOS operation port to the Internet.

## Test

```bash
uv run ruff check .
uv run python tools/check_docs.py
uv run pytest
make -C dos WATCOM=/path/to/watcom all
```

Foreground and resident DOSBox-X harnesses:

```bash
WATCOM=/path/to/watcom \
DOSBOX_X=/path/to/dosbox-x \
PACKET_DRIVER=/path/to/NE2000.COM \
tools/test_dosbox_x.sh

WATCOM=/path/to/watcom \
DOSBOX_X=/path/to/dosbox-x \
PACKET_DRIVER=/path/to/NE2000.COM \
tools/test_dosbox_x_tsr.sh
```

## Documentation

Start with [Documentation](docs/index.md):

- [Getting started](docs/getting-started.md)
- [Hardware commissioning](docs/hardware-commissioning.md)
- [Configuration](docs/configuration.md)
- [MCP tools](docs/mcp-tools.md)
- [DOS executables](dos/README.md)
- [RA-TSR](docs/tsr.md)
- [Discovery and multiple targets](docs/discovery.md)
- [Protocol v2](docs/protocol.md)
- [Architecture](docs/architecture.md)
- [Security model](docs/security-model.md)
- [DOS reentrancy](docs/dos-reentrancy.md)
- [Video capture](docs/video-capture.md)
- [Testing](docs/testing.md)
- [Roadmap](docs/roadmap.md)

[`PROJECT.md`](PROJECT.md) is the original brief. [`AGENTS.md`](AGENTS.md)
contains the current contributor constraints.

## License

A final open-source license has not yet been selected. Third-party packet
drivers are not redistributed by this repository.
