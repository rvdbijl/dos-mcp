# DOS endpoints

The DOS build produces two generic, non-PicoMEM network endpoints:

- `RAGENT.EXE`, the preserved foreground shell/agent;
- `RA-TSR.EXE`, the resident loadable/unloadable agent.

Both target the 8086/8088 instruction set and use an FTP/Crynwr-compatible
Ethernet packet driver. MCP and JSON never run on DOS.

## Components

| File | Responsibility |
|---|---|
| `agent/ragent.c` | shared dispatch plus foreground/resident policies |
| `agent/dmconfig.c` | bounded `MTCPCFG` reader for `IPADDR`/`PACKETINT` |
| `agent/dmproto.c` | SHA-256 credential derivation, XTEA session derivation, Speck packet MAC, codecs |
| `agent/dmnet.c` | bounded Ethernet, ARP, IPv4, UDP, and limited broadcast |
| `agent/dmpacket.c` | packet-driver access/downcalls and receive state |
| `agent/dmpacket_rx.asm` | register-safe minimal receive upcall |
| `agent/ratsr_hooks.asm` | resident stack, timer/DOS-idle/multiplex hooks, BIOS ring |
| `tests/proto_test.c` | cross-language protocol vectors |
| `tests/config_test.c` | mTCP configuration parsing vectors |
| `tests/tsr_host.c` | DOSBox-only deterministic foreground test host |

## Build

```bash
make -C dos WATCOM=/path/to/watcom all
```

Outputs:

- `build/PROTOCHK.EXE`
- `build/CFGCHK.EXE`
- `build/RAGENT.EXE`
- `build/RA-TSR.EXE`
- `build/TSRHOST.EXE` (integration fixture only)

The Makefile uses Open Watcom `-0 -ms -os -s -wx`. No packet-driver binary is
redistributed; supply the appropriate driver for the adapter.

For a copyable hardware-PC bundle, run:

```bash
make -C dos WATCOM=/path/to/watcom bin
```

This refreshes the repository-root `bin/` directory with `RAGENT.EXE`,
`RA-TSR.EXE`, `PROTOCHK.EXE`, `CFGCHK.EXE`, a SHA-256 manifest, an editable
`MTCP.CFG`, DOS-readable instructions, and safe load/unload batch files.
`TSRHOST.EXE` is intentionally excluded because it is only an emulator test
fixture. No third-party packet driver or working site credential is bundled.

## mTCP configuration reuse

When the DOS environment variable `MTCPCFG` names a readable mTCP
configuration file, the endpoints read these keys:

```text
PACKETINT 0x60
HOSTNAME WORKBENCH-286
IPADDR 192.168.10.55
```

Set the full path in DOS, exactly as for mTCP utilities:

```dos
SET MTCPCFG=C:\MTCP.CFG
```

Precedence is per field: an explicit command-line value wins, otherwise the
value in `MTCPCFG` wins, otherwise the built-in default is used. A `-` in the
IP, port, packet-interrupt, or RA-TSR name position means “no command-line
override.” For the name, `HOSTNAME` wins over DHCP's `HOSTNAME_ASSIGNED`, then
the endpoint falls back to `DOS-PC`. The port is not an mTCP setting and
remains 21300 unless explicitly changed.

If `MTCPCFG` is set and a non-overridden `IPADDR` or `PACKETINT` is missing,
invalid, unreadable, or on a line longer than 255 bytes, startup fails with a
visible diagnostic. Unknown mTCP keys and comments are ignored. Recognized
values are the first whitespace-delimited token, matching mTCP's line-oriented
configuration style. RA-TSR still reads the file for its hostname when IP and
packet interrupt are explicit but the name is not.

Examples:

```dos
RAGENT pass:UniqueLabPass - 21300 -
RA-TSR pass:UniqueLabPass - 21300 - C:\REMOTE RW WORKBENCH-386
```

With a configured `MTCPCFG`, a completely argument-free first install is also
valid:

```dos
RA-TSR
```

It uses the configured network fields and hostname, open credential mode,
port 21300, and disabled file access. Because file access is disabled, the
default `C:\RATSR` root is not required to exist. A root is validated only
when `R`, `W`, or `RW` access is requested.

The endpoints reuse mTCP's configuration convention, not its TCP/IP code.
Some packet drivers may refuse a second client claiming IPv4/ARP packet types;
exit foreground mTCP programs before loading an endpoint if initialization
reports that the packet-driver type is already in use.

## Credentials

Both agents accept:

| Form | Meaning |
|---|---|
| `pass:text` | derive 128-bit key from any nonempty supported command-tail passphrase |
| `key:32hex` | explicit raw 128-bit key |
| bare 32 hex | legacy raw-key form |
| other bare text | passphrase |
| omitted or `-` | public unauthenticated open mode |

DOS's command tail is normally limited to 127 bytes. Visible ASCII without
spaces is the most portable passphrase form. Password derivation is fast, not
a slow password hash; use high entropy.

## Foreground RAGENT

```text
RAGENT [credential] [local-ip] [udp-port] [packet-driver-interrupt]
```

Example:

```dos
NE2000 0x60 10 0x300
RAGENT pass:UniqueLabPass 10.0.2.15 21300 0x60
```

Defaults, when not supplied by `MTCPCFG`, are open mode, `10.0.2.15`, port
21300, and packet interrupt `60h`.
RAGENT captures text and queues BIOS keys. Its own foreground loop consumes
the keys as commands. `EXIT` or local Ctrl+Alt+Esc stops it. A child command
temporarily prevents network service by design.

## Resident RA-TSR

```text
RA-TSR [credential] [local-ip] [port] [packet-int] [root] [access] [name]
```

Example:

```dos
MD C:\REMOTE
RA-TSR pass:UniqueLabPass 10.0.2.15 21300 0x60 C:\REMOTE RW DOSBOX-TSR
```

Additional defaults are root `C:\RATSR`, file access `-`, and name `DOS-PC`.
The root must already exist. Access is `-`, `R`, `W`, or `RW`. Name is 1–31
visible ASCII bytes without spaces.

RA-TSR supports:

- status/capabilities;
- streamed active text-page capture;
- BIOS key insertion;
- standard CGA/Hercules/EGA/VGA raw capture;
- sequential sandboxed download and temp-file/CRC upload commit;
- disconnected named local discovery;
- explicit unload.

Query:

```dos
RA-TSR
```

Unload:

```dos
RA-TSR /U
```

Unload refuses when a newer TSR owns one of RA-TSR's vectors or a transfer is
active. See
[`docs/tsr.md`](../docs/tsr.md) for the resident model, path policy, and
compatibility boundary.

## Modern bridge

Single target:

```bash
DOS_MCP_TARGET=192.168.10.55:21300 \
DOS_MCP_PASSWORD=UniqueLabPass \
uv run dos-mcp
```

Enable file access on the bridge as well:

```bash
DOS_MCP_ALLOW_FILE_READ=1 \
DOS_MCP_ALLOW_FILE_WRITE=1 \
DOS_MCP_TARGET=192.168.10.55 \
DOS_MCP_PASSWORD=UniqueLabPass \
uv run dos-mcp
```

Discovery:

```bash
DOS_MCP_DISCOVERY=1 DOS_MCP_PASSWORD=UniqueLabPass uv run dos-mcp
```

See [Configuration](../docs/configuration.md) and
[Discovery](../docs/discovery.md).

## DOSBox-X tests

Foreground:

```bash
WATCOM=/path/to/watcom \
DOSBOX_X=/path/to/dosbox-x \
PACKET_DRIVER=/path/to/NE2000.COM \
tools/test_dosbox_x.sh
```

Resident:

```bash
WATCOM=/path/to/watcom \
DOSBOX_X=/path/to/dosbox-x \
PACKET_DRIVER=/path/to/NE2000.COM \
tools/test_dosbox_x_tsr.sh
```

The resident harness verifies 16-bit protocol vectors, load, credentialed
session, status/capabilities, 32/256-byte packet responses, exact streamed
80×25 text capture, BIOS-key `VER`, exact patterned 64 KiB VGA mode 13h
capture, 2.5 KiB binary upload/download, and unload.

`TSRHOST.EXE` keeps deterministic guest timer progress under DOSBox-X's
headless command-shell behavior. It is a test program, not a shipped remote
agent.

## Real-hardware bring-up

1. boot DOS with minimal unrelated drivers;
2. record free conventional memory;
3. load the correct packet driver and record MAC/IRQ/I/O/interrupt;
4. create a dedicated transfer directory if needed;
5. choose a static unused IP and unique credential;
6. load RAGENT or RA-TSR;
7. verify status and capabilities before mutation;
8. capture the screen, test a harmless key sequence, and recapture;
9. test local query/unload before adding other TSRs;
10. record CPU, DOS, BIOS, adapter, driver, video, and timing results.

The current emulator evidence does not prove every 8088, DOS version, packet
driver, video clone, memory manager, game, protected-mode extender, or Windows
environment. Unsupported software should fail closed or be operated locally.
