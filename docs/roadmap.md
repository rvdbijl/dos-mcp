# Roadmap and verification status

## Completed

### Modern bridge and foreground MVP

- [x] Python 3.12, MCP SDK 2.x, typed backend/domain layers.
- [x] Linux PTY backend and Linux-backed UDP simulator.
- [x] Status, capabilities, text capture, and keyboard tools.
- [x] Foreground `RAGENT.EXE` packet-driver endpoint.
- [x] 8086 assembly receive upcall, ARP/IPv4/UDP, retries, replay handling.
- [x] Optional passphrase, raw 128-bit key, and conspicuous open mode.
- [x] Cross-language protocol vectors and foreground DOSBox-X test.
- [x] Bounded mTCP `MTCPCFG` reuse for `IPADDR` and `PACKETINT`.
- [x] Tracked hardware commissioning bundle and offline DOS self-tests.

### Protocol v2 and resident RA-TSR

- [x] Preserve RAGENT and add separately named `RA-TSR.EXE`.
- [x] Build all DOS code with 8086/8088 instruction generation.
- [x] Private resident stack and shared timer/DOS-idle nested-worker guard.
- [x] Load/query/unload with vector ownership verification and PSP release.
- [x] Cached DOS/video facts and no video-BIOS calls from resident capture.
- [x] Bounded streamed text VRAM capture.
- [x] BIOS ring keyboard insertion with partial receipts.
- [x] Opt-in sandboxed file download and temp-file upload commit with CRC32.
- [x] Raw standard CGA/Hercules/EGA/VGA graphics state machine.
- [x] Replace per-block XTEA MAC with pre-expanded native-16-bit Speck MAC.
- [x] Protocol-v2 odd-frame padding for word-wide NE2000 drivers.
- [x] Exact DOSBox-X 64 KiB VGA mode 13h capture.
- [x] DOSBox-X resident status/text/keys/file/unload integration.

### Discovery and multiple systems

- [x] User-visible 1–31 byte RA-TSR name.
- [x] Disconnected local Ethernet/IP broadcast with TTL 1.
- [x] Approximately five-second interval and 30-second session expiry.
- [x] Strict discovery codec with CRC and stable adapter ID.
- [x] Named static multi-target configuration.
- [x] Nonblocking Linux discovery listener and target registry.
- [x] `dos.list_targets` and optional selector on every target tool.
- [x] Explicit-target requirement when multiple machines are known.

PicoMEM/PicoMEM2 code remains intentionally unimplemented.

## Next: hardening and physical validation

- [ ] Measure load size, resident paragraphs, and private-stack high water.
- [ ] Measure handshake, MAC, 256-byte fragment, and graphics-block timing on a
  genuine 4.77 MHz 8088.
- [ ] Test DOS 3.x, 5.x, 6.x, FreeDOS, 286, 386, and 486 machines.
- [ ] Test representative Crynwr packet drivers/adapters under loss/load.
- [ ] Catalogue packet-driver behavior when an mTCP utility and endpoint
  request overlapping IPv4/ARP packet types.
- [ ] Validate `INT 28h` file dispatch and critical-error behavior by DOS
  version.
- [ ] Validate unload ordering with common memory managers and TSR stacks.
- [ ] Add distinct-plane fixtures for CGA, Hercules, EGA, and VGA planar modes.
- [ ] Fuzz protocol, discovery, path, and transfer codecs.
- [ ] Independent review of 16-bit arithmetic, far pointers, vector hooks, and
  packet-driver ABI.

## Next: modern-host usability

- [ ] Per-target credential provider/keyring integration.
- [ ] Discovery expiry/pinning and rate limiting.
- [ ] Optional rendered PNG output and captured palette/DAC state.
- [ ] Wait-for-change, wait-for-stable, and wait-for-text logical operations.
- [ ] Per-target operation locks so independent machines can run concurrently.
- [ ] Persistent structured audit logging with secret/screen policy.
- [ ] Better transfer progress/cancellation and selective block retry.

## Security gates before less-trusted networks

- [ ] Wider packet authentication tag or secure authenticated outer tunnel.
- [ ] Credential provisioning and rotation workflow.
- [ ] Rate limiting for handshake, discovery, and invalid packets.
- [ ] Multi-controller ownership/revocation rules.
- [ ] Security audit of DOS file device names/aliases.
- [ ] Clear firewall examples for physical, VM, and container deployments.

## Explicitly deferred or excluded

- Direct arbitrary execution as an MCP tool.
- Memory writes, port writes, interrupt calls, reboot, and sector writes.
- Windows/Windows 9x resident operation.
- Compatibility promises for protected-mode extenders or direct-hardware
  keyboard/video games.
- Stealth, autorun persistence, evasion, or hidden local indicators.
- PicoMEM/PicoMEM2 integration until explicitly requested.

## Unresolved assumptions

### Real hardware

- Open Watcom/runtime behavior matches DOSBox across target CPUs.
- The 32 KiB private stack is sufficient and the retained 128 KiB block can be
  safely reduced.
- Packet-driver transmit downcalls are safe from the selected deferred context
  on representative drivers.
- BIOS Data Area offsets and `INT 28h`/critical-error conventions match
  supported DOS families.
- Early EGA/VGA/Hercules clones implement the ports/memory maps used.

### Protocol/security

- A 32-bit tag is acceptable only on the documented private network.
- One shared bridge credential is insufficient for ideal multi-target
  deployments and must be replaced with per-target secret resolution.
- CRC32 protects transfer integrity but is not authentication independent of
  the packet MAC.
- Discovery adapter IDs can collide or be spoofed and must remain advisory.

### Publication

- Final project license.
- Packet-driver acquisition/redistribution and attribution.
- CP437 font/palette assets if rendering ships.
