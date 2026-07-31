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
2. that the simulator, `RAGENT`, or `RA-TSR` is running;
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

If `MTCPCFG` is set, confirm it names a readable full path and contains valid
`IPADDR` and `PACKETINT` lines. `HOSTNAME` is optional and supplies RA-TSR's
visible name; `HOSTNAME_ASSIGNED` is its DHCP fallback. Recognized values end
at whitespace, so ordinary trailing annotations are ignored. Startup reports
configuration failures before it contacts the packet driver. Sharing that
file with mTCP does not guarantee
that the driver permits a running mTCP utility and DOS MCP endpoint to claim
the same packet types at once; exit the foreground utility and retry.

For the checked DOSBox-X profile:

```dos
NE2000 0x60 10 0x300
RAGENT pass:<password> 10.0.2.15 21300 0x60
```

The equivalent mTCP-configured invocation is:

```dos
SET MTCPCFG=C:\MTCP.CFG
RAGENT pass:<password> - 21300 -
```

For a fresh no-argument RA-TSR install, the default root does not need to exist
because file access defaults off. A requested `R`, `W`, or `RW` root still
must exist.

Different hardware needs its own driver arguments.

## DOSBox-X reports illegal interrupts or opcodes

Use the current `dmpacket_rx.asm` build. A C callback copied from a Turbo C
packet-driver example is not ABI-compatible with Open Watcom's interrupt
parameter layout and can corrupt memory. Clean stale binaries, rebuild with
the documented Open Watcom toolchain, and rerun the harness.

## A text screen is blank or incorrect

- Verify the machine is in an 80×25 text mode.
- Verify the active page and cursor are valid.
- Mode 7 uses monochrome memory at `B000h`; other implemented text modes use
  `B800h`.
- The current agent caps capture at 80 columns and 25 rows.
- Use `dos.capture_graphics`, not `dos.capture_screen`, in a supported graphics
  mode.

Use `dos.get_capabilities` and inspect `adapter`, `video_mode`, dimensions,
and `active_page` in the capture.

## Graphics capture is rejected or looks wrong

`RA-TSR` supports CGA modes 4/5/6, Hercules graphics, EGA modes
0Dh/0Eh/0Fh/10h, VGA modes 11h/12h, and packed VGA mode 13h. Other modes are
rejected rather than guessed. The returned bytes are raw packed, interleaved,
or plane-major video memory as described by the result's `layout`; they are
not a PNG and do not include a VGA palette.

Check that the application did not switch mode during capture. Direct
hardware tricks, tweaked timings, banked SVGA, Mode X, and unusual clone
adapters are outside the implemented format set.

## Keys are accepted but nothing happens

Acceptance means queueing/processing, not application-level success.

- Capture the screen to confirm the expected prompt.
- Include `ENTER` when entering a command.
- Try a small inter-key delay.
- BIOS queue injection does not work for software that reads keyboard
  controller ports directly.
- `RAGENT` controls its own shell. `RA-TSR` can queue keys for another
  foreground BIOS/DOS application, but direct-controller games may ignore
  them.

## A long-running DOS command causes timeouts

This is an expected foreground limitation. `RAGENT` cannot service packets
while `system()` runs a child command. The bridge retries, and duplicate
mutation protection prevents repeated input once the cached response exists,
but the operation must eventually return to the agent shell.

Do not use commands that wait indefinitely for local input unless you have a
local recovery plan.

## `RA-TSR /U` refuses to unload

The resident agent only unloads when its `INT 08h`, `INT 1Ch`, `INT 28h`, and
`INT 2Fh` vectors are still the active top-of-chain handlers and no transfer is
active.
If another TSR was loaded afterward, unload that program first in reverse
order and retry. If a transfer is active, finish/abort it or wait for session
expiry and DOS-idle cleanup. Do not force-free the resident PSP; reboot
normally if the chain cannot be restored safely.

## RA-TSR disappears during a diagnostic or game

Run `RA-TSR` without arguments after returning to DOS and compare its counters:

- increasing `int08` with a stationary `int1c` and increasing `fallback`
  means the foreground program bypassed `INT 1Ch`; the watchdog is operating;
- increasing timer counters with stationary packet `allocate`/`complete`
  counts means the NIC IRQ, packet driver, or AT PIC cascade stopped delivery;
- increasing packet completion with stationary worker counts indicates a
  resident scheduler or nested-work problem;
- stationary `int08` and `int1c` means interrupts/IRQ0 were masked or another
  program replaced the hardware timer without chaining.

The PIC bytes are snapshots, not proof of which physical IRQ the packet driver
uses. Record the adapter IRQ separately. A software packet-driver vector such
as `60h` is not the NIC's hardware IRQ.

## Discovery does not show a resident target

- Start the bridge with `DOS_MCP_DISCOVERY=1`.
- Permit local UDP 21301 broadcasts on the intended interface.
- Confirm `RA-TSR /Q` reports the expected name and no active session.
- Wait at least five seconds while interrupts are enabled.
- Remember that advertisements stop during an active bridge session.
- Use `DOS_MCP_TARGETS` as an explicit fallback on networks that filter
  limited broadcast.

Discovery is unauthenticated. A listed name is only a routing hint; the
normal credential handshake establishes control.

## File transfer is denied or interrupted

Both ends enforce file policy. Load `RA-TSR` with `R`, `W`, or `RW` and set
the corresponding `DOS_MCP_ALLOW_FILE_READ=1` or
`DOS_MCP_ALLOW_FILE_WRITE=1` on the bridge. Paths must remain relative to the
configured existing DOS root and may not contain traversal components.

Uploads are sequential and commit only after length and CRC-32 validation.
After an interrupted upload, reconnect and retry; the temporary file is
discarded on session cleanup rather than exposed as the destination.

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

At the `RAGENT` shell, type `EXIT`, or hold Ctrl+Alt and press Esc. For the
resident endpoint, return to DOS and run `RA-TSR /U`. If a foreground child
or application cannot return control, use the emulator or physical machine's
local controls.
