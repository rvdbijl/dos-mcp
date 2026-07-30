# Security model

## Current boundary

The MCP host and modern bridge run on a trusted Linux workstation. The target,
network, MCP arguments, terminal output, and DOS screen bytes are untrusted.
The MCP transport is stdio. The optional target transport is authenticated
UDP on a trusted private LAN.

The Linux development backend starts a local interactive shell. `dos.send_keys` can therefore cause commands to run with the bridge process's user permissions. It is an explicit mutation and must only be enabled for a working directory the user intends to expose.

## Defaults

- The MCP bridge opens an ephemeral UDP client socket only when
  `DOS_MCP_TARGET` is set; the simulator binds only its explicitly configured
  address.
- Start the Linux child in the configured root directory.
- Expose status, capabilities, text capture, and keyboard input only.
- Do not expose direct execution, files, memory, ports, interrupts, reboot, or storage operations.
- Apply bounded text/key input and bounded settle delays.
- Never use an IP or MAC address as authentication. The DOS agent additionally
  pins these to an authenticated session to prevent cross-peer response reuse.
- Never log secrets or write diagnostics to MCP stdout.

The configured root is a working-directory boundary, not a filesystem sandbox. A shell can still access anything permitted to the operating-system user. Strong containment requires a separately configured container, namespace, VM, or restricted account.

## Threats and controls

| Threat | Initial control |
|---|---|
| Accidental command execution | Mutation is a separately named tool; no direct `exec` tool |
| Escape from configured root | Clearly documented; run under external OS sandbox when needed |
| Terminal escape injection | Parser accepts a small bounded CSI subset and never forwards escapes to the host terminal |
| Resource exhaustion | Fixed 80×25 buffer, bounded arguments, nonblocking reads |
| Child leakage | Backend owns one process group and has idempotent shutdown |
| MCP stream corruption | No stdout logging or import-time output |
| UDP spoofing/corruption | Per-machine password-derived or raw 128-bit key, authenticated nonce handshake, per-datagram MAC and CRC |
| UDP replay/duplicate mutation | Session-bound monotonic request IDs and cached duplicate response |
| Network disclosure | No encryption; private trusted LAN and no public port forwarding are mandatory |
| Dangerous DOS operations | Absent from initial capability and tool surface |

## Future permission levels

Adopt the levels in `PROJECT.md`, beginning with screen/status and explicit keyboard input. Each new mutating category needs all three:

1. bridge configuration authorization;
2. transport/agent enforcement;
3. local approval for high-impact operations where practical.

Authentication, integrity, replay protection, and confidentiality are separate properties. A local-network deployment does not remove any of them; confidentiality may be deferred on an 8088 only with a documented residual risk.

## Implemented DOS controls

- Passwords deterministically derive a 128-bit key using domain-separated
  SHA-256 truncated to 16 bytes; raw 32-hex keys remain supported.
- Password derivation is intentionally lightweight enough for an 8088, not
  resistant to offline dictionary guessing. Use a high-entropy passphrase.
- In credentialed mode every request, including `HELLO`, is authenticated.
- Optional open mode uses a documented public key and therefore provides no
  peer authentication. Both programs print/log a warning when it is selected.
- Malformed, unauthenticated, old-session, and wrong-peer datagrams are
  silently discarded.
- Request, response, fragment, diagnostic, text, and key counts are bounded.
- The foreground agent advertises no direct filesystem, execution, raw
  memory, port, reboot, or storage capability.
- Keyboard input is at-most-once across retries and partial acceptance is
  reported.
- `EXIT` and local Ctrl+Alt+Esc stop remote control.

The 32-bit XTEA-CBC-MAC tag is a constrained 8088 design, not a substitute
for a modern encrypted transport. It has an expected online forgery cost of
roughly 2^32 attempts and the protocol provides no confidentiality. Rate
limiting and stronger cryptographic alternatives remain open work.

Open mode retains the MAC-shaped wire field, CRC, sessions, replay rejection,
and duplicate suppression for protocol compatibility. Because its key is
public, an attacker can calculate valid tags; none of those mechanisms
authenticate the peer. Open mode is suitable only for isolated testing.

## Security review gates

Before a UDP DOS agent is usable outside a closed private test network:

- measure challenge/response and MAC cost on a 4.77 MHz 8088;
- define secret provisioning and rotation;
- add rate limiting and a wider replay window if the deployment requires it;
- fuzz packet decoding;
- independently audit the C decoder and arithmetic;
- adopt a stronger tag or secure outer tunnel for less trusted networks.

Before filesystem or execution tools:

- canonicalize DOS paths and reject traversal/device names;
- add read-only and sandbox-drive policies;
- define temporary-file and atomic-replace behavior;
- add explicit approvals and audit records.
