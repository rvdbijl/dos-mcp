# Local discovery and multiple DOS targets

RA-TSR can advertise its presence while it has no authenticated controller.
The Linux MCP process can combine these announcements with a static
multi-target configuration.

## Safety boundary

Discovery is intentionally unauthenticated. An announcement is only a
candidate address and display label. It never establishes a session, grants
file access, or authorizes keyboard input. The first real operation still
performs the normal credentialed `HELLO` and derives a session key.

The DOS sender uses:

- destination IP `255.255.255.255`;
- destination Ethernet address `ff:ff:ff:ff:ff:ff`;
- IP TTL 1;
- UDP destination port 21301 by default;
- an interval of approximately five seconds;
- no announcements while an authenticated session is active.

The authenticated session expires after approximately 30 seconds without a
valid request. RA-TSR then returns to disconnected announcement mode. Local
broadcast and TTL 1 prevent intended routing beyond the local segment, but
operators should still apply normal firewall rules.

## Advertisement format

All multibyte values are little-endian:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic `DMD2` |
| 4 | 1 | discovery format version, 1 |
| 5 | 1 | agent protocol version |
| 6 | 1 | flags; bit 0 means open mode |
| 7 | 1 | name length, 1–31 |
| 8 | 2 | authenticated service UDP port |
| 10 | 4 | capability flags |
| 14 | 6 | packet-adapter address used as stable agent ID |
| 20 | N | visible ASCII name |
| 20+N | 2 | CRC-16/CCITT-FALSE over all preceding bytes |

The receiver validates magic, versions, flags, bounds, exact length, ASCII,
and CRC before recording a target. It routes to the UDP source address and
the advertised service port; it does not trust an embedded host address.

The adapter address is a collision-resistant lab identifier, not a secret or
proof of identity. Duplicate names remain usable through the canonical
`name@agent-id` selector.

## Bridge configuration

Enable discovery:

```bash
DOS_MCP_DISCOVERY=1 \
DOS_MCP_PASSWORD='shared lab passphrase' \
uv run dos-mcp
```

Use a nondefault listener port only when every sender is configured to match
it (the current DOS build uses 21301):

```bash
DOS_MCP_DISCOVERY=1 DOS_MCP_DISCOVERY_PORT=21301 uv run dos-mcp
```

The listener is nonblocking and drains queued advertisements when a target is
listed or selected. Once discovered, a record remains known while the bridge
runs, including while RA-TSR stops advertising during a connection.

Static targets use a JSON object:

```bash
DOS_MCP_TARGETS='{
  "desk8088": "192.168.10.21:21300",
  "lab386": "192.168.10.38:21300"
}' \
DOS_MCP_PASSWORD='shared lab passphrase' \
uv run dos-mcp
```

`DOS_MCP_TARGET`, `DOS_MCP_TARGETS`, and discovery can cover these cases:

- `DOS_MCP_TARGET`: one backward-compatible UDP target;
- `DOS_MCP_TARGETS`: multiple named static targets;
- discovery alone: dynamic RA-TSR targets;
- `DOS_MCP_TARGETS` plus discovery: fixed and dynamic targets together.

`DOS_MCP_TARGET` and `DOS_MCP_TARGETS` are mutually exclusive.

## MCP selection

Call `dos.list_targets` to obtain selectors and source metadata. Every other
tool accepts an optional `target` string. It may be omitted when exactly one
target is known. When more than one exists, omission is rejected so a
mutation cannot silently go to the wrong machine.

Example:

```json
{
  "target": "WORKBENCH-386@acde48444d02",
  "text": "VER",
  "keys": ["ENTER"]
}
```

The bridge currently uses one configured credential for all UDP targets in a
process. Operators needing per-machine secrets should run separate bridge
processes until a secret-provider/keyring interface is added. A unique
credential per target remains the preferred deployment policy.

## Network caveats

Routers, VLANs, Wi-Fi isolation, host firewalls, virtual NAT engines, and
container namespaces often suppress limited broadcast. In those environments
use `DOS_MCP_TARGETS` or add an explicitly configured relay outside the DOS
agent. Do not replace the limited broadcast with Internet-routable discovery.
