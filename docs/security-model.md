# Security model

## Trust boundary

The MCP client, modern bridge process, and operator are trusted for the
selected deployment. The DOS target, network, discovery announcements, MCP
arguments, terminal output, screen bytes, filenames, and remote responses are
untrusted inputs.

The protocol is intended for a trusted private LAN. It is not an
Internet-facing remote-management protocol and does not provide
confidentiality.

## Default and opt-in behavior

- With no UDP configuration, the bridge starts only the local Linux PTY
  backend.
- File reads and writes are independently disabled on the modern bridge
  unless `DOS_MCP_ALLOW_FILE_READ=1` or `DOS_MCP_ALLOW_FILE_WRITE=1`.
- RA-TSR file access is independently disabled unless local load-time access
  is `R`, `W`, or `RW`.
- Upload therefore requires modern-host permission, DOS-local permission, and
  a per-call overwrite decision when replacing a file.
- Direct execution, arbitrary memory/port access, reboot, and storage-sector
  operations are not exposed.
- MCP stdout is reserved for protocol traffic; diagnostics use logging/stderr.
- Backends own and idempotently close their sockets, listener, PTY, child,
  transfer, or packet-driver resources.

The Linux root is a working-directory/path boundary, not an OS sandbox.
Commands entered into the PTY retain the bridge user's permissions.

## Authentication modes

### Credentialed mode

A raw 128-bit key or password-derived 128-bit key authenticates `HELLO`.
Nonce-derived session keys authenticate every later packet. Session binding,
advancing request IDs, and duplicate-response handling reduce replay and
duplicate mutation risk.

Password derivation is intentionally lightweight enough for 16-bit DOS and
is vulnerable to offline guessing of weak passwords. Use a generated,
high-entropy, unique credential. The password is not transmitted.

Protocol v2's packet MAC has a 32-bit tag. Expected online forgery cost is
only roughly 2^32 attempts before accounting for rate/network controls. This
is an 8088 feasibility compromise, not a modern cryptographic security level.

### Open mode

Open mode uses a documented public key so packet framing and duplicate logic
remain identical. Anyone who can send traffic can compute valid tags. Open
mode provides no peer authentication and is suitable only for isolated
testing. Both DOS programs and the Linux bridge make the mode conspicuous.

### Confidentiality

Neither mode encrypts screen, keyboard, file, address, timing, or capability
data. Use physical isolation, a trusted VLAN, or a secure outer tunnel when
confidentiality is required. Never directly forward the DOS operation port
from the Internet.

## Discovery boundary

Discovery announcements are not authenticated. Controls are:

- destination `255.255.255.255` and broadcast Ethernet only;
- IP TTL 1;
- bounded fixed format, visible ASCII name, exact length, and CRC validation;
- no announcement while an authenticated session is active;
- source address used for routing instead of an embedded claimed host;
- mandatory normal `HELLO` authentication before every backend can operate.

A local attacker can spoof a name, adapter ID, capability set, or address,
and can flood the listener. The result should be limited to misleading target
list entries and connection attempts that fail authentication. Discovery
must never become authority. Network policy should block UDP 21301 on
untrusted interfaces.

When multiple targets exist, MCP operations require an explicit selector.
This prevents newly discovered machines from changing the implicit mutation
destination.

## Keyboard mutation

`dos.send_keys` is explicitly non-idempotent at the MCP layer and can cause
program execution through an existing DOS or Linux shell. Controls:

- capture-before-input operator guidance;
- bounded 4,096-byte text and 128 named-key arguments on the modern side;
- bounded BIOS queue insertion and partial receipts on DOS;
- advancing request IDs and cached response behavior across retries;
- no keyboard-controller port emulation or direct-input game compatibility
  claim.

Acceptance means input entered a queue, not that the application performed
the intended action. Capture and verify afterward.

## RA-TSR file boundary

RA-TSR accepts only relative bounded paths under its configured root. It
rejects drive letters, absolute paths, empty/dot/dot-dot components, wildcard
and control punctuation, oversized components, and oversized complete paths.

Downloads are sequential and CRC32-verified. Uploads use a temporary file,
declared size/CRC, sequential offsets, final integrity verification, and
rename-on-commit. Incomplete transfers are aborted during DOS-idle
session cleanup. Unload refuses while a transfer still owns resources.

Residual risks include DOS device/file aliasing, filesystem corruption,
TOCTOU behavior under local software, weak/no DOS permissions, 8.3 alias
surprises, and differences among DOS kernels. Use a dedicated transfer
directory and backups. This feature is not a general-purpose secure file
server.

## Resident execution risks

The packet callback only records a bounded frame. Resident work uses a private
stack and a nested-work guard. Timer entries never call DOS; file opcodes are
dispatched only through DOS's `INT 28h` idle interrupt and when no critical
error is active.

Even with those controls, timer-resident C, packet drivers, BIOS code, DOS
kernels, and applications were not designed as a modern preemptive system.
Unsupported software may replace vectors, disable interrupts, read hardware
directly, or use non-reentrant drivers. RA-TSR refuses unsafe unload ordering,
but it cannot make every third-party TSR or game compatible.

Maintain local keyboard/reset access and test on the exact DOS/driver stack
before relying on it.

## Threat/control summary

| Threat | Control | Residual risk |
|---|---|---|
| Packet corruption | CRC16, lengths, enums, offsets, CRC32 | denial of service |
| Unauthorized LAN peer | pre-shared credential and session MAC | 32-bit tag, weak passwords |
| Duplicate keyboard/write | request IDs and cached results | state lost after restart |
| Wrong machine selected | explicit selector when multiple targets | spoofed discovery list |
| File traversal | relative component validation and fixed root | DOS alias/device quirks |
| Partial upload | temporary file, size/CRC, commit | local filesystem failure |
| TSR stack/reentry | private stack, active guard, `INT 28h` file gate | unverified DOS/driver combinations |
| Secret disclosure | no secret logging, environment config | shell history/process environment |
| Traffic disclosure | private LAN/outer tunnel guidance | protocol itself is plaintext |

## Required review before broader deployment

- physical 4.77 MHz 8088 timing and memory measurements;
- DOS 3.x/5.x/6.x/FreeDOS and representative packet-driver testing;
- C parser fuzzing and independent review;
- discovery flood/rate controls;
- operational key provisioning and rotation;
- per-target secret-provider support;
- a wider tag or authenticated encrypted outer transport;
- additional DOS path/device-name audit.
