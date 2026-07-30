# Roadmap

## Completed: foreground end-to-end MVP

- [x] Distill contributor guidance into `AGENTS.md`.
- [x] Use Python 3.12 and the official MCP Python SDK 2.x.
- [x] Define the transport-independent backend and typed domain model.
- [x] Expose status, capabilities, text capture, and keyboard MCP tools.
- [x] Implement and test a bounded Linux PTY backend.
- [x] Freeze the version-1 authenticated datagram envelope and payloads.
- [x] Cross-check deterministic crypto/packet vectors in Python and C89.
- [x] Implement the UDP simulator and reliable UDP bridge backend.
- [x] Implement packet-driver discovery plus minimal ARP/IPv4/UDP.
- [x] Implement a register-safe 16-bit packet receive upcall.
- [x] Implement foreground DOS status and capability reporting.
- [x] Capture the active 80×25 text page with cursor and CP437 cells.
- [x] Inject printable and named BIOS key words with partial receipts.
- [x] Add a local Ctrl+Alt+Esc emergency stop.
- [x] Build `PROTOCHK.EXE` and `RAGENT.EXE` with 8086 generation.
- [x] Verify NE2000 load and protocol vectors in DOSBox-X.
- [x] Verify authenticated UDP, four-fragment VGA capture, remote `VER`,
  duplicate suppression, and output recapture end to end.
- [x] Provide a repeatable DOSBox-X harness.

This completes the safe foreground MVP in `PROJECT.md`: Codex can use MCP to
observe a DOS prompt, type a command via the BIOS queue, and capture the
result. PicoMEM code has not been introduced.

## Next generic milestone: resident observation

- Design a bounded request queue rather than the foreground single buffer.
- Select and verify `InDOS`, critical-error, and `INT 28h` deferral behavior.
- Move only status, text capture, and BIOS keyboard queueing into a TSR.
- Add local activation/disable UI and a persistent remote-control indicator.
- Measure resident paragraphs, callback cycles, 4 KB screen-copy time, packet
  loss, and MAC cost on a genuine 4.77 MHz 8088.
- Test DOS 3.x, 5.x, 6.x, FreeDOS, multiple packet drivers, CGA, and MDA.

## Then: generic agent shell services

- Direct, policy-aware execution with state and bounded output.
- Sandboxed directory listing, stat, and binary reads.
- Reliable upload/download transactions with temporary files and CRC32.
- DOS path/device-name validation, free-space checks, and atomic replacement.
- Explicit modern-host authorization and local approvals for writes.

## Later generic capabilities

- CGA and Hercules raw capture/rendering, then EGA and VGA.
- Wait-for-change, wait-for-stable, and wait-for-text on the bridge.
- Policy-controlled memory reads and diagnostics.
- Restricted memory writes and port I/O only after independent enforcement.
- Protocol cancellation, selective missing-fragment recovery, and fault
  injection for loss, reordering, corruption, and target reboot.

PicoMEM/PicoMEM2 research and implementation remain intentionally deferred.

## Unresolved assumptions requiring verification

### Real hardware

- Exact Open Watcom runtime footprint and instruction stream on an 8088.
- MAC, screen-copy, and packet-processing latency at 4.77 MHz.
- Packet-driver callback behavior and loss across representative adapters.
- BIOS keyboard queue sizes and enhanced-key behavior across BIOS families.
- EGA/VGA detection fallbacks on early or unusual BIOS implementations.
- Safe DOS deferral points and critical-error handling by DOS version.

### Protocol and security

- Whether a 1,024-byte application fragment performs well on slow adapters.
- Operational provisioning and rotation of a unique key per machine.
- Rate limiting and whether a wider authentication tag is affordable.
- Parser fuzzing and an independent audit of both implementations.

### Publication

- Final project license.
- Attribution/distribution policy for packet-driver binaries.
- CP437 font licensing when image rendering is added.
