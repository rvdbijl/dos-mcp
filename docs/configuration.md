# Configuration reference

The MCP bridge uses environment variables. The UDP simulator and foreground
DOS agent use command-line arguments.

## Bridge environment

| Variable | Default | Meaning |
|---|---|---|
| `DOS_MCP_TARGET` | unset | Selects UDP when set; value is `host` or `host:port` |
| `DOS_MCP_KEY` | unset | Required with `DOS_MCP_TARGET`; 32 hexadecimal characters |
| `DOS_MCP_ROOT` | current directory | Linux backend starting directory |
| `DOS_MCP_SHELL` | `/bin/sh` | Absolute path to the Linux backend shell |

Backend selection is exclusive:

- with no `DOS_MCP_TARGET`, the bridge creates `LinuxTerminalBackend`;
- with `DOS_MCP_TARGET`, it creates `UdpBackend` and requires
  `DOS_MCP_KEY`.

The default UDP port is 21300.

Examples:

```bash
# Local backend
DOS_MCP_ROOT=/srv/dos-work DOS_MCP_SHELL=/bin/bash uv run dos-mcp

# UDP target using the default port
DOS_MCP_TARGET=192.168.10.55 \
DOS_MCP_KEY=8D6A33C8B7A0521DFEF7926C44819A20 \
uv run dos-mcp

# UDP target using an explicit port
DOS_MCP_TARGET=127.0.0.1:22130 \
DOS_MCP_KEY=00112233445566778899AABBCCDDEEFF \
uv run dos-mcp
```

## Key format

The protocol key is 16 bytes encoded as exactly 32 hexadecimal characters.
Uppercase and lowercase hex are accepted. An all-zero key is rejected.

Generate a key on the modern host with a cryptographic random-byte tool, for
example:

```bash
openssl rand -hex 16
```

Provision the same value on the bridge and DOS command line. Treat it as a
secret: do not put a production key in the repository, screenshots, command
history, shared shell scripts, or MCP prompts.

## UDP simulator

```text
dos-mcp-simulator
  --key HEX
  [--bind HOST:PORT]
  [--root DIRECTORY]
  [--shell ABSOLUTE_PATH]
```

| Argument | Default | Meaning |
|---|---|---|
| `--key` | required | Protocol pre-shared key |
| `--bind` | `127.0.0.1:21300` | UDP listen address |
| `--root` | current directory | Simulated target shell directory |
| `--shell` | `/bin/sh` | Simulated target shell |

The simulator logs to stderr and closes its PTY child when it receives SIGINT
or SIGTERM.

## Foreground DOS agent

```text
RAGENT keyhex [local-ip] [udp-port] [packet-driver-interrupt]
```

| Argument | Default | Meaning |
|---|---|---|
| `keyhex` | required | Protocol pre-shared key |
| `local-ip` | `10.0.2.15` | Static IPv4 address claimed by the agent |
| `udp-port` | `21300` | Application UDP destination port |
| `packet-driver-interrupt` | `0x60` | Installed packet-driver software interrupt |

The packet driver itself must already be loaded with the adapter's correct
I/O address and IRQ. `RAGENT` does not configure the Ethernet hardware.

## DOSBox-X test configuration

[`dos/dosbox-x.conf`](../dos/dosbox-x.conf) is a deterministic test profile:

| Setting | Value |
|---|---|
| Adapter | NE2000 |
| I/O base | `300h` |
| IRQ | 10 |
| MAC | `AC:DE:48:44:4D:01` |
| Backend | SLiRP |
| Guest IP | `10.0.2.15` |
| Host/guest UDP forward | 21300 |
| Packet-driver interrupt | `60h` |

The checked-in key is public test data and must never be treated as a
deployment secret.

The harness accepts:

| Variable | Meaning |
|---|---|
| `WATCOM` | Open Watcom installation root |
| `DOSBOX_X` | DOSBox-X executable |
| `DOSBOX_LIBDIR` | Optional directory for unpacked shared libraries |
| `PACKET_DRIVER` | Path to a compatible `NE2000.COM` |
| `UV_CACHE_DIR` | Optional uv cache location |
| `SDL_VIDEODRIVER` | Defaults to `dummy` in the harness |
| `SDL_AUDIODRIVER` | Defaults to `dummy` in the harness |

See [Testing](testing.md) for the complete command.
