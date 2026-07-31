# Documentation

DOS MCP has four runnable target paths:

1. a local Linux PTY backend for MCP development;
2. a Linux-backed UDP simulator for transport testing;
3. the `RAGENT.EXE` foreground DOS endpoint using a packet driver;
4. the loadable/unloadable `RA-TSR.EXE` resident DOS endpoint.

The MCP tools share transport-independent behavior. Capabilities and platform
identity tell a client what a selected target actually supports.

## Start here

| Goal | Document |
|---|---|
| Run the bridge for the first time | [Getting started](getting-started.md) |
| Commission a hardware DOS PC | [Hardware commissioning](hardware-commissioning.md) |
| Configure a backend or simulator | [Configuration reference](configuration.md) |
| Understand MCP inputs and results | [MCP tools](mcp-tools.md) |
| Build and run the DOS executable | [Foreground DOS agent](../dos/README.md) |
| Load and operate the resident agent | [RA-TSR](tsr.md) |
| Discover and select multiple machines | [Discovery](discovery.md) |
| Reproduce the emulator test | [Testing guide](testing.md) |
| Diagnose a failure | [Troubleshooting](troubleshooting.md) |

## Design and implementation

| Topic | Document |
|---|---|
| Component boundaries and language choices | [Architecture](architecture.md) |
| Exact credentialed/open UDP wire format | [Protocol version 2](protocol.md) |
| DOS callback, resident stack, and deferral rules | [DOS reentrancy](dos-reentrancy.md) |
| Text and raw graphics representation | [Video capture](video-capture.md) |
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

The current implementation supports:

- target status and capability discovery;
- request-driven 80×25 text capture with CP437 cells and attributes;
- BIOS-compatible text and named-key injection;
- authenticated, replay-resistant UDP request/response;
- duplicate-safe keyboard retries;
- resident text/graphics capture and sandboxed file transfer;
- named local discovery and explicit multi-target routing;
- RA-TSR load/query/unload;
- a Linux simulator and a DOSBox-X/NE2000 end-to-end test.

It does not provide a direct execution tool, raw memory/port writes, reboot,
Windows/game compatibility guarantees, or PicoMEM integration. The bridge
reports unavailable capabilities rather than pretending they exist.
