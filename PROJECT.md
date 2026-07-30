# Retro DOS MCP Agent

## Project brief

### Working title

**Retro DOS Agent**

Alternative possible names:

* DOS MCP Agent
* XT Agent
* RetroMCP
* DOS Remote Agent
* MCP-XT
* Retro Control Bridge

The final name has not yet been selected.

---

## 1. Project objective

Build an open-source system that allows an AI coding agent such as Codex to inspect, control, diagnose, and interact with a real DOS computer, including very limited systems such as:

* IBM PC and PC/XT-class machines
* Intel 8088 at approximately 4.77 MHz
* 640 KB conventional memory
* MS-DOS or compatible DOS
* Packet-driver-compatible Ethernet adapters
* Optional PicoMEM or PicoMEM2 hardware

The actual AI model and MCP implementation will run on a modern computer.

The DOS machine will run a compact privileged agent that exposes machine capabilities through a small binary protocol.

The design must support two primary communication paths:

1. A generic DOS agent using an Ethernet packet driver and UDP.
2. A PicoMEM/PicoMEM2-enhanced path using firmware on the board and a small ISA mailbox or shared-memory interface.

Both paths should expose the same logical operations to the modern proxy.

The modern proxy will act as the actual MCP server. From Codex's perspective, the DOS computer should appear to be an MCP-managed machine with tools for screen capture, keyboard input, files, commands, memory, and hardware access.

---

## 2. Core architectural principle

Do not attempt to run Codex, an LLM, HTTP, TLS, MCP JSON-RPC, or other heavyweight modern protocols on the DOS computer.

The DOS-side software must remain small and deterministic.

The modern system will handle:

* MCP
* JSON-RPC
* model communication
* authentication policy
* conversation context
* image conversion
* graphics reconstruction
* compression when appropriate
* UDP reliability
* retries
* file-transfer transactions
* security approvals
* logging
* tool schemas
* screen interpretation
* optional OCR or vision processing

The DOS system will handle only operations that must occur on the physical machine.

---

## 3. High-level architecture

```text
┌────────────────────────────────────────────┐
│ Codex or another MCP host                  │
│                                            │
│ Uses MCP tools to operate the DOS system   │
└─────────────────────┬──────────────────────┘
                      │
                      │ MCP over stdio or HTTP
                      ▼
┌────────────────────────────────────────────┐
│ Modern MCP bridge                          │
│                                            │
│ • Implements the MCP server                │
│ • Maintains machine state                  │
│ • Converts MCP calls to agent operations   │
│ • Reconstructs text and graphics screens   │
│ • Handles UDP reliability                  │
│ • Enforces security policy                 │
│ • Stores logs and transfer state           │
└─────────────────────┬──────────────────────┘
                      │
                      │ Compact binary protocol
                      │ normally over UDP
                      ▼
┌────────────────────────────────────────────┐
│ DOS machine                                │
│                                            │
│ Generic DOS agent or PicoMEM helper        │
│                                            │
│ • Captures video state and framebuffer     │
│ • Injects BIOS-level keystrokes            │
│ • Reads and writes files                   │
│ • Executes DOS commands safely             │
│ • Reads and writes memory                  │
│ • Performs I/O port operations             │
│ • Reports machine capabilities             │
└────────────────────────────────────────────┘
```

---

## 4. Two transport implementations

### 4.1 Generic packet-driver path

This path must work on a normal DOS PC with a packet-driver-supported network adapter.

```text
MCP bridge
    │
    │ UDP
    ▼
DOS packet driver
    │
    ▼
DOS resident agent and companion shell
```

Likely DOS-side components:

* `RAGENT.COM` or similar resident agent
* `RASHELL.EXE` or similar agent-aware shell
* Packet-driver communication module
* UDP reliability module
* Screen capture module
* Keyboard injection module
* Filesystem service module
* Hardware access module

The generic implementation should avoid requiring mTCP applications to be active. It may use mTCP code, concepts, or packet-driver conventions where licensing and architecture permit, but it should ideally be independently usable.

UDP is preferred over TCP because:

* The protocol can remain compact.
* The bridge can handle retries.
* Screen updates tolerate some packet loss.
* The DOS client does not need a large TCP state machine.
* Transactions can have operation-specific reliability semantics.

The application protocol must nevertheless provide reliability for operations such as:

* File upload
* File download
* Memory dumps
* Command execution
* Configuration changes

---

### 4.2 PicoMEM and PicoMEM2 path

For machines equipped with PicoMEM or PicoMEM2, move as much work as practical to the board's microcontroller.

Expected architecture:

```text
MCP bridge
    │
    │ Wi-Fi or Ethernet UDP
    ▼
PicoMEM firmware
    │
    │ Shared ISA memory, mailbox registers and IRQ
    ▼
Small DOS helper
```

Potential PicoMEM responsibilities:

* UDP networking
* Authentication
* Packet sequencing
* Fragmentation and reassembly
* Retransmission
* CRC verification
* File staging
* SD or USB storage staging
* Large transfer buffers
* Screen compression
* CP437 conversion
* Screen history and change detection
* Keyboard queues
* Command queues
* ISA shared-memory window
* Optional IRQ generation
* Optional DMA support
* Boot-time presence
* Last-known-screen storage
* Recovery image selection

The DOS helper should perform only operations requiring the host CPU or DOS context, such as:

* BIOS keyboard-buffer insertion
* DOS filesystem calls
* Program execution
* CPU `IN` and `OUT` instructions
* Copying conventional memory
* Copying video memory
* Reading BIOS and DOS state
* Safe command execution

The PicoMEM route should preferably work as an out-of-band management path, independent of the DOS packet driver and ordinary DOS networking.

The machine may continue to use PicoMEM's normal emulated network adapter separately.

---

## 5. Transport-independent logical agent API

The project must define a logical agent API before binding operations to either UDP transport.

Initial capability categories:

```text
Machine
Screen
Keyboard
Filesystem
Execution
Memory
I/O ports
Interrupts
Storage
Session control
Security and approval
```

Proposed operations follow.

### Machine

```text
machine.get_status
machine.get_capabilities
machine.get_identity
machine.get_dos_version
machine.get_memory_map
machine.get_environment
machine.get_time
machine.reboot
machine.cancel_operation
```

### Screen

```text
screen.get_video_state
screen.capture
screen.capture_text
screen.capture_graphics
screen.wait_for_change
screen.wait_for_stable
screen.wait_for_text
```

### Keyboard

```text
keyboard.send_text
keyboard.send_keys
keyboard.send_bios_keys
keyboard.clear_queue
keyboard.get_queue_status
```

### Filesystem

```text
fs.list
fs.stat
fs.read
fs.write
fs.upload
fs.download
fs.rename
fs.copy
fs.mkdir
fs.delete
fs.get_drives
fs.get_free_space
fs.calculate_crc
```

### Execution

```text
exec.get_state
exec.run
exec.queue
exec.cancel
exec.get_result
exec.enter_agent_shell
```

### Memory

```text
memory.read
memory.write
memory.calculate_crc
memory.get_interrupt_vector
memory.set_interrupt_vector
```

### I/O ports

```text
io.read_port
io.write_port
io.read_range
```

### Interrupts

Possible advanced functionality:

```text
interrupt.call_bios
interrupt.call_dos
```

Calling arbitrary interrupts remotely is dangerous and should not be part of the first implementation.

### Storage and recovery

```text
storage.list_images
storage.mount_image
storage.unmount_image
storage.select_boot_image
storage.stage_file
```

These operations may be available only with PicoMEM.

---

## 6. MCP representation

The modern bridge is the actual MCP server.

The DOS computer itself should not parse MCP or JSON.

The MCP server may initially run using stdio:

```text
Codex launches retro-dos-mcp
                       │
                       └── UDP ── DOS computer
```

Example invocation:

```bash
retro-dos-mcp --target 192.168.10.55 --config machines/ibm-xt.toml
```

Later, a persistent Streamable HTTP MCP server may be added.

The MCP server should dynamically advertise tools according to the machine's capabilities.

For example, an ordinary Ethernet-connected XT may advertise:

```text
dos.capture_screen
dos.send_keys
dos.read_file
dos.write_file
dos.execute
dos.read_memory
dos.write_memory
dos.read_port
dos.write_port
```

A PicoMEM-equipped XT may additionally advertise:

```text
dos.stage_file
dos.select_boot_image
dos.get_last_screen
dos.get_boot_state
dos.mount_remote_image
```

The MCP server should expose high-level tools by default.

Raw memory and port operations should be categorized as advanced or dangerous.

---

## 7. Screen capture requirements

Screen capture is a primary project capability.

The project must support text and graphics modes.

### 7.1 Text modes

Capture:

* BIOS video mode
* Adapter type when known
* Active video page
* Number of columns
* Number of rows
* Cursor position
* Cursor shape
* Character bytes
* Attribute bytes
* Relevant font or code-page information
* Blink/intensity state where possible

Typical memory locations include:

```text
B800:0000 — color text memory
B000:0000 — monochrome text memory
```

Do not assume a fixed address without identifying the active adapter and mode.

A normal 80×25 text screen is approximately 4,000 bytes when retaining character and attribute bytes.

The modern bridge should provide two representations:

1. Structured text with rows, cursor and attributes.
2. A rendered image using CP437 glyphs and DOS display attributes.

The DOS side should not render PNG files.

### 7.2 CGA graphics

Support at least:

* 320×200 four-color modes
* 640×200 monochrome mode

Capture:

* Raw CGA video memory
* BIOS mode
* Relevant CGA mode and color registers
* Palette/intensity state

The bridge reconstructs CGA's interleaved scanline layout.

### 7.3 Hercules graphics

Support:

* Hercules 720×348 monochrome graphics
* Hercules interleaved memory layout
* Relevant mode and control registers

A raw Hercules capture may be approximately 32 KB.

### 7.4 EGA and VGA

These are later milestones.

Plan for:

* Planar memory
* Multiple planes
* Graphics controller registers
* Sequencer registers
* CRTC state
* attribute controller state
* palette state
* VGA DAC palette
* packed and planar graphics modes

Capturing EGA or VGA planes may require selecting and reading planes one by one.

All modified registers must be saved and restored accurately.

### 7.5 Capture strategy

Initial captures should be request-driven, not continuous video streaming.

Possible triggers:

* Explicit capture request
* After a keyboard sequence
* After command completion
* After screen-change notification
* After a stable period
* Periodic low-frequency capture
* BIOS video-call hook

The design should eventually support:

```text
Wait until the screen changes.
Wait until the screen remains stable for N milliseconds.
Wait until specified text appears.
```

Text matching should happen on the modern bridge whenever possible.

---

## 8. Keyboard injection

Initial keyboard injection should use the BIOS keyboard queue.

Requirements:

* Inject BIOS scan-code/ASCII pairs.
* Support printable characters.
* Support Enter, Escape, Tab and Backspace.
* Support arrows and navigation keys.
* Support function keys.
* Support Ctrl, Alt and Shift combinations.
* Support configurable inter-key delays.
* Avoid overrunning the BIOS keyboard buffer.
* Report queue status.
* Allow cancellation.
* Provide an emergency local disable hotkey.

The primary target is software that reads input through BIOS `INT 16h` or DOS console services.

Software that directly reads the keyboard controller may not respond to BIOS-buffer injection. Hardware-level keyboard emulation may be considered later, particularly for PicoMEM-equipped machines, but it is not required for the first release.

---

## 9. DOS execution model

DOS is not generally reentrant.

The resident agent must never issue arbitrary DOS calls directly from a packet-driver callback, hardware interrupt, or unsafe context.

Incoming operations should be queued and deferred.

Possible mechanisms include:

* DOS `InDOS` flag checks
* Critical-error-state checks
* `INT 28h` DOS idle processing
* A foreground companion shell
* A device driver or redirector architecture
* A polling loop at the DOS prompt

Two operation modes should exist.

### 9.1 Observe/control mode

Available while arbitrary DOS applications run:

* Screen capture
* Keyboard injection
* Status queries
* Selected memory reads
* Carefully controlled memory writes
* Selected port reads
* Carefully controlled port writes
* Request queuing
* Last-known-state reporting

### 9.2 Agent shell mode

A foreground shell or command wrapper should provide safe:

* Command execution
* File operations
* Output capture
* Current-directory management
* Child-process tracking
* Exit-status reporting when possible
* Command cancellation where possible
* Return-to-agent-shell detection

Possible invocation:

```dos
C:\>RASH
Retro Agent Shell
C:\>
```

The agent shell could eventually be installed as the DOS command interpreter, but this should not be required initially.

---

## 10. File transfer

The project must support reliable binary file upload and download.

Requirements:

* Binary-safe transfer
* Fragmentation
* Block numbering
* Missing-block detection
* Retransmission
* CRC32 or another suitable integrity check
* Temporary-file creation
* Final atomic rename where possible
* Transfer cancellation
* Resume support as a later feature
* DOS path and filename validation
* DOS free-space checks
* FAT filesystem limitations
* DOS date/time handling
* Protection against path traversal
* Optional read-only mode
* Optional sandbox directory

Recommended upload flow:

1. Validate target path.
2. Check available disk space.
3. Create a temporary file.
4. Transfer numbered blocks.
5. Request retransmission of missing blocks.
6. Calculate the DOS-side checksum.
7. Compare it with the bridge checksum.
8. Rename the temporary file to its final name.
9. Return final path, byte count and checksum.

For PicoMEM:

1. Upload the complete file to PicoMEM staging storage.
2. Verify it on the board.
3. Notify the DOS helper.
4. Transfer locally from shared storage into a temporary DOS file.
5. Verify the DOS-side file.
6. Rename it.

---

## 11. Memory access

Support physical-address and segment:offset access.

Examples:

```text
segment = B800h
offset  = 0000h
length  = 4000
```

and:

```text
physical_address = 000B8000h
length = 4000
```

Address arithmetic must account for the 20-bit 8088 address space and possible A20 behavior on later systems.

Memory reads should support chunking.

Memory writes must have strict policy controls.

Suggested initial memory policy:

```text
00000h–003FFh   Interrupt vector table — protected
00400h–004FFh   BIOS Data Area — restricted
00500h–9FFFFh   Conventional RAM — readable
A0000h–BFFFFh   Video memory — readable and controlled writes
C0000h–EFFFFh   Adapter ROM and upper-memory area — restricted
F0000h–FFFFFh   System BIOS — read-only
```

Actual policies must account for machine-specific memory maps.

PicoMEM-owned memory, shared windows, EMS memory and emulated memory may have separate policies.

---

## 12. I/O port access

The remote agent should eventually permit controlled CPU-level `IN` and `OUT` instructions.

Support widths:

* 8-bit
* 16-bit where supported and appropriate

On an 8088, 16-bit port operations require careful handling and may map to multiple bus cycles.

Port access is dangerous.

Default policy:

* Port reads: allowlisted ranges only.
* Port writes: denied unless specifically enabled.
* Storage-controller writes: denied by default.
* DMA-controller writes: denied by default.
* PIC writes: denied by default.
* System-control writes: denied by default.

The MCP server, transport and DOS agent should all enforce security independently.

Potential commonly useful ranges include:

```text
20h–21h       Programmable interrupt controller
40h–43h       Programmable interval timer
60h–64h       Keyboard/controller area
200h–207h     Joystick
278h–27Fh     LPT2
2F8h–2FFh     COM2
378h–37Fh     LPT1
3B0h–3DFh     Video hardware
3F8h–3FFh     COM1
```

This list is illustrative and must not be treated as a blanket safe allowlist.

---

## 13. Generic UDP protocol goals

The binary protocol should be:

* Compact
* Versioned
* Extensible
* Easy to parse on an 8088
* Independent of MCP
* Independent of JSON
* Independent of CPU endianness where practical
* Resilient to duplication
* Resilient to packet loss
* Capable of fragmentation
* Capable of authentication
* Capable of transaction cancellation
* Suitable for both packet-driver and PicoMEM transports

Potential packet header:

```text
magic                 2 or 4 bytes
protocol_version      1 byte
message_type          1 byte
flags                 1 or 2 bytes
session_id            2 or 4 bytes
request_id            2 or 4 bytes
fragment_number       2 bytes
fragment_count        2 bytes
payload_length        2 bytes
header_checksum       optional
payload_crc           optional
```

The exact structure must be designed after considering 8088 parsing cost, alignment, maximum UDP payload and future extensions.

Do not assume that every message needs every field.

Possible message classes:

```text
HELLO
HELLO_ACK
CAPABILITIES_REQUEST
CAPABILITIES_RESPONSE
REQUEST
RESPONSE
DATA
ACK
NACK
CANCEL
ERROR
EVENT
PING
PONG
```

Potential operation payloads:

```text
GET_STATUS
CAPTURE_SCREEN
SEND_KEYS
READ_FILE
WRITE_FILE
LIST_DIRECTORY
EXECUTE
READ_MEMORY
WRITE_MEMORY
READ_PORT
WRITE_PORT
REBOOT
```

The bridge should own transaction complexity.

One MCP file-upload tool call may generate hundreds of UDP datagrams, acknowledgements and retries. Codex should receive only the final operation result.

---

## 14. Authentication and session security

The protocol must never be designed for unauthenticated Internet exposure.

Initial deployment assumption:

* Trusted local network
* Directly configured target IP
* Shared secret
* No public port forwarding

Possible protocol security:

* Pre-shared key
* Session challenge/response
* Message authentication code
* Monotonic request counter
* Random session nonce
* Replay protection

Full encryption may be too expensive for the first generic 8088 client.

A PicoMEM implementation may be able to support stronger cryptography on its microcontroller.

The security design should separate:

* Authentication
* Integrity
* Replay protection
* Confidentiality

At minimum, commands must not be accepted solely because they originate from a specific IP or MAC address.

---

## 15. Permission model

Suggested operation levels:

```text
Level 0 — Observe only
Level 1 — Screen and status
Level 2 — Keyboard input
Level 3 — Filesystem read
Level 4 — Sandboxed filesystem write
Level 5 — Approved command execution
Level 6 — Memory and hardware diagnostics
Level 7 — Unrestricted laboratory mode
```

Dangerous operations should require explicit configuration or local approval.

Examples requiring approval:

* Delete files
* Replace `CONFIG.SYS`
* Replace `AUTOEXEC.BAT`
* Write interrupt vectors
* Write arbitrary memory
* Write arbitrary I/O ports
* Reboot
* Mount writable disk images
* Change boot images
* Direct disk-sector writes
* Format or partition operations

Possible physical approval UI:

```text
REMOTE AGENT REQUEST

Write C:\AUTOEXEC.BAT
Size: 783 bytes

F10 = Approve
Esc = Reject
```

The agent should visibly indicate when remote control is active.

There must be an emergency local kill switch or hotkey.

---

## 16. Capability discovery

The DOS or PicoMEM agent must report capabilities and constraints.

Example logical response:

```text
Protocol version:          1
Agent version:             0.1
Transport:                 packet-driver UDP
CPU class:                 8088
DOS version:               6.22
Conventional memory:       640 KB
Video adapter:             CGA
Current video mode:        3
Packet-driver interrupt:   60h
Maximum UDP payload:       1024
Text capture:              yes
Graphics capture:          CGA
Keyboard injection:        BIOS queue
File access:               yes
Command execution:         agent-shell only
Memory read:               yes
Memory write:              restricted
Port read:                 allowlist
Port write:                disabled
EMS:                       no
XMS:                       no
PicoMEM:                   no
```

The MCP bridge should use these capabilities to determine which tools and options to expose.

---

## 17. PicoMEM ISA mailbox concept

For PicoMEM-equipped machines, investigate implementing a custom virtual ISA device.

Possible components:

* Small I/O register block
* Shared-memory window
* Request and response descriptors
* Keyboard queue
* Event queue
* Bulk-transfer buffer
* Optional IRQ
* Optional DMA

Illustrative register design:

```text
BASE+0   Status
BASE+1   Command
BASE+2   Flags
BASE+3   Doorbell/acknowledge
BASE+4   Request ID low
BASE+5   Request ID high
BASE+6   Length low
BASE+7   Length high
```

The precise base port must be configurable to avoid conflicts.

Illustrative memory window:

```text
Offset 0000h   Control structure
Offset 0100h   Request descriptors
Offset 0200h   Response descriptors
Offset 0400h   Keyboard queue
Offset 0800h   Event queue
Offset 1000h   Screen/file bulk transfer
```

The design must examine existing PicoMEM firmware architecture rather than assuming arbitrary ISA bus-master abilities.

Important distinction:

PicoMEM may emulate devices and memory, but this does not automatically mean it can autonomously initiate arbitrary ISA memory or port transactions as a bus master.

Operations involving ordinary motherboard RAM or unrelated I/O ports may still require the 8088 DOS helper.

---

## 18. PicoMEM display possibilities

Initial PicoMEM screen capture should remain CPU-assisted.

Flow:

1. PicoMEM receives a screen-capture request over Wi-Fi.
2. It places a request in the shared mailbox.
3. The DOS helper captures video metadata.
4. The DOS helper copies video memory into shared PicoMEM memory.
5. PicoMEM compresses or transmits the data.
6. The modern bridge reconstructs the display.

Possible later optimization:

* Passively observe CGA or Hercules video-memory writes on the ISA bus.
* Maintain a shadow framebuffer in PicoMEM memory.
* Track relevant video-controller registers.
* Retain the last known screen after a host crash.

Passive snooping is exploratory and must not be assumed to work until the PicoMEM/PicoMEM2 hardware and PIO firmware paths are reviewed.

CGA and Hercules are more plausible initial targets than planar EGA or VGA.

---

## 19. PicoMEM boot-time capabilities

PicoMEM may allow the project to become a primitive retro-computer management controller.

Potential pre-DOS features:

* Network presence before DOS loads
* POST-state reporting
* Boot progress reporting
* Last-known video state
* Boot-time key injection
* Recovery disk selection
* Mounting a diagnostic image
* Selecting a boot image
* Agent option-ROM integration
* Reporting whether the DOS helper loaded successfully

Possible machine phases:

```text
POWERED_OFF_OR_UNREACHABLE
PICOMEM_ONLINE
OPTION_ROM_INITIALIZED
BOOTING
DOS_DRIVER_LOADED
TSR_OBSERVE_MODE
AGENT_SHELL_READY
CHILD_PROCESS_RUNNING
DOS_BUSY
AWAITING_APPROVAL
HOST_UNRESPONSIVE
```

A true reset-control feature may require separate hardware or wiring and should not be assumed.

---

## 20. Language and toolchain considerations

### Modern MCP bridge

Possible implementation languages:

* Rust
* Python
* Go

Initial preference should be based on:

* MCP library maturity
* Binary protocol support
* Image reconstruction libraries
* Testability
* Distribution
* Cross-platform support
* Ease of debugging

Python may be fastest for initial protocol and MCP experimentation.

Rust may be attractive for a robust distributable bridge.

Do not select a language without documenting the tradeoff.

### DOS client

Potential toolchains:

* Open Watcom C/C++
* Turbo C
* Microsoft C
* Assembly where needed
* NASM or MASM-compatible assembler

Likely approach:

* Mostly C
* Small assembly modules for interrupt hooks, port access, optimized copies and register-sensitive operations
* Real-mode 16-bit memory models
* No assumption of 386 instructions
* 8088-compatible code generation

The first supported CPU must be a genuine 8088.

Optimizations must not silently introduce 80186, 286 or 386 instructions.

### PicoMEM firmware

The PicoMEM implementation must integrate cleanly with the upstream project's firmware architecture.

Avoid creating a permanent unmaintainable fork if an extension mechanism or upstream contribution is feasible.

Before coding:

* Inspect PicoMEM and PicoMEM2 repository structure.
* Identify firmware license.
* Identify networking stack.
* Identify PIO ISA handlers.
* Identify memory-emulation paths.
* Identify command interface between host and Pico.
* Identify available shared memory.
* Identify IRQ allocation.
* Identify DMA functionality and limitations.
* Identify available GPIO and reset possibilities.
* Document upstream compatibility strategy.

---

## 21. Emulator and test support

The project should not require physical XT hardware for every development cycle.

Potential test targets:

* DOSBox-X
* 86Box
* PCem
* QEMU where applicable
* Real IBM PC/XT hardware
* Real compatible 8088 systems
* PicoMEM-equipped machines

Build a host-side simulator that implements the DOS-agent UDP protocol.

The simulator should be able to:

* Return synthetic text screens
* Return CGA framebuffer samples
* Accept key injection
* Simulate packet loss
* Simulate duplicate packets
* Simulate latency
* Simulate file transfers
* Simulate DOS busy state
* Simulate capability differences
* Simulate command execution
* Simulate a target reboot

This allows the MCP bridge and protocol to be developed before the full DOS TSR exists.

A packet-capture decoder for Wireshark would be useful later.

---

## 22. Initial repository layout

Suggested structure:

```text
retro-dos-agent/
│
├── README.md
├── LICENSE
├── CONTRIBUTING.md
├── SECURITY.md
├── AGENTS.md
├── PROJECT.md
│
├── docs/
│   ├── architecture.md
│   ├── protocol.md
│   ├── security-model.md
│   ├── dos-reentrancy.md
│   ├── video-capture.md
│   ├── picomem-integration.md
│   ├── hardware-support.md
│   └── roadmap.md
│
├── protocol/
│   ├── specification.md
│   ├── messages.md
│   ├── capabilities.md
│   ├── test-vectors/
│   └── schemas/
│
├── bridge/
│   ├── src/
│   ├── tests/
│   └── README.md
│
├── dos/
│   ├── agent/
│   ├── shell/
│   ├── packet/
│   ├── video/
│   ├── keyboard/
│   ├── filesystem/
│   ├── hardware/
│   ├── include/
│   ├── tests/
│   └── README.md
│
├── picomem/
│   ├── firmware/
│   ├── dos-helper/
│   ├── mailbox/
│   └── README.md
│
├── simulator/
│   ├── src/
│   ├── fixtures/
│   └── README.md
│
├── tools/
│   ├── protocol-dump/
│   ├── screen-renderer/
│   └── test-client/
│
└── examples/
    ├── machine-configs/
    ├── screen-captures/
    └── mcp-configs/
```

This layout is a starting proposal, not a fixed requirement.

---

## 23. Recommended implementation phases

### Phase 0 — Research and architecture

Deliverables:

* Repository structure
* Architecture document
* Threat model
* Protocol requirements
* PicoMEM feasibility notes
* DOS memory-budget estimate
* DOS reentrancy notes
* Video-mode support matrix
* Toolchain recommendation
* Licensing review

No TSR implementation yet.

### Phase 1 — Host simulator and bridge skeleton

Build:

* Agent protocol library
* Simulated DOS target
* UDP request/response
* Basic capability discovery
* Status request
* Text-screen response
* Basic MCP server
* `dos.get_status`
* `dos.capture_screen`
* `dos.send_keys`

This validates the model-facing architecture without real DOS hardware.

### Phase 2 — Foreground DOS prototype

Build a normal foreground DOS program, not a TSR.

Capabilities:

* Packet-driver initialization
* UDP send and receive
* Capability response
* Current text-screen capture
* Receive and display messages
* BIOS keyboard injection
* Basic diagnostics

This reduces early complexity.

### Phase 3 — Generic TSR observation agent

Move suitable functions into a TSR:

* Packet receive queue
* Deferred request processing
* Text-screen capture
* Keyboard queue
* Status reporting
* Local activation and disable hotkey

Do not add arbitrary DOS command execution from interrupt context.

### Phase 4 — Agent shell

Build a foreground companion shell:

* Safe command execution
* Output redirection and return
* File operations
* Upload and download
* Command-state reporting
* Child-process state
* Exit handling

### Phase 5 — Graphics capture

Implement:

1. CGA
2. MDA/Hercules
3. EGA
4. VGA

Create bridge-side renderers and fixture tests.

### Phase 6 — Memory and hardware operations

Add:

* Memory reads
* Restricted memory writes
* Port reads
* Restricted port writes
* BIOS Data Area inspection
* Interrupt-vector inspection
* Detailed security policy

### Phase 7 — PicoMEM prototype

Build:

* Firmware UDP service
* Shared mailbox
* DOS helper
* Status and capability discovery
* Text-screen capture
* Keyboard queue
* File staging

### Phase 8 — PicoMEM advanced support

Investigate:

* DMA
* Boot-time option-ROM hooks
* Passive screen snooping
* Last-known screen
* Recovery image selection
* Reset-control hardware
* Additional shared memory
* EMS integration
* Upstream PicoMEM collaboration

---

## 24. First minimum viable product

The first useful MVP should do only the following:

1. Run a foreground agent on an XT or DOS emulator.
2. Communicate with a modern bridge using UDP.
3. Return machine status and capabilities.
4. Capture an 80×25 text screen.
5. Render that screen on the modern side.
6. Receive and inject BIOS-level keystrokes.
7. Allow Codex to use MCP tools for capture and keyboard input.
8. Avoid all command execution, file writes and raw hardware writes initially.

Demonstration goal:

```text
Codex calls dos.capture_screen.
The bridge returns the current DOS prompt.
Codex calls dos.send_keys with "DIR" and Enter.
The XT receives the BIOS keystrokes.
Codex waits for the screen to settle.
Codex captures the resulting directory listing.
```

That proves the complete end-to-end design with minimal danger.

---

## 25. Initial engineering questions

Research and document these before committing to implementation details:

1. Which packet-driver API strategy has the smallest resident memory cost?
2. Should the generic DOS agent implement minimal ARP/IP/UDP itself?
3. Can existing open-source DOS networking code be reused under compatible licensing?
4. What is the safest deferred-execution strategy for the TSR?
5. Should the foreground shell wrap `COMMAND.COM` or act as a command interpreter?
6. How should command output be captured?
7. Can command completion be detected reliably?
8. What maximum UDP payload performs well on XT-era Ethernet hardware?
9. Which checksum is efficient enough on an 8088?
10. What authentication mechanism is practical on an 8088?
11. What exact PicoMEM/PicoMEM2 firmware interfaces are available?
12. Can PicoMEM maintain a private shared-memory window without conflicts?
13. Can PicoMEM use an available IRQ safely?
14. What DMA functionality is actually supported?
15. Can PicoMEM firmware observe unrelated ISA memory writes?
16. How expensive is a 16 KB or 32 KB screen copy on a 4.77 MHz 8088?
17. How should CGA composite modes be represented?
18. How should custom text fonts be captured or represented?
19. How should screen state be synchronized after packet loss?
20. What operations must always require local physical approval?

Do not guess answers that require hardware or source-code inspection.

---

## 26. Coding principles

* Preserve compatibility with a genuine 4.77 MHz 8088.
* Keep the DOS resident footprint small.
* Avoid unnecessary dynamic allocation.
* Do not call DOS from unsafe interrupt contexts.
* Keep packet callbacks extremely short.
* Preallocate packet and request queues.
* Treat all remote input as untrusted.
* Validate all lengths and offsets.
* Defend against integer overflow.
* Defend against path traversal.
* Avoid fixed video assumptions.
* Restore hardware registers after inspection.
* Make dangerous capabilities opt-in.
* Maintain clean separation between protocol, transport and operations.
* Keep the MCP bridge independent of the DOS transport.
* Create deterministic protocol test vectors.
* Prefer documented behavior over clever undocumented dependencies.
* Design for real hardware first, then optimize for emulators.
* Record hardware-specific quirks rather than hiding them.

---

## 27. Licensing and publication goals

The project is intended for publication on GitHub as open-source software.

Before selecting a license:

* Review licenses of any mTCP-derived code.
* Review PicoMEM and PicoMEM2 licenses.
* Review MCP SDK licenses.
* Review graphics-font and CP437 assets.
* Review any DOS toolchain runtime requirements.
* Avoid incorporating incompatible GPL code into components intended for permissive licensing unless the project deliberately selects a compatible license.

Potential project license candidates:

* MIT
* BSD-2-Clause
* BSD-3-Clause
* Apache-2.0
* GPLv3

No final license has been selected.

Each component may need separate attribution or licensing considerations.

---

## 28. Expected first Codex task

Codex should begin by reading this project brief completely.

Do not immediately generate a large codebase.

First produce:

1. `docs/architecture.md`
2. `docs/protocol.md`
3. `docs/security-model.md`
4. `docs/dos-reentrancy.md`
5. `docs/video-capture.md`
6. `docs/picomem-integration.md`
7. `docs/roadmap.md`
8. `AGENTS.md`
9. A recommended initial language and toolchain decision
10. A list of unresolved assumptions requiring source-code or hardware verification

Then implement a small modern-side simulator and protocol test harness.

The first coding milestone should be:

```text
MCP bridge
    ↕ UDP
simulated DOS target
```

The simulator must return:

* Machine capabilities
* A synthetic 80×25 CP437 screen
* Cursor state
* Acknowledgement of injected keystrokes

Only after this architecture is reviewed should implementation of the real DOS client begin.

---

## 29. Project success criteria

The project is successful when Codex can connect through MCP to a real 8088 DOS computer and safely perform a workflow such as:

1. Query machine state.
2. Capture and understand the current text or graphics screen.
3. Inject BIOS-compatible keyboard input.
4. Wait for the machine to respond.
5. Upload a diagnostic program.
6. Execute it through a safe DOS agent shell.
7. Retrieve its output and generated files.
8. Inspect relevant memory or I/O ports when authorized.
9. Help diagnose or configure real retro hardware.
10. Perform the same logical workflow through either the generic packet-driver transport or PicoMEM transport.

The central design objective is:

> Make a real DOS computer appear to a modern AI agent as a small, capability-discoverable, remotely operated machine without burdening the DOS computer with modern AI or MCP protocol complexity.
