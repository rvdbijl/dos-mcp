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
| DOSBox-X NE2000/SLiRP | Implemented | Repeatable end-to-end harness |
| Physical 8088 packet-driver machine | Designed, not yet measured here | Requires hardware validation |
| TSR observation agent | Not implemented | Foreground agent only |
| PicoMEM/PicoMEM2 | Intentionally not implemented | Outside current scope |

## DOS and CPU

`RAGENT.EXE` is compiled with Open Watcom's `-0` option for 8086/8088
instruction generation and the small memory model. The protocol vector
program and network agent run under DOSBox-X's DOS environment.

| Platform | Status |
|---|---|
| 8086/8088 instruction set | Build target; generated code still needs independent audit |
| DOSBox-X reported DOS 5.0 | End-to-end verified |
| MS-DOS 3.x | Not yet verified |
| MS-DOS 5.x/6.x on hardware | Not yet verified |
| PC DOS / DR-DOS | Not yet verified |
| FreeDOS | Not yet verified |
| 286/386+ real mode | Expected but not yet catalogued |

The foreground binary is roughly 30 KB on disk in the current build.
Runtime conventional-memory use has not yet been measured on physical
hardware.

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
| CGA graphics | Not implemented | Planned |
| Hercules graphics | Not implemented | Planned |
| EGA/VGA graphics | Not implemented | Planned |

The agent reads BIOS Data Area metadata and uses BIOS display-combination
information when available. It returns raw CP437 character bytes and
attribute bytes; rendering remains a modern-host responsibility.

## Keyboard

The DOS endpoint writes BIOS scan/ASCII words to the standard BIOS keyboard
ring buffer. It is appropriate for BIOS `INT 16h` and DOS console consumers.
It cannot promise compatibility with software that reads the keyboard
controller directly.

Enhanced F11/F12 BIOS words may not be understood by early BIOS
implementations. Printable text, Enter, Escape, Tab, Backspace, arrows,
navigation keys, F1–F10, Ctrl-C, and Ctrl-D still require representative
hardware testing.

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
