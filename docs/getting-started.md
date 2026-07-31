# Getting started

This guide takes the shortest path from a fresh checkout to each supported
runtime. Start with the local backend, then add UDP or DOS only if needed.

## Prerequisites

- Linux
- Python 3.12 or newer
- [`uv`](https://docs.astral.sh/uv/)
- an MCP client that can launch a stdio server

The real DOS build additionally needs Open Watcom 2, a DOS packet driver, and
either DOS hardware or DOSBox-X. See [the DOS guide](../dos/README.md).

## Install the Python environment

From the repository root:

```bash
uv sync
```

This creates a project-local virtual environment from `pyproject.toml` and
`uv.lock`.

## Option A: local Linux backend

Run:

```bash
uv run dos-mcp
```

The process is a stdio MCP server. It normally prints nothing because stdout
is reserved for MCP messages. The backend starts an interactive `/bin/sh`
inside an 80×25 pseudo-terminal when the first tool is called.

To select the shell and its starting directory:

```bash
DOS_MCP_ROOT=/work/retro-project \
DOS_MCP_SHELL=/bin/bash \
uv run dos-mcp
```

This is a development backend, not a DOS emulator. Its status result says
`linux-terminal`, its text is UTF-8, and its keyboard mode is
`terminal-sequences`.

`DOS_MCP_ROOT` changes the child's initial directory and `HOME`; it is not an
OS sandbox. Commands entered through `dos.send_keys` still have the
permissions of the user running the bridge.

## Add the server to an MCP client

Configure the client to launch the equivalent of:

```text
command: uv
arguments: run --project /absolute/path/to/dos-mcp dos-mcp
working directory: /absolute/path/to/dos-mcp
```

For a DOS target, add `DOS_MCP_TARGET` and normally `DOS_MCP_PASSWORD` to that
server process's environment. Exact configuration syntax is client-specific.

After connection:

1. call `dos.list_targets`;
2. select a target;
3. call `dos.get_status`;
4. call `dos.get_capabilities`;
5. call `dos.capture_screen`;
6. only then use a mutation.

The tool details and result shapes are in [MCP tools](mcp-tools.md).

## Option B: Linux-backed UDP simulator

Choose a throwaway test passphrase:

```bash
TEST_PASSWORD='local-simulator-only'
```

In terminal one:

```bash
uv run dos-mcp-simulator \
  --bind 127.0.0.1:21300 \
  --password "$TEST_PASSWORD" \
  --root "$PWD"
```

In terminal two:

```bash
DOS_MCP_TARGET=127.0.0.1:21300 \
DOS_MCP_PASSWORD="$TEST_PASSWORD" \
uv run dos-mcp
```

This exercises the authenticated UDP protocol, retries, fragmentation, and
CP437 conversion while using a Linux PTY as the logical target. Do not reuse
the example password on a real machine.

## Option C: foreground DOS agent

For the quickest hardware bring-up, copy the repository-root `bin/` directory
to `C:\DOSMCP`, edit `MTCP.CFG`, and load the adapter's packet driver. Then run
the two offline checks and start the resident endpoint:

```dos
CD \DOSMCP
PROTOCHK
CFGCHK
STARTTSR MyUniqueLabPassphrase
```

`STARTTSR.BAT` creates `C:\DOSMCP\REMOTE`, sets
`MTCPCFG=C:\DOSMCP\MTCP.CFG`, and obtains the endpoint IP and packet-driver
interrupt from that file. Its placeholder TEST-NET addresses must be edited
before use. Give each PC a unique name by changing the final `DOS-PC` argument
in the batch file.

Build the DOS executables:

```bash
make -C dos WATCOM=/path/to/watcom all
```

On DOS, after loading the adapter's packet driver:

```dos
RAGENT pass:MyUniqueLabPassphrase 192.168.10.55 21300 0x60
```

If the PC already has a working mTCP configuration, reuse it instead:

```dos
SET MTCPCFG=C:\MTCP.CFG
RAGENT pass:MyUniqueLabPassphrase - 21300 -
```

On the modern host:

```bash
DOS_MCP_TARGET=192.168.10.55:21300 \
DOS_MCP_PASSWORD=MyUniqueLabPassphrase \
uv run dos-mcp
```

Use a unique high-entropy passphrase per machine. The UDP protocol
authenticates packets but does not encrypt screen or keyboard data, so use a
trusted private network. Omitting the credential on both sides enables
unauthenticated open mode for isolated testing. Full packet-driver, DOSBox-X,
and emergency-stop instructions are in [the DOS guide](../dos/README.md).

## Option D: resident DOS agent

Create a dedicated transfer directory and load RA-TSR with a visible name:

```dos
MD C:\REMOTE
RA-TSR pass:MyUniqueLabPassphrase 192.168.10.55 21300 0x60 C:\REMOTE RW WORKBENCH-386
```

Connect directly, adding bridge file policy when wanted:

```bash
DOS_MCP_TARGET=192.168.10.55 \
DOS_MCP_PASSWORD=MyUniqueLabPassphrase \
DOS_MCP_ALLOW_FILE_READ=1 \
DOS_MCP_ALLOW_FILE_WRITE=1 \
uv run dos-mcp
```

Or listen for disconnected resident systems:

```bash
DOS_MCP_DISCOVERY=1 \
DOS_MCP_PASSWORD=MyUniqueLabPassphrase \
uv run dos-mcp
```

Call `dos.list_targets` and use the returned selector. With multiple targets,
the bridge requires `target` on every operation. See [RA-TSR](tsr.md) and
[Discovery](discovery.md).

## Verify the checkout

Run the normal checks:

```bash
uv run ruff check .
uv run pytest
```

For the 16-bit and DOSBox-X layers, continue with the
[testing guide](testing.md).
