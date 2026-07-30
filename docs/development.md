# Development guide

## Repository layout

```text
src/dos_mcp/
  server.py             MCP presentation
  runtime.py            backend selection and ownership
  backend.py            logical Backend protocol
  models.py             transport-independent results
  terminal.py           bounded text-terminal model
  backends/linux.py     local PTY backend
  backends/udp.py       reliable authenticated UDP client
  agent_server.py       UDP simulator-side server
  simulator.py          simulator CLI
  protocol/             Python protocol constants and codecs

dos/
  agent/ragent.c        foreground operation loop and shell
  agent/dmnet.c         ARP, IPv4, and UDP
  agent/dmpacket.c      packet-driver downcalls and receive state
  agent/dmpacket_rx.asm register-safe receive upcall
  agent/dmproto.c       C protocol implementation
  include/              shared DOS headers
  tests/proto_test.c    cross-language golden vectors

tests/                  Python unit and integration tests
tools/                  DOSBox-X test client and harness
docs/                   design, operations, and contributor documentation
```

## Architectural rule

MCP handlers depend only on the logical `Backend` contract. They do not pack
UDP messages, manipulate PTYs, inspect video memory, or know which transport
is active.

When adding behavior, keep these layers separate:

1. domain model and backend operation;
2. MCP schema and presentation;
3. protocol payload, if remote;
4. Python simulator dispatch;
5. DOS implementation;
6. capability advertisement and security policy;
7. tests and documentation.

Transport-specific branching inside an MCP tool is a design regression.

## Adding an operation

For a new logical operation:

1. decide whether it is observational or mutating;
2. add typed request/result values to `models.py` where appropriate;
3. extend `Backend`;
4. implement or explicitly reject it in each backend;
5. add an opcode and strictly validated codec;
6. update Python and C constants together;
7. implement duplicate/retry semantics before exposing a mutation;
8. enforce the same authorization at bridge and target layers;
9. advertise the capability only when it is usable;
10. add unit, UDP, C-vector, and DOSBox tests in proportion to risk;
11. update the tool, protocol, security, and roadmap documents.

Do not reserve a capability bit and then report it as available before the
complete path works.

## Python conventions

- Python 3.12 or newer.
- Production code under `src/dos_mcp`.
- Tests under `tests`.
- Typed dataclasses for values crossing layers.
- Standard-library implementation for core transport unless a dependency
  clearly reduces risk.
- No import-time process or socket creation.
- No stdout logging: stdout belongs exclusively to MCP stdio.
- Every backend owns its resources and has idempotent `close()`.
- Every external length, enum, address, and count is validated.

Run:

```bash
uv run ruff check .
uv run python tools/check_docs.py
uv run pytest
```

## DOS conventions

- Generate only 8086/8088-compatible instructions.
- Use fixed-size buffers and bounded loops.
- Avoid heap allocation in the network path.
- Never call DOS from packet callbacks or hardware interrupt context.
- Keep register-sensitive entry points in small reviewed assembly modules.
- Validate first, then copy.
- Use unsigned-width typedefs from `dmproto.h`.
- Treat packet loss and duplicate requests as normal.
- Keep foreground shell behavior distinct from eventual TSR behavior.

Open Watcom is invoked through `dos/Makefile`; do not rely on an IDE-specific
project file.

## Protocol compatibility

Numeric constants and byte layouts are versioned API. A change that alters an
encoded vector must either:

- remain backward compatible within version 1 and update both
  implementations; or
- introduce a new protocol version.

Always update:

- Python encoder/decoder tests;
- `dos/tests/proto_test.c`;
- [Protocol version 1](protocol.md);
- any affected maximums in capabilities and docs.

## Documentation conventions

- Keep runnable commands copyable from the repository root.
- Distinguish verified behavior from plans.
- Mark example keys as test-only.
- Link to the source-of-truth document rather than duplicating long wire
  layouts.
- Update [Documentation](index.md) when adding a user-facing guide.

## Before a commit

```bash
uv run ruff check .
uv run python tools/check_docs.py
uv run pytest
git diff --check
```

When DOS code changes, also build with Open Watcom and run the DOSBox-X
harness if the toolchain is available.

The repository may have unrelated local changes. Review `git status` and
stage only the intended files.
