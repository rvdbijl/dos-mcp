# Foreground DOS agent

`RAGENT.EXE` is the generic, non-PicoMEM DOS endpoint. It is a 16-bit
8088-targeted foreground program that talks to an Ethernet packet driver,
implements minimal ARP/IPv4/UDP, authenticates every application datagram,
captures the active text page, and injects BIOS key words.

## Components

| File | Responsibility |
|---|---|
| `agent/ragent.c` | Session state, operation dispatch, screen/keyboard services, foreground shell |
| `agent/dmproto.c` | CRC16, XTEA, packet MAC, key derivation, packet codec |
| `agent/dmnet.c` | Ethernet, ARP, IPv4, and UDP framing |
| `agent/dmpacket.c` | Packet-driver discovery, access handles, downcalls, receive state |
| `agent/dmpacket_rx.asm` | Register-safe receive upcall |
| `tests/proto_test.c` | Python-compatible protocol vectors |

The receive upcall does not parse packets or call DOS. It returns a
preallocated frame buffer on the first driver callback, records a length and
ready flag on the second, and returns immediately. The foreground loop does
all authenticated parsing and target work.

## Build

Install an Open Watcom 2 toolchain and run:

```bash
make -C dos WATCOM=/path/to/watcom all
```

The build uses `-0` (8086/8088 instruction generation), the small memory
model, size optimization, warnings as errors, and a small assembly receive
upcall. Outputs are placed in `dos/build/`:

- `RAGENT.EXE` — foreground network agent
- `PROTOCHK.EXE` — DOS protocol golden-vector test

The current build is approximately 30 KB on disk. Runtime conventional-memory
use and performance on a physical 4.77 MHz 8088 remain to be measured.

No packet-driver binary is redistributed by this repository. Obtain a
driver appropriate for the physical adapter and load it before `RAGENT`.
For the checked DOSBox-X configuration, place a compatible `NE2000.COM` in
`dos/build/`.

The packet driver must expose the FTP/Crynwr Ethernet API and allow access
handles for IPv4 (`0800h`) and ARP (`0806h`). SLIP and PPP framing are not
supported.

## Run on DOS

Load the adapter driver, then pass a unique high-entropy password:

```dos
NE2000 0x60 10 0x300
RAGENT pass:MyUniqueLabPassphrase 10.0.2.15 21300 0x60
```

Arguments:

```text
RAGENT [credential] [local-ip] [udp-port] [packet-driver-interrupt]
```

Defaults:

| Value | Default |
|---|---|
| Credential | Open mode |
| Local IP | `10.0.2.15` |
| UDP port | 21300 |
| Packet-driver interrupt | `60h` |

`local-ip` is static; this implementation has no DHCP client. Use an unused
address on the DOS machine's network. `RAGENT` does not configure the
adapter's IRQ or I/O base—those belong to the packet-driver command line.

`pass:text` derives a 128-bit key from the password. `key:32hex` forces a raw
key; a bare 32-hex value keeps the original raw-key syntax; any other
nonempty bare value is treated as a password. Use `-` when you want open mode
but still need to specify the later network arguments:

```dos
RAGENT - 10.0.2.15 21300 0x60
```

With no arguments, `RAGENT` also starts in open mode using all network
defaults. It prints a warning because open mode is unauthenticated and any
reachable peer can control the shell. DOS normally limits the complete
command tail to 127 bytes; use a no-space printable-ASCII passphrase that
fits that limit for portable matching with the modern host.

The startup display should include:

```text
Retro DOS Agent 0.1 - <ip>:<port>
Authenticated UDP ready.
EXIT or local Ctrl-Alt-Esc stops.
RAGENT>
```

The modern bridge targets the address with the same key:

```bash
DOS_MCP_TARGET=192.168.10.55:21300 \
DOS_MCP_PASSWORD=MyUniqueLabPassphrase \
uv run dos-mcp
```

The bridge also supports `DOS_MCP_KEY` for a raw key. Omit both credential
variables to match DOS open mode.

`EXIT` stops the foreground shell. Holding Ctrl+Alt while pressing Esc is
the local emergency stop.

## Implemented operations

- authenticated `HELLO` and session-key derivation;
- machine status and DOS version;
- capability discovery;
- 80×25 active text-page capture;
- BIOS mode, adapter, page, cursor, CP437 cells, and attributes;
- text and named BIOS keys with accepted counts;
- `PING`;
- cached resend of the last completed response.

The exact byte format and vectors are in
[`docs/protocol.md`](../docs/protocol.md).

## Foreground shell behavior

Incoming text is converted to BIOS scan/ASCII words. The foreground loop
consumes those words as its command line and invokes the DOS C runtime's
`system()` after Enter. This is why remote `DIR` or `VER` works even though
the agent correctly reports that no separate direct-execution capability
exists.

Keep commands short and noninteractive. While a child owns the foreground:

- screen contents may change normally;
- packet-driver receive frames are dropped;
- status and capture requests time out and retry;
- the local agent hotkey is not processed until control returns.

This behavior is intentional for the foreground prototype. It avoids DOS
reentrancy hazards while establishing the complete network/control path.

## DOSBox-X end-to-end test

The checked configuration uses NE2000 at I/O `300h`, IRQ 10, packet-driver
interrupt `60h`, SLiRP address `10.0.2.15`, and host UDP port 21300.

```bash
WATCOM=/path/to/watcom \
DOSBOX_X=/path/to/dosbox-x \
PACKET_DRIVER=/path/to/NE2000.COM \
tools/test_dosbox_x.sh
```

If DOSBox-X was unpacked rather than installed, also set `DOSBOX_LIBDIR`.
The test runs the 16-bit protocol vectors, boots the agent, performs an
authenticated handshake, queries status and capabilities, transfers a
fragmented 80×25 VGA screen, injects `VER` through the BIOS queue, and
verifies the captured result.

The profile in `dosbox-x.conf` mounts the build directory as `C:`, runs
`PROTOCHK`, loads NE2000 at I/O `300h`/IRQ 10/interrupt `60h`, and starts
`RAGENT` at `10.0.2.15:21300`. Its fixed `dosbox-test` password and MAC
address are public test values.

## Real-hardware bring-up

Suggested order:

1. boot DOS without unrelated network software;
2. record free conventional memory;
3. load the adapter's packet driver and verify its reported MAC, IRQ, I/O
   base, and software interrupt;
4. choose an unused static IP and a unique high-entropy passphrase or random
   key;
5. start `RAGENT`;
6. query status and capabilities from the bridge;
7. capture the prompt before sending input;
8. test `VER`, screen recapture, and local emergency stop;
9. record the result as described in
   [`docs/hardware-support.md`](../docs/hardware-support.md).

If initialization fails, verify that the packet driver is installed at the
specified interrupt and another DOS network stack has not exclusively
claimed the needed packet types.

## Current limits

This is the complete foreground MVP, not a TSR. It does not remain responsive
while arbitrary applications run, and it does not expose graphics,
filesystem, raw memory, port, reboot, or storage operations.

See [`docs/troubleshooting.md`](../docs/troubleshooting.md) for packet-driver,
timeout, video, and key-injection failures.
