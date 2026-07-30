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

Access values are `-`, `R`, `W`, and `RW`. Name is 1–31 visible ASCII bytes
without spaces. Discovery sends to UDP 21301 at build-time default.

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
