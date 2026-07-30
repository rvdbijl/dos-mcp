# Operations and safety

The foreground MVP is intended for a trusted operator managing a retro
machine on a private network. It is not an Internet remote-management
service.

## Deployment checklist

Before enabling credentialed UDP control:

1. choose a unique high-entropy passphrase or generate a random 16-byte key;
2. provision the same credential directly on the bridge and DOS console;
3. place the target and bridge on a trusted private segment;
4. block the agent UDP port at external routers and host firewalls;
5. confirm the packet-driver interrupt, IRQ, I/O base, and target IP;
6. start `RAGENT` locally and verify its visible ready message;
7. call status and capabilities before sending input;
8. capture the screen and confirm the expected prompt;
9. test the local Ctrl+Alt+Esc emergency stop;
10. keep a local recovery path to the keyboard, emulator, or power control.

## Credential management

The simplest setup uses a passphrase:

```dos
RAGENT pass:MyUniqueLabPassphrase 192.168.10.55 21300 0x60
```

```bash
DOS_MCP_TARGET=192.168.10.55:21300 \
DOS_MCP_PASSWORD=MyUniqueLabPassphrase \
uv run dos-mcp
```

The derivation is fast enough for an 8088 and is consequently vulnerable to
offline guessing of weak passwords. Prefer a generated multiword or random
passphrase, not a familiar password.

To use a raw key instead, generate one:

```bash
openssl rand -hex 16
```

Use a different credential for every target. Rotate it when:

- it appears in a public log, screenshot, shell history, or commit;
- a bridge host is lost or compromised;
- target ownership changes;
- an operator who knew the key should no longer have access.

Version 1 has no credential-negotiation or rotation operation. Stop the agent,
change both configurations, and restart it.

Environment variables can be visible to same-user process inspection on
some systems. For higher-assurance deployments, launch the bridge through a
service manager or wrapper that provides the environment without copying the
secret into a shared script.

Omitting the credential on both peers enables open mode. It is not a
convenience security setting: anyone able to send UDP to the agent can forge
commands. Use open mode only on an isolated test network, and treat the
startup warning as a deployment failure anywhere else.

## Network boundary

Protocol version 1 provides:

- a pre-shared-key authenticated `HELLO`;
- nonce-derived session keys;
- a MAC and CRC on every datagram;
- session binding to the peer;
- monotonically advancing request IDs;
- duplicate response caching for mutations.

It does not provide:

- encryption or traffic-flow confidentiality;
- Internet-grade denial-of-service protection;
- per-user identities;
- rate limiting;
- remote key rotation;
- multi-controller coordination.

Screen contents, typed commands, addresses, timing, and packet sizes are
observable to a network monitor. Use an isolated LAN or an authenticated
encrypted outer tunnel if traffic leaves a trusted segment.

## Safe control procedure

For every mutation:

1. verify target identity and phase;
2. capture the current screen;
3. send the smallest useful input;
4. use a delay suitable for the machine;
5. capture the resulting screen;
6. stop if the result differs from expectation.

Do not blindly replay a series of commands based only on timing. Retro
machines may show prompts, errors, or modal UI at different speeds.

## Foreground limitations

`RAGENT` is both network endpoint and foreground shell. A child command
temporarily prevents network servicing. Prefer short, noninteractive
commands. Avoid:

- programs that never return;
- commands requiring unplanned local input;
- commands that switch to unsupported graphics modes if remote visibility is
  required;
- destructive disk utilities;
- configuration changes without a bootable recovery disk.

The capability response intentionally reports direct execution, filesystem,
memory, port, and reboot operations as unavailable.

## Emergency recovery

Normal local stop:

```text
RAGENT> EXIT
```

Emergency local stop: hold Ctrl+Alt and press Esc.

Because a foreground child owns the CPU while running, the agent cannot
process its hotkey until the child returns. Maintain access to emulator stop
controls, a physical keyboard, reset, or power as appropriate for the lab.

## Logs and audit

The bridge does not yet implement a persistent audit log. MCP clients may
record tool calls and results according to their own policies. Treat those
records as sensitive because they can contain:

- screen contents;
- commands and filenames;
- machine identities and network addresses;
- operational timing;
- possibly secrets entered by mistake.

Never log the protocol password or raw key. Diagnostics belong on stderr;
stdout must remain reserved for MCP stdio.

## Incident response

If unexpected remote input is observed:

1. use the local stop;
2. isolate the DOS machine's network;
3. stop the bridge/simulator;
4. rotate the target credential;
5. inspect MCP-client history and host logs;
6. verify files and boot configuration locally;
7. report a suspected software vulnerability through
   [SECURITY.md](../SECURITY.md), not a public issue.
