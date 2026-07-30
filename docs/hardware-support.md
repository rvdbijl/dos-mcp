# Hardware and platform support

This document separates implemented code, emulator verification, and
physical-hardware claims. Emulator success is not proof of timing or BIOS
compatibility on a 4.77 MHz 8088.

## Modern host

| Component | Status | Notes |
|---|---|---|
| Linux | Supported | PTY backend and current development environment |
| Python 3.12+ | Supported | Required by `pyproject.toml` |
| Other host operating systems | Not implemented | PTY/process assumptions are Linux-oriented |
| MCP stdio | Supported | Only MCP transport currently exposed |
| Streamable HTTP MCP | Not implemented | Possible later bridge feature |

## Target backends

| Target | Status | Verification |
|---|---|---|
| Linux PTY | Implemented | Automated tests |
| Linux UDP simulator | Implemented | Automated loopback UDP tests |
| DOSBox-X foreground endpoint | Implemented | Repeatable NE2000/SLiRP harness |
| DOSBox-X resident endpoint | Implemented | Text, VGA 13h, keys, files, unload |
| Physical 8088 packet-driver machine | Designed, not yet measured here | Requires hardware validation |
| Resident `RA-TSR` | Implemented | Emulator verified; physical timing unverified |
| PicoMEM/PicoMEM2 | Intentionally not implemented | Outside current scope |

## DOS and CPU

`RAGENT.EXE`, `RA-TSR.EXE`, and their assembly hooks are built for the
8086/8088 instruction set. `RA-TSR` uses a private resident stack and retains
its PSP-owned allocation. The same binaries can run in real mode on later
286-through-486 CPUs; this is an instruction-set claim, not a claim about
every BIOS, packet driver, or application.

| Platform | Status |
|---|---|
| 8086/8088 instruction set | Build target; generated code still needs independent audit |
| DOSBox-X reported DOS 5.0 | End-to-end verified |
| MS-DOS 3.x | Not yet verified |
| MS-DOS 5.x/6.x on hardware | Not yet verified |
| PC DOS / DR-DOS | Not yet verified |
| FreeDOS | Not yet verified |
| 286/386/486 real mode | Compatible build target; not yet catalogued on hardware |
| Windows DOS boxes / protected mode | Not supported by RA-TSR |

Runtime conventional-memory use and worst-case interrupt latency have not yet
been measured on physical hardware. The resident build deliberately retains
128 KiB for code, data, transfer buffers, and its private stack.

## Ethernet and packet drivers

The agent uses the FTP/Crynwr packet-driver API and requests Ethernet class
access for:

- EtherType `0800h` (IPv4);
- EtherType `0806h` (ARP).

The implementation provides minimal ARP, IPv4, and UDP itself. It does not
require an active mTCP application or a TCP stack.

| Driver/adapter | Status |
|---|---|
| Crynwr-compatible NE2000 driver in DOSBox-X | Verified |
| Physical NE1000/NE2000 | Not yet verified |
| Other packet-driver Ethernet adapters | API-compatible in design; unverified |
| SLIP/PPP packet drivers | Not supported by the Ethernet framing code |
| Wi-Fi bridges presenting Ethernet packet drivers | Unverified |

The callback accepts one maximum-size Ethernet frame at a time. Additional
frames arriving before foreground processing completes are dropped, and the
modern bridge relies on authenticated retries.

## Video

| Mode/adapter | Implementation | Verification |
|---|---|---|
| 80×25 VGA text | Implemented | DOSBox-X E2E |
| 80×25 EGA text | Implemented detection/copy path | Not independently verified |
| 80×25 CGA text | Implemented fallback/copy path | Not independently verified |
| 80×25 MDA text, mode 7 | Implemented memory selection | Not independently verified |
| Other text dimensions | Capped/not fully supported | Future work |
| CGA modes 4/5, 320×200×2bpp | Implemented raw 16 KiB capture | Not independently verified |
| CGA mode 6, 640×200×1bpp | Implemented raw 16 KiB capture | Not independently verified |
| Hercules, 720×348×1bpp | Implemented raw 32 KiB capture | Not independently verified |
| EGA modes 0Dh/0Eh/0Fh/10h | Implemented plane-major capture | Not independently verified |
| VGA modes 11h/12h | Implemented plane-major capture | Not independently verified |
| VGA mode 13h, 320×200×8bpp | Implemented raw 64,000-byte capture | DOSBox-X E2E |

The agent reads BIOS Data Area metadata and uses BIOS display-combination
information when available. It returns raw CP437 character bytes and
attribute bytes for text. Graphics results are raw adapter-memory layouts;
palette interpretation and rendering remain modern-host responsibilities.

Graphics register accesses are bracketed and restored. Emulator verification
does not establish that every clone adapter tolerates capture while a
timing-sensitive game is running.

## Keyboard

The DOS endpoint writes BIOS scan/ASCII words to the standard BIOS keyboard
ring buffer. It is appropriate for BIOS `INT 16h` and DOS console consumers.
It cannot promise compatibility with software that reads the keyboard
controller directly.

Enhanced F11/F12 BIOS words may not be understood by early BIOS
implementations. Printable text, Enter, Escape, Tab, Backspace, arrows,
navigation keys, F1–F10, Ctrl-C, and Ctrl-D still require representative
hardware testing.

## Files and discovery

`RA-TSR` implements bounded sequential files relative to one existing DOS
root. Read and write policy is selected at load time; writes use a temporary
file, CRC-32 verification, and rename-on-commit. The DOSBox-X test verifies a
2,560-byte binary round trip. FAT variants, sharing software, and network
redirectors remain unverified.

While it has no active bridge session, `RA-TSR` broadcasts a named discovery
advertisement at TTL 1 approximately every five seconds. Receipt has unit
coverage on Linux; DOSBox-X SLiRP is not evidence that physical switches,
routers, or packet drivers pass limited broadcast correctly.

## Recording new verification

When testing a real machine, record:

- system/BIOS identity and DOS version;
- CPU and clock;
- conventional memory before and after loading;
- adapter, packet-driver name/version, interrupt, IRQ, and I/O base;
- video adapter and mode;
- status/capability result;
- screen and cursor fidelity;
- key compatibility;
- average status and screen round-trip time;
- packet loss/retry observations;
- emergency-stop behavior.

Only move a platform from “unverified” after preserving enough information
for another contributor to reproduce the result.
