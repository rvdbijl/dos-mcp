# DOS reentrancy and resident-work rules

## Core rule

Packet-driver callbacks do not parse requests, allocate, call DOS, touch
video hardware, or run response state machines. They only provide/copy a
bounded preallocated frame, record completion, and return.

RA-TSR's timer entry is a deferred worker, not permission to call arbitrary
DOS services. Every resident operation must have a bounded execution path and
an explicitly reviewed context.

## Contexts

### Packet-driver receive upcall

Implemented in `dmpacket_rx.asm`:

1. allocation phase returns the fixed 1,518-byte buffer in `ES:DI`;
2. an already-ready or oversized frame receives a null buffer and is dropped;
3. completion phase records only `CX` and a ready byte;
4. the handler returns with `IRET`.

Loss is expected and recovered through modern-host request retries.

### `INT 1Ch` resident entry

Implemented in `ratsr_hooks.asm`:

- save all general/segment registers;
- load resident DGROUP;
- if a worker is already active, restore and chain immediately;
- save the interrupted `SS:SP`;
- switch atomically to the private 32 KiB resident stack;
- set direction forward and enable interrupts;
- call the bounded C worker;
- disable interrupts, restore the exact original stack, clear active state;
- restore registers and far-chain the prior `INT 1Ch` handler.

Nested timer ticks still reach the previous handler but cannot run a second
RA-TSR worker. Packet IRQs may fill the single receive buffer while the worker
runs; an already-ready frame is dropped.

The current 32 KiB stack and retained 128 KiB PSP allocation are conservative
until linker-derived and physical high-water measurements are available.

### `INT 28h` DOS-idle entry

The DOS-idle handler uses the same save/switch/guard/restore sequence and
chains the previous `INT 28h` handler. It passes an explicit DOS-idle flag to
the C worker. Only this entry may dispatch file opcodes or clean up a DOS file
handle; a timer entry leaves the receive buffer queued.

This follows the conventional TSR rule that suitable DOS functions may be
called during DOS's idle interrupt. The critical-error flag is still checked.
The test host explicitly issues `INT 28h` while waiting so emulator file
transfers exercise this path.

### Multiplex and unload entry

`INT 2Fh AX=D05Ah` supports query, unload preparation, packet-handle query,
and diagnostic state. Unload preparation stops resident work. After the
interrupt returns, the transient unloader releases both packet-driver handles
from ordinary process context, then restores the verified `INT 1Ch`, `INT 28h`,
and `INT 2Fh` vectors and frees the PSP block. Packet handle zero is valid and
is distinct from the internal `FFFFh` invalid sentinel.
It refuses while a transfer owns a DOS handle, avoiding DOS close/remove
calls from the multiplex interrupt.

If another TSR is above RA-TSR, unload refuses. Removing memory from the
middle of a chain would be unsafe.

## Operation rules

| Work | Resident rule |
|---|---|
| Packet decode/MAC | one bounded received datagram |
| Status/capabilities | BDA/cached values only |
| Text capture | metadata snapshot, then ≤256 VRAM bytes per response entry |
| Keyboard insertion | direct validated BIOS ring update with preserved flags |
| Packet response | at most one encoded/sent fragment per entry |
| Graphics | explicit ≤900-byte sequential reads; plane registers restored |
| File read/write | only from `INT 28h`, with critical-error flag clear |
| Discovery | one bounded TTL-1 broadcast, only disconnected and interval due |
| Allocation | forbidden while resident |
| Direct execution | not implemented |

Text capture caches mode/page/cursor metadata at begin and streams VRAM
fragments. This avoids the emulator failure observed when a complete 4 KiB
screen was copied and authenticated in one timer invocation.

Graphics transactions read only the requested bounded block. EGA/VGA reads
save the graphics-controller index/read-map state and restore it afterward.
The transfer must complete sequentially and pass final CRC32.

## DOS file calls

RA-TSR records DOS's InDOS pointer through `INT 21h AH=34h` during install
and derives the adjacent critical-error flag used by the supported DOS
convention. File opcodes are never dispatched from `INT 1Ch`; they remain
queued until the chained DOS-idle entry and a clear critical-error state.

The file state machine permits one transfer:

- open/create at begin;
- one sequential read/write per request;
- close and verify at end/commit;
- close/remove temporary state at abort or idle-session cleanup.

Unload refuses while a transfer is active rather than calling DOS from
`INT 2Fh`. This strategy is intentionally conservative but still requires
validation of `INT 28h`, critical-error conventions, and the selected file
functions on each DOS family. Emulator success alone is not a universal
reentrancy proof.

## Keyboard notes

Resident insertion uses the original PC/XT BIOS ring at BDA offsets
`40:1A`/`40:1C`, validates head/tail alignment/range, checks full state, writes
one scan/ASCII word, and preserves the caller's interrupt flags. Partial
acceptance is reported.

This works for software using BIOS/DOS console input. Programs reading the
8042/controller or replacing the BIOS ring are outside the guarantee.

## Foreground RAGENT

RAGENT retains the simpler model: its callback only records a frame, and its
main loop performs all protocol and DOS work. It consumes injected BIOS words
as a command shell and invokes `system()` only from foreground context. No TSR
rules are silently applied to it.

## Emulator test host

DOSBox-X's internal command prompt may suspend guest instruction execution
aggressively. `TSRHOST.EXE` is a test-only foreground BIOS keyboard loop that
keeps deterministic timer progress, issues DOS-idle interrupts while
waiting, runs `VER`, switches VGA mode, and exits so the batch file can unload
RA-TSR. It is not installed or exposed as a product endpoint.

## Still requiring hardware verification

- private-stack high-water mark and retained paragraph count;
- worst-case packet decode and 256-byte encode duration at 4.77 MHz;
- timer jitter during text/file/graphics operations;
- DOS version/critical-error behavior;
- packet-driver reentrancy and transmit completion;
- interaction with common memory managers and TSR stacks;
- BIOS ring variations and enhanced keyboard behavior.
