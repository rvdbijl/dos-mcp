# Configuration reference

The MCP bridge uses environment variables. The UDP simulator and foreground
DOS agent use command-line arguments.

## Bridge environment

| Variable | Default | Meaning |
|---|---|---|
| `DOS_MCP_TARGET` | unset | Selects UDP when set; value is `host` or `host:port` |
| `DOS_MCP_PASSWORD` | unset | Passphrase used to derive the 128-bit protocol key |
| `DOS_MCP_KEY` | unset | Raw 128-bit protocol key as 32 hexadecimal characters |
| `DOS_MCP_ROOT` | current directory | Linux backend starting directory |
| `DOS_MCP_SHELL` | `/bin/sh` | Absolute path to the Linux backend shell |

Backend selection is exclusive:

- with no `DOS_MCP_TARGET`, the bridge creates `LinuxTerminalBackend`;
- with `DOS_MCP_TARGET`, it creates `UdpBackend`;
- set either `DOS_MCP_PASSWORD` or `DOS_MCP_KEY`, never both;
- if neither credential variable is set, UDP uses unauthenticated open mode
  and writes a warning to stderr.

The default UDP port is 21300.

Examples:

```bash
# Local backend
DOS_MCP_ROOT=/srv/dos-work DOS_MCP_SHELL=/bin/bash uv run dos-mcp

# UDP target using the default port
DOS_MCP_TARGET=192.168.10.55 \
DOS_MCP_PASSWORD='MyUniqueLabPassphrase' \
uv run dos-mcp

# UDP target using an explicit port and legacy raw key
DOS_MCP_TARGET=127.0.0.1:22130 \
DOS_MCP_KEY=00112233445566778899AABBCCDDEEFF \
uv run dos-mcp
```

## Credential modes

The password form is the most convenient. SHA-256 hashes a domain separator
and the UTF-8 password, and the protocol uses the first 16 bytes. This is a
cross-platform key derivation, not a deliberately slow password hash: use a
long, high-entropy passphrase rather than a dictionary word. No password is
sent over the network.

Raw-key mode remains available. The key must be exactly 16 nonzero bytes
encoded as 32 hexadecimal characters. Generate one with:

```bash
openssl rand -hex 16
```

Open mode uses a fixed public protocol key so framing, integrity checks,
sessions, replay handling, and retries continue to operate. It provides no
authentication: any peer that can reach the UDP port can forge control
traffic. Use it only for isolated local testing.

Provision the same mode and credential on both peers. Treat passwords and raw
keys as secrets: do not put production credentials in the repository,
screenshots, command history, shared shell scripts, or MCP prompts.

## UDP simulator

```text
dos-mcp-simulator
  [--password TEXT | --key HEX]
  [--bind HOST:PORT]
  [--root DIRECTORY]
  [--shell ABSOLUTE_PATH]
```

| Argument | Default | Meaning |
|---|---|---|
| `--password` | unset | Passphrase used to derive the protocol key |
| `--key` | unset | Raw 32-hex protocol key |
| `--bind` | `127.0.0.1:21300` | UDP listen address |
| `--root` | current directory | Simulated target shell directory |
| `--shell` | `/bin/sh` | Simulated target shell |

The credential options are mutually exclusive. With neither, the simulator
uses open mode and logs a warning to stderr. It closes its PTY child when it
receives SIGINT or SIGTERM.

## Foreground DOS agent

```text
RAGENT [credential] [local-ip] [udp-port] [packet-driver-interrupt]
```

| Argument | Default | Meaning |
|---|---|---|
| `credential` | open mode | Password, raw key, or `-` for explicit open mode |
| `local-ip` | `10.0.2.15` | Static IPv4 address claimed by the agent |
| `udp-port` | `21300` | Application UDP destination port |
| `packet-driver-interrupt` | `0x60` | Installed packet-driver software interrupt |

The packet driver itself must already be loaded with the adapter's correct
I/O address and IRQ. `RAGENT` does not configure the Ethernet hardware.

Credential forms:

| Form | Meaning |
|---|---|
| `pass:text` | Derive a key from `text`; this also forces password treatment for 32 hex-looking characters |
| `key:32hex` | Explicit raw key |
| bare 32-hex value | Legacy raw key |
| any other bare nonempty value | Password |
| omitted or `-` | Unauthenticated open mode |

DOS command tails have a platform limit (normally 127 bytes), so the DOS-side
passphrase has no fixed protocol length but must fit that command line.
Printable ASCII without spaces is the most portable choice across DOS shells
and the modern host.

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

The checked-in `dosbox-test` password is public test data and must never be
treated as a deployment secret.

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
