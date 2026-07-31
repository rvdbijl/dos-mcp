# RA-TSR resident DOS agent

`RA-TSR.EXE` is the background counterpart to the existing foreground
`RAGENT.EXE`. Both use the same generic packet-driver transport and protocol;
building the TSR did not replace or convert the foreground agent.

## Supported operations

The resident agent supports:

- authenticated status and capability queries;
- 80×25 text-mode screen capture from the active BIOS page;
- BIOS keyboard-buffer insertion;
- raw CGA, Hercules, EGA, and VGA graphics capture for the standard modes
  listed in [Video capture](video-capture.md);
- bounded binary file download and temp-file upload commit inside a
  configured root;
- explicit load, installed-state query, and unload;
- a visible user-supplied machine name and disconnected UDP announcements.

It does not expose a direct execute operation, arbitrary memory access, port
writes, reboot, disk-sector access, persistence, stealth, or Windows support.
It is not expected to service software that never permits safe timer progress,
replaces the hooked vectors, bypasses the BIOS keyboard queue, or monopolizes
video hardware in an undocumented way.

## Build and load

Build all DOS outputs with 8086 instruction generation:

```bash
make -C dos WATCOM=/path/to/watcom all
```

Load a packet driver, create the file root if file access is wanted, then
install:

```dos
MD C:\REMOTE
NE2000 0x60 10 0x300
RA-TSR pass:UniqueMachinePass 192.168.10.55 21300 0x60 C:\REMOTE RW WORKBENCH-386
```

Arguments are positional:

```text
RA-TSR [credential] [local-ip] [port] [packet-int] [root] [access] [name]
```

| Argument | Default | Rules |
|---|---|---|
| credential | open mode | `pass:text`, `key:32hex`, legacy bare key/password, or `-` |
| local IP | `10.0.2.15` | static IPv4 address; RA-TSR has no DHCP client |
| port | `21300` | authenticated operation port |
| packet interrupt | `0x60` | FTP/Crynwr packet-driver software interrupt |
| root | `C:\RATSR` | must exist only when file access is enabled |
| access | `-` | `-`, `R`, `W`, or `RW` |
| name | `DOS-PC` | 1–31 visible ASCII bytes without spaces |

When `MTCPCFG` is set, use `-` for local IP, packet interrupt, and optionally
name to read `IPADDR`, `PACKETINT`, and `HOSTNAME` from the shared mTCP file:

```dos
SET MTCPCFG=C:\MTCP.CFG
RA-TSR pass:UniqueMachinePass - 21300 - C:\REMOTE RW -
```

An explicit positional IP or interrupt overrides its corresponding file key.
An explicit name overrides `HOSTNAME`; DHCP's `HOSTNAME_ASSIGNED` is the name
fallback when `HOSTNAME` is absent.
See [Configuration reference](configuration.md) for strict parsing and failure
behavior, or [Hardware commissioning](hardware-commissioning.md) for the
copy-ready bundle workflow.

A fresh argument-free install is supported when the packet driver is loaded:

```dos
SET MTCPCFG=C:\MTCP.CFG
RA-TSR
```

This selects the configured network identity, port 21300, open credential
mode, and no file access. Since file access is off, a missing default
`C:\RATSR` directory does not block installation.

The name is an operator label, not authentication. If names collide, the
Linux bridge exposes selectors such as `WORKBENCH@acde48444d02`.

### Unrestricted drive mode

The literal root `ALL` is an explicit commissioning mode that exposes every
drive visible to DOS. In this mode, every network file path must be an
absolute drive path such as `C:\DOS\MEM.TXT`; `C:MEM.TXT`, UNC paths, and
relative paths are rejected. `ALL W` and `ALL RW` print a prominent
unrestricted-access warning during installation.

```dos
RA-TSR - - 21300 - ALL RW
```

This example obtains IP address, packet interrupt, and name from `MTCPCFG`,
uses open credential mode, and enables reads and writes. The Linux bridge
must still opt into both operations with `DOS_MCP_ALLOW_FILE_READ=1` and
`DOS_MCP_ALLOW_FILE_WRITE=1`.

## Query and unload

Run `RA-TSR` again with no arguments to query the installed instance. Unload
with:

```dos
RA-TSR /U
```

Unload succeeds only when RA-TSR still owns all four interrupt vectors.
If another TSR was installed above it, RA-TSR refuses to free memory because
doing so would leave the newer program with dangling chains. It also refuses
while a file or graphics transfer is active. After the transfer completes or
idle-session cleanup aborts it, unload disables new work, releases
packet-driver handles from normal process context, restores the saved vectors,
and frees the resident PSP block. DOSBox-X verifies an immediate reload after
unload, including a driver that returns valid packet handle zero.

## Resident execution model

The packet-driver receive callback is an assembly producer only. It offers a
fixed 1,518-byte buffer, records the completed length, and returns. It never
parses a request or calls DOS.

The resident worker has three chained entries on one private stack. `INT 1Ch`
handles normal timer-safe networking, screen, graphics, and keyboard work.
`INT 08h` first invokes the saved BIOS timer and runs the same non-DOS worker
only when that BIOS chain did not reach RA-TSR's `INT 1Ch`. `INT 28h` is the
only entry allowed to perform DOS filesystem calls. An active flag prevents
nested worker execution while still chaining nested interrupts. The worker:

1. expires an inactive authenticated session;
2. emits a disconnected discovery announcement when due;
3. encodes or sends at most one response fragment;
4. consumes at most one completed receive buffer;
5. leaves file requests queued until a DOS-idle entry with no critical error;
6. restores the interrupted stack and chains the prior handler.

Running `RA-TSR` with no arguments prints scheduler, packet callback, send,
last-activity, and PIC-mask counters. The remote protocol's resident
diagnostics operation reports the same state plus ownership bits for all four
vectors. Counters are unsigned 16-bit commissioning values and wrap naturally.

Text VRAM is read in 256-byte response fragments rather than copied as a
single 4 KiB interrupt operation. Large file and graphics data use explicit
900-byte block requests, CRC32 state, and bounded packet fragments. The
application-visible result is complete only after the modern bridge verifies
every block and final checksum.

## File sandbox and transactions

File access is disabled unless `R`, `W`, or `RW` is supplied locally. Paths
are limited to 80 bytes. A normal root accepts only relative paths under that
root. The special root `ALL` accepts only absolute drive-qualified paths.
RA-TSR rejects:

- absolute paths and drive letters in normal-root mode, or non-absolute paths
  in `ALL` mode;
- empty, `.` or `..` components;
- components longer than DOS 8.3's 12-character rendered form;
- DOS wildcard/control punctuation;
- a combined path that exceeds its bounded resident buffer.

Downloads stream sequentially and end with size and CRC32 verification.
Uploads declare size, CRC32, and overwrite policy before data is accepted.
Data goes to a newly created temporary file in the destination directory;
commit verifies byte count and checksum, then renames it. An existing
destination is replaced only when both the Linux
bridge and RA-TSR were explicitly configured to allow writes and the MCP call
sets `overwrite=true`.

This is a path-policy boundary, not a replacement for DOS filesystem
permissions or a defense against a compromised local DOS kernel.

## Compatibility

The build uses Open Watcom `-0`, so generated code and handwritten assembly
use the 8086/8088 instruction set and run on later 286, 386, and 486 CPUs.
The integration suite uses a DOSBox-X NE2000 packet driver at a fixed
12,000-cycle profile. Physical verification on representative adapters and a
4.77 MHz 8088 remains required before making timing claims.

RA-TSR intentionally makes no Windows, protected-mode extender, direct-input
game, or arbitrary TSR-stacking guarantee. See [DOS reentrancy](dos-reentrancy.md)
and [Hardware support](hardware-support.md).
