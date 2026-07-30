# Testing

The project tests progressively from pure codecs to a real 16-bit packet
driver. Most development needs only the first two layers.

## Test matrix

| Layer | Command | External requirements |
|---|---|---|
| Python lint | `uv run ruff check .` | Python environment |
| Documentation links | `uv run python tools/check_docs.py` | Python environment |
| Python behavior | `uv run pytest` | Loopback sockets for UDP tests |
| Native C vectors | command below | Host C89 compiler |
| DOS build | `make -C dos WATCOM=/path/to/watcom all` | Open Watcom 2 |
| In-DOS vectors and network E2E | `tools/test_dosbox_x.sh` | Open Watcom, DOSBox-X, packet driver |

## Python suite

```bash
uv sync
uv run ruff check .
uv run python tools/check_docs.py
uv run pytest
```

The tests cover:

- terminal dimensions, scrolling, cursor movement, and text attributes;
- Linux backend startup, key input, validation, and idempotent shutdown;
- MCP tool registration, annotations, argument preservation, and results;
- CRC16, XTEA, session-key, and packet-MAC vectors;
- packet truncation, corruption, fragmentation, and reassembly;
- payload codec validation;
- UDP authentication, retries, fragmented screens, duplicate suppression,
  and wrong-key timeout.

## Native C89 vectors

The DOS protocol code is also portable enough to compile on the host:

```bash
gcc -std=c89 -Wall -Wextra -Werror \
  -Idos/include \
  dos/tests/proto_test.c dos/agent/dmproto.c \
  -o /tmp/dos-mcp-proto-test

/tmp/dos-mcp-proto-test
```

Expected output:

```text
PASS protocol vectors
```

This catches C/Python drift quickly, but it does not prove 16-bit ABI or
instruction compatibility.

## Open Watcom build

```bash
make -C dos WATCOM=/path/to/watcom all
```

Expected outputs in `dos/build/`:

- `PROTOCHK.EXE`
- `RAGENT.EXE`

The Makefile selects 8086/8088 code generation, the small memory model,
size optimization, stack checks off, and warnings as errors. The packet
receive upcall is assembled separately because its register ABI cannot be
expressed safely as a compiler-dependent C interrupt parameter list.

## DOSBox-X end-to-end test

Obtain a Crynwr-compatible `NE2000.COM` separately, then run:

```bash
WATCOM=/path/to/watcom \
DOSBOX_X=/path/to/dosbox-x \
PACKET_DRIVER=/path/to/NE2000.COM \
tools/test_dosbox_x.sh
```

For an unpacked DOSBox-X bundle:

```bash
WATCOM=/path/to/watcom \
DOSBOX_X=/path/to/dosbox-x \
DOSBOX_LIBDIR=/path/to/unpacked/usr/lib/x86_64-linux-gnu \
PACKET_DRIVER=/path/to/NE2000.COM \
tools/test_dosbox_x.sh
```

The harness:

1. builds `PROTOCHK.EXE` and `RAGENT.EXE`;
2. copies the externally supplied packet driver into the ignored build tree;
3. starts DOSBox-X with a deterministic NE2000/SLiRP profile;
4. runs the protocol vectors inside DOS;
5. starts the packet driver and foreground agent;
6. authenticates with a password-derived key from the Python UDP backend;
7. verifies status and capabilities;
8. receives a multi-fragment 80×25 VGA snapshot;
9. sends `VER` and Enter through the BIOS queue;
10. captures and verifies the resulting DOS text;
11. kills the isolated emulator process and removes its temporary log.

Expected terminal output includes:

```text
PASS: password-derived authentication, status, capabilities, fragmented VGA text capture, BIOS keys, and VER output
```

The harness uses host UDP port 21300. Stop any process already using that
port before running it.

## What remains hardware-only

DOSBox-X verifies the software path, not physical timing or compatibility.
Before a release claims real 8088 support, measure on representative
hardware:

- executable and runtime conventional-memory use;
- receive callback duration;
- XTEA-MAC request cost;
- 4 KB screen-copy time;
- loss and retry behavior under adapter load;
- BIOS queue behavior;
- CGA/MDA mode and cursor detection.

Record results in [Hardware support](hardware-support.md) and unresolved
items in [Roadmap](roadmap.md).
