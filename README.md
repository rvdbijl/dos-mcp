# DOS MCP

DOS MCP lets a modern MCP client observe and control a text-mode DOS
computer without putting MCP, JSON, HTTP, or an AI runtime on the retro
machine.

The modern Python bridge exposes four high-level tools:

- `dos.get_status`
- `dos.get_capabilities`
- `dos.capture_screen`
- `dos.send_keys`

The target can be a local Linux PTY for development, a Linux-backed UDP
simulator, or the real 16-bit `RAGENT.EXE` foreground DOS endpoint.

## What works

| Capability | Linux PTY | UDP simulator | Foreground DOS |
|---|:---:|:---:|:---:|
| MCP over stdio | Yes | Yes, through bridge | Yes, through bridge |
| Status/capabilities | Yes | Yes | Yes |
| 80×25 text capture | Yes | Yes | Yes |
| Cursor and cell attributes | Yes | Yes | Yes |
| Keyboard input | Terminal sequences | UDP (authenticated or open) | BIOS queue |
| Retry/fragmentation testing | No | Yes | Yes |
| Packet-driver Ethernet | No | No | Yes |

The DOS path has been verified end to end with DOSBox-X, SLiRP, an emulated
NE2000, and a Crynwr-compatible packet driver. The test authenticates, queries
status and capabilities, receives a four-fragment VGA screen, types `VER`
through the BIOS queue, and captures the resulting output.

PicoMEM/PicoMEM2-specific work is intentionally absent.

## Architecture

```text
MCP client
    │ MCP over stdio
    ▼
Python DOS MCP bridge
    │ transport-independent Backend operations
    ├──────── local PTY ──────── Linux shell
    │
    └──────── authenticated/open UDP
                    │
                    ├──────── Linux simulator
                    └──────── RAGENT.EXE
                                  │
                                  └── DOS packet driver / Ethernet
```

The DOS program implements only the work that must happen on the target:
minimal ARP/IPv4/UDP, packet authentication, text-memory capture, BIOS key
injection, and a small foreground command loop. The modern host owns MCP,
timeouts, retries, reassembly, CP437 conversion, policy, and structured
results.

## Quick start: local backend

Requirements:

- Linux
- Python 3.12 or newer
- [`uv`](https://docs.astral.sh/uv/)

Install and run:

```bash
uv sync
uv run dos-mcp
```

The process is a stdio MCP server, so silence while it waits for a client is
expected.

Select a starting directory and shell:

```bash
DOS_MCP_ROOT=/path/to/workspace \
DOS_MCP_SHELL=/bin/bash \
uv run dos-mcp
```

`DOS_MCP_ROOT` is not a security sandbox. The child shell retains the
permissions of the bridge user.

## Quick start: UDP simulator

Terminal one:

```bash
SIM_PASSWORD='replace-this-test-password'

uv run dos-mcp-simulator \
  --bind 127.0.0.1:21300 \
  --password "$SIM_PASSWORD" \
  --root "$PWD"
```

Terminal two:

```bash
DOS_MCP_TARGET=127.0.0.1:21300 \
DOS_MCP_PASSWORD="$SIM_PASSWORD" \
uv run dos-mcp
```

Use a unique high-entropy passphrase for each real target. A raw 128-bit
hexadecimal key is still supported for backward compatibility.

## Quick start: DOS target

Build with Open Watcom 2:

```bash
make -C dos WATCOM=/path/to/watcom all
```

After loading the appropriate DOS packet driver:

```dos
RAGENT pass:MyUniqueLabPassphrase 192.168.10.55 21300 0x60
```

Start the bridge with the same key:

```bash
DOS_MCP_TARGET=192.168.10.55:21300 \
DOS_MCP_PASSWORD=MyUniqueLabPassphrase \
uv run dos-mcp
```

The credential is optional: omit it on both sides to use open mode. Open mode
is deliberately conspicuous and unauthenticated; use it only on an isolated
test network.

See [the foreground DOS guide](dos/README.md) for packet-driver arguments,
the local emergency stop, and the complete DOSBox-X test.

## Security boundary

Keyboard input is a mutation and can execute commands in either foreground
shell. Capture and verify the screen before and after sending input.

Protocol version 1 authenticates packets, checks integrity, rejects replayed
request IDs, and suppresses duplicate mutations. It does not encrypt traffic.
Use a unique passphrase or key per target on a trusted private network, and
never expose the agent UDP port directly to the Internet. Open mode allows
any network peer to control the foreground shell.

Read [Security model](docs/security-model.md),
[Operations and safety](docs/operations.md), and [Security policy](SECURITY.md)
before using a real machine.

## Test

```bash
uv run ruff check .
uv run python tools/check_docs.py
uv run pytest
```

The UDP tests bind localhost sockets. Environments that disable loopback
networking must grant local socket access.

The cross-language and DOSBox-X layers are documented in
[Testing](docs/testing.md).

## Current boundary

This is the complete foreground MVP from the project brief. It is not yet a
TSR and cannot remain network-responsive while arbitrary child applications
run. Graphics capture, direct filesystem transfer, direct execution, memory,
port I/O, reboot, and storage operations are deferred and reported as
unavailable capabilities.

## Documentation

Start with the [documentation index](docs/index.md).

- [Getting started](docs/getting-started.md)
- [Configuration reference](docs/configuration.md)
- [MCP tool reference](docs/mcp-tools.md)
- [Foreground DOS agent](dos/README.md)
- [UDP simulator](docs/simulator.md)
- [Testing](docs/testing.md)
- [Troubleshooting](docs/troubleshooting.md)
- [Architecture](docs/architecture.md)
- [Protocol version 1](docs/protocol.md)
- [DOS reentrancy](docs/dos-reentrancy.md)
- [Video capture](docs/video-capture.md)
- [Hardware support](docs/hardware-support.md)
- [Development guide](docs/development.md)
- [Roadmap](docs/roadmap.md)
- [Contributing](CONTRIBUTING.md)

[`PROJECT.md`](PROJECT.md) contains the original complete project brief, and
[`AGENTS.md`](AGENTS.md) contains concise contributor constraints.

## License

A final open-source license has not yet been selected. Third-party packet
drivers are not redistributed by this repository.
