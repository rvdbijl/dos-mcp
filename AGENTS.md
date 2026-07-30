# Contributor instructions

Read `PROJECT.md` before changing architecture or expanding scope. This repository is building a modern MCP bridge for safely observing and controlling a real DOS machine; MCP and other heavyweight protocols never run on the DOS host.

## Current scope

- Phase 0 through the Phase 2 foreground MVP are implemented: architecture, modern bridge, Linux and UDP simulators, the version-1 protocol, and a real foreground DOS packet-driver endpoint.
- The first tool surface is `dos.get_status`, `dos.get_capabilities`, `dos.capture_screen`, and `dos.send_keys`.
- Do not implement PicoMEM/PicoMEM2 code unless a task explicitly requests it. Keep future transport seams generic and do not make PicoMEM assumptions.
- Do not turn the foreground agent into a TSR without a dedicated reentrancy and resident-memory milestone.
- Prefer small, reviewable vertical slices over broad scaffolding.

## Non-negotiable design rules

- Keep MCP, JSON-RPC, authentication policy, retries, rendering, and transaction state on the modern host.
- Separate MCP presentation, logical operations, transport, and target implementation.
- Treat every target response and every remote input as untrusted. Validate lengths, enum values, offsets, and state transitions.
- Default to observation. Keyboard input is an explicit mutation; file writes, execution, memory writes, port writes, reboot, and storage changes are out of the initial tool surface.
- Preserve eventual 8088 compatibility: little memory, bounded buffers, no unsafe DOS calls from callbacks or interrupt context, and no assumption of instructions newer than the 8088.
- Do not encode transport-specific behavior into MCP tool handlers.
- Do not claim hardware behavior without source-code or physical verification. Record unresolved assumptions in `docs/roadmap.md`.

## Python bridge

- Support Python 3.12 or newer and use the official MCP Python SDK 2.x.
- Keep production code under `src/dos_mcp` and tests under `tests`.
- Use standard-library code for the backend core unless a dependency clearly reduces risk.
- Keep stdout exclusively for MCP stdio traffic. Send diagnostics to stderr through `logging`.
- Use typed dataclasses for domain values and a `Backend` protocol for target implementations.
- A backend owns its resources and must implement idempotent `close()`.

## Testing

- Run `uv run pytest` for behavioral tests.
- Run `uv run ruff check .` for static checks.
- Run `make -C dos WATCOM=/path/to/watcom all` for the 16-bit targets.
- Use `tools/test_dosbox_x.sh` for the optional real packet-driver emulator test.
- Tests must be deterministic and must not require physical DOS hardware, PicoMEM hardware, or external network access.
- Add boundary tests for terminal dimensions, cursor position, scrolling, key validation, and backend shutdown when those areas change.

## Documentation

- Architecture decisions belong in `docs/architecture.md`.
- Wire-format requirements and decisions belong in `docs/protocol.md`; update test vectors when the wire format is implemented.
- Safety defaults and threat boundaries belong in `docs/security-model.md`.
- DOS callback/deferred-work rules belong in `docs/dos-reentrancy.md`.
- Video representation and capture rules belong in `docs/video-capture.md`.
- Milestones and unresolved hardware/source assumptions belong in `docs/roadmap.md`.

## Completion standard

A change is complete when its behavior is tested, user-visible setup is documented, dangerous behavior has an explicit policy, and no unrelated future phase is silently pulled into scope.
