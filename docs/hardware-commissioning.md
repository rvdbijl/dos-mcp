# Hardware PC commissioning

The repository-root `bin/` directory is a self-contained DOS MCP payload for
first installation on a hardware DOS PC. It contains project-built files only;
obtain the correct packet driver for the Ethernet adapter separately.

## Bundle inventory

| File | Purpose |
|---|---|
| `RA-TSR.EXE` | resident endpoint for normal use |
| `RAGENT.EXE` | foreground diagnostic/alternative endpoint |
| `PROTOCHK.EXE` | offline protocol/crypto self-test |
| `CFGCHK.EXE` | offline mTCP configuration-parser self-test |
| `MTCP.CFG` | deliberately unusable TEST-NET example to edit |
| `STARTTSR.BAT` | credentialed install using `MTCPCFG` |
| `STOPTSR.BAT` | explicit resident unload |
| `README.TXT` | DOS-readable condensed instructions |
| `MANIFEST.SHA256` | host-side executable checksums |

No packet driver, usable network address, or default credential is shipped.
`STARTTSR.BAT` refuses to start without a passphrase argument.

## Prepare and copy

On Linux, verify or regenerate the binaries from the same checkout:

```bash
make -C dos WATCOM=/path/to/watcom bin
cd bin
sha256sum --check MANIFEST.SHA256
```

Copy the entire `bin/` directory to `C:\DOSMCP` using a method that preserves
binary files exactly. Floppy, removable media, a serial transfer, or an
already-working network transfer are all suitable. On DOS, the offline checks
provide a useful execution/format sanity check:

```dos
C:
CD \DOSMCP
PROTOCHK
CFGCHK
```

Both should print a line beginning with `PASS`. They do not load a packet
driver or touch the network.

## Configure the packet driver and address

Load the adapter's FTP/Crynwr-compatible Ethernet packet driver according to
its documentation. Record its software interrupt; `0x60` is only an example.
For an NE2000-compatible driver, a command might look like:

```dos
NE2000 0x60 5 0x300
```

The actual interrupt, IRQ, and I/O base must match the PC. Edit
`C:\DOSMCP\MTCP.CFG` so `PACKETINT` matches the loaded driver and `IPADDR` is
an unused static address on the Linux host's LAN. Also set the ordinary mTCP
`NETMASK`, `GATEWAY`, and `NAMESERVER` values so the same file remains useful
to mTCP programs:

```text
PACKETINT 0x60
IPADDR 192.168.10.55
NETMASK 255.255.255.0
GATEWAY 192.168.10.1
NAMESERVER 192.168.10.1
```

If mTCP DHCP has already populated a configuration file with `IPADDR`, the DOS
MCP endpoint can read that file after the DHCP program exits. RA-TSR itself
does not run DHCP and does not rewrite the file.

## Install and identify RA-TSR

For each PC, edit the final `DOS-PC` argument in `STARTTSR.BAT` to a unique
1–31 character name without spaces. Then install using a unique passphrase:

```dos
CD \DOSMCP
STARTTSR My-Unique-Lab-Passphrase
```

The batch file creates `C:\DOSMCP\REMOTE`, sets the `MTCPCFG` environment
variable, and runs the equivalent of:

```dos
RA-TSR PASS:My-Unique-Lab-Passphrase - 21300 - C:\DOSMCP\REMOTE RW DOS-PC
```

The two `-` placeholders mean that IP address and packet-driver interrupt come
from the mTCP file. The passphrase appears briefly in the DOS command tail; do
not reuse an important password. The file root is locally restricted to the
dedicated directory, while `RW` permits bridge-approved upload and download.
Change it to `R` or `-` before installation when less access is appropriate.

Run `RA-TSR` without arguments to query the installed copy. Run `STOPTSR` to
unload. Test unload before placing unrelated TSRs above RA-TSR, because it
correctly refuses to free memory if a newer handler owns one of its vectors.

## Connect the Linux bridge

For one directly addressed PC:

```bash
DOS_MCP_TARGET=192.168.10.55:21300 \
DOS_MCP_PASSWORD='My-Unique-Lab-Passphrase' \
DOS_MCP_ALLOW_FILE_READ=1 \
DOS_MCP_ALLOW_FILE_WRITE=1 \
uv run dos-mcp
```

For named local discovery:

```bash
DOS_MCP_DISCOVERY=1 \
DOS_MCP_PASSWORD='My-Unique-Lab-Passphrase' \
uv run dos-mcp
```

First call `dos.list_targets`, `dos.get_status`, `dos.get_capabilities`, and
`dos.capture_screen`. Only enable bridge file policy when file operations are
actually required.

## mTCP coexistence boundary

DOS MCP shares mTCP's configuration convention but uses its own bounded
ARP/IPv4/UDP implementation. Packet-driver clients register packet types, and
some drivers may reject overlapping IPv4/ARP registrations. Complete a
foreground mTCP operation and exit it before loading RA-TSR if packet-driver
initialization fails. Concurrent operation must be verified for each physical
driver rather than assumed from use of the same `MTCPCFG` file.

For parser precedence and failure rules, see
[Configuration reference](configuration.md). For hardware observations to
record, see [Hardware support](hardware-support.md).
