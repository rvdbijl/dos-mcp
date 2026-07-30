# Documentation

DOS MCP has three runnable target paths:

1. a local Linux PTY backend for MCP development;
2. a Linux-backed UDP simulator for transport testing;
3. the `RAGENT.EXE` foreground DOS endpoint using a packet driver.

The four MCP tools and their logical behavior are identical across these
paths. Capabilities and platform identity tell a client which target it is
actually controlling.

## Start here

| Goal | Document |
|---|---|
| Run the bridge for the first time | [Getting started](getting-started.md) |
| Configure a backend or simulator | [Configuration reference](configuration.md) |
| Understand MCP inputs and results | [MCP tools](mcp-tools.md) |
| Build and run the DOS executable | [Foreground DOS agent](../dos/README.md) |
| Reproduce the emulator test | [Testing guide](testing.md) |
| Diagnose a failure | [Troubleshooting](troubleshooting.md) |

## Design and implementation

| Topic | Document |
|---|---|
| Component boundaries and language choices | [Architecture](architecture.md) |
| Exact authenticated UDP wire format | [Protocol version 1](protocol.md) |
| DOS callback and foreground-context rules | [DOS reentrancy](dos-reentrancy.md) |
| Text-screen representation and future graphics | [Video capture](video-capture.md) |
| Linux-backed UDP target | [Simulator](simulator.md) |
| Supported and unverified platforms | [Hardware support](hardware-support.md) |
| Repository layout and change workflow | [Development guide](development.md) |

## Safety and project direction

| Topic | Document |
|---|---|
| Trust boundaries and implemented controls | [Security model](security-model.md) |
| Key handling and operating checklist | [Operations guide](operations.md) |
| Vulnerability reporting | [Security policy](../SECURITY.md) |
| Contribution expectations | [Contributing](../CONTRIBUTING.md) |
| Completed work and deferred milestones | [Roadmap](roadmap.md) |
| Original complete project brief | [Project brief](../PROJECT.md) |

## Current implementation boundary

The completed foreground MVP supports:

- target status and capability discovery;
- request-driven 80×25 text capture with CP437 cells and attributes;
- BIOS-compatible text and named-key injection;
- authenticated, replay-resistant UDP request/response;
- duplicate-safe keyboard retries;
- a Linux simulator and a DOSBox-X/NE2000 end-to-end test.

It does not yet provide a TSR, continuous operation while arbitrary DOS
applications run, graphics capture, direct file transfer, a direct execution
tool, raw memory access, port I/O, reboot, or PicoMEM integration. The bridge
reports these capabilities as unavailable rather than pretending they exist.
