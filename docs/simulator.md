# UDP simulator

The simulator wraps `LinuxTerminalBackend` in the same credentialed (or
explicitly open) UDP agent protocol used by `RAGENT.EXE`. It separates
bridge/transport work from DOS toolchain and hardware work.

```text
MCP client
   │ stdio
dos-mcp
   │ authenticated/open UDP
dos-mcp-simulator
   │ PTY
Linux shell
```

The simulator is useful for:

- validating environment-based UDP backend selection;
- exercising handshakes, session keys, replay checks, and fragmentation;
- developing MCP behavior without DOSBox or hardware;
- inspecting timeout and wrong-key behavior;
- running deterministic Python tests around duplicate mutations.

It is not a DOS emulator. Its status reports Linux, terminal input uses
terminal escape sequences, and screen data originates from a UTF-8 PTY before
being represented through the protocol.

## Run

Start the simulator:

```bash
SIM_PASSWORD='local-simulator-only'

uv run dos-mcp-simulator \
  --bind 127.0.0.1:21300 \
  --password "$SIM_PASSWORD" \
  --root /tmp/dos-mcp-simulator \
  --shell /bin/sh
```

Start the bridge in another terminal:

```bash
DOS_MCP_TARGET=127.0.0.1:21300 \
DOS_MCP_PASSWORD="$SIM_PASSWORD" \
uv run dos-mcp
```

Connect an MCP client to the bridge process, not directly to the simulator.
`--key` remains available for a raw 32-hex key. Omitting both credential
options starts the simulator in visibly warned, unauthenticated open mode;
omit both `DOS_MCP_KEY` and `DOS_MCP_PASSWORD` on the bridge to match.

## Lifecycle

The simulator:

- opens one UDP socket at the configured bind address;
- accepts one authenticated bridge session at a time;
- lazily starts one Linux PTY shell;
- retains a bounded pending-fragment map and four completed responses;
- closes the PTY backend on normal shutdown;
- treats SIGINT and SIGTERM as shutdown requests.

A newly authenticated `HELLO` replaces the previous session. This is
intentional for the current single-controller model.

## Reliability tests

`tests/test_udp.py` covers:

- status and capabilities through UDP;
- a full fragmented text screen;
- loss of the first mutation response;
- cached response replay without repeated keyboard input;
- timeout with the wrong key.

These tests bind loopback UDP sockets. Sandboxed environments may need local
network permission even though no external traffic is used.

## Limitations

The current CLI does not expose fault-injection switches. The test server has
a focused first-response-drop option used by the suite, but latency,
corruption, reordering, target reboot, and arbitrary loss profiles remain
roadmap work.

The simulator must not be exposed as a public service. It launches a shell
and accepts authenticated keyboard input, and version 1 provides no
confidentiality.
