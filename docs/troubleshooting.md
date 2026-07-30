# Troubleshooting

## The MCP server appears silent

This is normal for a stdio server. Stdout is reserved for MCP protocol
traffic, and the process waits for a client. Launch it through an MCP client
or use the Python tests to inspect behavior.

If it exits immediately, run the same command in a terminal and inspect
stderr.

## The bridge warns about unauthenticated open mode

`DOS_MCP_TARGET` now works without a credential, but that deliberately selects
open mode. Configure the same password on both peers to authenticate them:

```bash
DOS_MCP_TARGET=127.0.0.1:21300 \
DOS_MCP_PASSWORD='the-same-high-entropy-passphrase' \
uv run dos-mcp
```

Or use `DOS_MCP_KEY` with the same raw 32-hex key as the simulator or DOS
agent. Do not set both variables.

## The credential is rejected

`DOS_MCP_PASSWORD` must be nonempty. A raw `DOS_MCP_KEY` must contain exactly
32 hexadecimal characters and decode to 16 nonzero bytes; remove `0x`,
separators, and accidental whitespace.

On DOS, `pass:` and `key:` force the intended interpretation. A bare
32-hex-character value is a legacy raw key; every other nonempty bare value
is a password. Use `-` for explicit open mode when supplying later arguments.
The whole DOS command tail normally has a 127-byte limit.

## UDP requests time out

Check, in order:

1. target IP and UDP port;
2. that the simulator or `RAGENT` is still running;
3. that both sides use the same credential mode and value;
4. host firewall rules;
5. DOSBox-X UDP port-forward initialization;
6. packet-driver interrupt, IRQ, and I/O address;
7. duplicate use of host port 21300.

Wrong-password and wrong-key packets are intentionally discarded without an
authentication oracle, so they look like a timeout. Open mode only connects
to another open-mode peer.

For DOSBox-X, inspect its log for:

```text
SLIRP: Setup UDP port 21300:21300 forward
ETHERNET: NE2000 Ethernet emulation backend selected: slirp
```

If setup says the UDP forward failed, another process probably owns the
port.

## `RAGENT: packet driver initialization failed`

Confirm that:

- the packet driver loaded successfully before `RAGENT`;
- its software interrupt matches the final `RAGENT` argument;
- the interrupt entry contains the packet-driver signature;
- the driver accepts Ethernet IP and ARP access types;
- no other DOS network stack exclusively owns those types.

For the checked DOSBox-X profile:

```dos
NE2000 0x60 10 0x300
RAGENT pass:<password> 10.0.2.15 21300 0x60
```

Different hardware needs its own driver arguments.

## DOSBox-X reports illegal interrupts or opcodes

Use the current `dmpacket_rx.asm` build. A C callback copied from a Turbo C
packet-driver example is not ABI-compatible with Open Watcom's interrupt
parameter layout and can corrupt memory. Clean stale binaries, rebuild with
the documented Open Watcom toolchain, and rerun the harness.

## The screen is blank or incorrect

- Verify the machine is in an 80×25 text mode.
- Verify the active page and cursor are valid.
- Mode 7 uses monochrome memory at `B000h`; other implemented text modes use
  `B800h`.
- The current agent caps capture at 80 columns and 25 rows.
- Graphics modes are not implemented and must not be interpreted as text.

Use `dos.get_capabilities` and inspect `adapter`, `video_mode`, dimensions,
and `active_page` in the capture.

## Keys are accepted but nothing happens

Acceptance means queueing/processing, not application-level success.

- Capture the screen to confirm the expected prompt.
- Include `ENTER` when entering a command.
- Try a small inter-key delay.
- BIOS queue injection does not work for software that reads keyboard
  controller ports directly.
- The foreground MVP controls its own shell; it is not a resident controller
  for arbitrary applications.

## A long-running DOS command causes timeouts

This is an expected foreground limitation. `RAGENT` cannot service packets
while `system()` runs a child command. The bridge retries, and duplicate
mutation protection prevents repeated input once the cached response exists,
but the operation must eventually return to the agent shell.

Do not use commands that wait indefinitely for local input unless you have a
local recovery plan.

## The Linux backend can access files outside `DOS_MCP_ROOT`

`DOS_MCP_ROOT` is only a starting directory and `HOME` value. It is not a
filesystem sandbox. Run the bridge in a container, VM, namespace, or
restricted account for stronger isolation.

## Tests fail with socket permission errors

The UDP tests use localhost sockets. Some sandboxes disable all networking,
including loopback. Permit local binds/connects or run:

```bash
uv run pytest -q --ignore=tests/test_udp.py
```

Then run `tests/test_udp.py` in an environment with loopback access before
merging.

## The DOSBox-X harness cannot find dependencies

Set explicit paths:

```bash
WATCOM=/path/to/watcom \
DOSBOX_X=/path/to/dosbox-x \
DOSBOX_LIBDIR=/path/to/unpacked/libs \
PACKET_DRIVER=/path/to/NE2000.COM \
tools/test_dosbox_x.sh
```

`DOSBOX_LIBDIR` is only needed when DOSBox-X was unpacked with private shared
libraries rather than installed normally.

## Local emergency stop

At the `RAGENT` shell, type `EXIT`, or hold Ctrl+Alt and press Esc. If the
foreground child is running, the agent cannot process the hotkey until
control returns; use the emulator or physical machine's local controls when
necessary.
