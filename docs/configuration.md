# Configuration reference

## Bridge environment

| Variable | Default | Meaning |
|---|---|---|
| `DOS_MCP_TARGET` | unset | one UDP endpoint, `host` or `host:port` |
| `DOS_MCP_TARGETS` | unset | JSON object of selector to `host[:port]` |
| `DOS_MCP_DISCOVERY` | `0` | set `1` to listen for RA-TSR announcements |
| `DOS_MCP_DISCOVERY_PORT` | `21301` | local announcement listener port |
| `DOS_MCP_PASSWORD` | unset | passphrase for all UDP targets in this process |
| `DOS_MCP_KEY` | unset | raw 128-bit key as 32 hexadecimal characters |
| `DOS_MCP_ALLOW_FILE_READ` | `0` | permit backend download operations |
| `DOS_MCP_ALLOW_FILE_WRITE` | `0` | permit backend upload operations |
| `DOS_MCP_ROOT` | current directory | Linux backend root/starting directory |
| `DOS_MCP_SHELL` | `/bin/sh` | Linux backend shell |

`DOS_MCP_TARGET` and `DOS_MCP_TARGETS` are mutually exclusive.
`DOS_MCP_TARGETS` and discovery may be combined. If none of the three UDP
selection modes is enabled, the bridge uses `LinuxTerminalBackend`.

Boolean variables accept only `0`, `1`, or unset.

Examples:

```bash
# Local Linux development target
DOS_MCP_ROOT=/srv/dos-work DOS_MCP_SHELL=/bin/bash uv run dos-mcp

# One DOS/simulator target
DOS_MCP_TARGET=192.168.10.55 \
DOS_MCP_PASSWORD='UniqueLabPass' \
uv run dos-mcp

# Named static targets
DOS_MCP_TARGETS='{"desk8088":"192.168.10.21","lab386":"192.168.10.38:22300"}' \
DOS_MCP_PASSWORD='shared-lab-test-pass' \
uv run dos-mcp

# Dynamic resident targets
DOS_MCP_DISCOVERY=1 \
DOS_MCP_PASSWORD='shared-lab-test-pass' \
uv run dos-mcp

# Static plus dynamic, with file operations allowed at the bridge
DOS_MCP_TARGETS='{"bench":"192.168.10.20"}' \
DOS_MCP_DISCOVERY=1 \
DOS_MCP_PASSWORD='shared-lab-test-pass' \
DOS_MCP_ALLOW_FILE_READ=1 \
DOS_MCP_ALLOW_FILE_WRITE=1 \
uv run dos-mcp
```

The bridge currently resolves one credential for all UDP records. Prefer
unique target credentials by running separate bridge processes until
per-target secret-provider support exists.

## Credentials

Set exactly one of `DOS_MCP_PASSWORD` and `DOS_MCP_KEY`.

Password mode computes:

```text
SHA-256("DOS-MCP credential v1" || NUL || UTF-8(password))[0:16]
```

It accepts any nonempty environment value. This is a compatibility derivation
that 16-bit DOS can reproduce, not a slow password hash. Use a long generated
passphrase.

Raw-key mode requires exactly 32 hexadecimal characters and rejects the
all-zero key. Generate:

```bash
openssl rand -hex 16
```

If neither variable is set for UDP, the bridge logs a warning and uses
unauthenticated open mode. Open mode is only for isolated testing.

## UDP simulator

```text
dos-mcp-simulator
  [--password TEXT | --key HEX]
  [--bind HOST:PORT]
  [--root DIRECTORY]
  [--shell ABSOLUTE_PATH]
  [--allow-file-read]
  [--allow-file-write]
```

Default bind is `127.0.0.1:21300`. File permissions must also be enabled on
the connecting bridge.

## Foreground DOS agent

```text
RAGENT [credential] [local-ip] [udp-port] [packet-driver-interrupt]
```

Defaults: open mode, `10.0.2.15`, 21300, `0x60`.

## DOS `MTCPCFG`

`RAGENT` and `RA-TSR` honor the conventional DOS environment variable used by
mTCP:

```dos
SET MTCPCFG=C:\MTCP.CFG
```

`MTCPCFG` must contain a full readable path. The DOS endpoints recognize
`IPADDR`, `PACKETINT`, `HOSTNAME`, and `HOSTNAME_ASSIGNED`,
case-insensitively, while ignoring blank lines, `#` comments, and other mTCP
keys:

```text
PACKETINT 0x60
HOSTNAME WORKBENCH-286
IPADDR 192.168.10.55
NETMASK 255.255.255.0
GATEWAY 192.168.10.1
NAMESERVER 192.168.10.1
```

The last valid occurrence of a recognized key wins. IPv4 octets must be
decimal 0–255; `PACKETINT` accepts decimal or C-style hexadecimal from 1 to
255. Values end at whitespace, so additional annotation after a valid value
does not make the address invalid. A recognized malformed value or an overlong
line rejects the whole file without partially changing endpoint configuration.

Precedence is evaluated separately for IP and packet interrupt:

1. explicit positional argument;
2. the matching `MTCPCFG` key;
3. built-in default (`10.0.2.15` or `0x60`).

RA-TSR name precedence is explicit name, `HOSTNAME`,
`HOSTNAME_ASSIGNED`, then `DOS-PC`. Use `-` in the final name position to
request the configuration-file name explicitly.

Use `-` to leave the IP, port, or packet-interrupt positional argument
unmodified. For example:

```dos
RAGENT pass:UniqueLabPass - 21300 -
RA-TSR pass:UniqueLabPass - 21300 - C:\REMOTE RW WORKBENCH-386
```

If `MTCPCFG` is set and either non-overridden required key is missing or the
file cannot be read, startup fails rather than silently mixing an unintended
address. If both IP and packet interrupt are explicit, `MTCPCFG` is not read.
The UDP port is DOS MCP-specific and is never obtained from the mTCP file.

When a fresh RA-TSR is run with no arguments and `MTCPCFG` is set, it uses the
file's `IPADDR`, `PACKETINT`, and hostname. Other defaults remain: port 21300,
open credential mode, and disabled file access. `C:\RATSR` need not exist while
file access is disabled; an enabled `R`, `W`, or `RW` normal root must exist.
The literal root `ALL` is an explicit exception and enables drive-qualified
paths across every mounted DOS drive.

This behavior follows the mTCP configuration-file convention documented in
the [mTCP user documentation](https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/net/mtcp/2025-01-10/mTCP_2025-01-10.pdf).

## Resident DOS agent

```text
RA-TSR [credential] [local-ip] [port] [packet-int] [root] [access] [name]
```

| Argument | Default |
|---|---|
| credential | open mode |
| local IP | `10.0.2.15` |
| operation port | `21300` |
| packet interrupt | `0x60` |
| existing file root | `C:\RATSR` |
| file access | `-` |
| discovery name | `DOS-PC` |

The local-IP, packet-interrupt, and name defaults apply only when no explicit
value or active `MTCPCFG` value supplies that field.

Access values are `-`, `R`, `W`, and `RW`. Name is 1–31 visible ASCII bytes
without spaces. Discovery sends to UDP 21301 at build-time default.

With a normal root, file-operation paths are relative to that root. With root
`ALL`, paths must instead be absolute DOS drive paths such as
`C:\DATA\FILE.BIN`; drive-relative forms such as `C:FILE.BIN` remain invalid.
RA-TSR prints `WARNING: UNRESTRICTED FILE ACCESS - ALL DOS DRIVES EXPOSED.`
when `ALL` is combined with `W` or `RW`. For example, using the shared mTCP
network identity and hostname:

```dos
RA-TSR - - 21300 - ALL RW
```

Credential forms on DOS:

| Form | Meaning |
|---|---|
| `pass:text` | force passphrase interpretation |
| `key:32hex` | force raw-key interpretation |
| bare 32 hex | legacy raw key |
| other bare nonempty text | passphrase |
| omitted or `-` | open mode |

## DOSBox-X profiles

`dos/dosbox-x.conf` tests foreground RAGENT. `dos/dosbox-x-tsr.conf` tests
RA-TSR with:

- NE2000 I/O `300h`, IRQ 10, packet interrupt `60h`;
- SLiRP guest IP `10.0.2.15`;
- fixed public test password `dosbox-test`;
- resident root `C:\REMOTE`, access `RW`;
- deterministic `TSRHOST.EXE`.

Both profiles set `MTCPCFG=C:\MTCP.CFG`; the harness copies a deterministic
fixture and passes `-` for IP and packet interrupt. This exercises the same
configuration route used by the hardware commissioning bundle.

Harness variables:

| Variable | Meaning |
|---|---|
| `WATCOM` | Open Watcom root |
| `DOSBOX_X` | DOSBox-X executable |
| `DOSBOX_LIBDIR` | optional shared-library directory |
| `PACKET_DRIVER` | external compatible `NE2000.COM` |
| `UV_CACHE_DIR` | optional uv cache |
| `SDL_VIDEODRIVER` | defaults to `dummy` |
| `SDL_AUDIODRIVER` | defaults to `dummy` |
