# Security policy

## Supported code

Security fixes currently target the latest `main` branch. No stable release
series or backport policy exists yet.

## Reporting a vulnerability

Prefer GitHub's private vulnerability reporting feature for this repository.
If it is unavailable, contact the repository owner privately through the
contact method on their GitHub profile.

Do not include active production passwords or keys, private screen captures,
or other sensitive machine data unless the maintainer explicitly requests them
through a secure channel.

Please provide:

- affected commit or version;
- backend and transport;
- target DOS/emulator/hardware details where relevant;
- reproduction steps or a minimal packet/test;
- impact and required attacker position;
- whether a credential or public endpoint has already been exposed;
- any suggested mitigation.

Allow reasonable time for triage and a coordinated fix before public
disclosure.

## Immediate response to credential exposure

Protocol credentials cannot be revoked remotely. If a password or raw key is
exposed:

1. stop `RAGENT`, unload `RA-TSR`, or stop the simulator;
2. block UDP access to the target;
3. choose a new high-entropy passphrase or generate a random 16-byte key;
4. update both target and bridge;
5. inspect recent MCP and host activity;
6. remove the credential from public history where practical, without treating
   history rewriting as a substitute for rotation.

## Deployment security

Protocol version 2 is for a trusted private LAN. It authenticates and checks
integrity but does not encrypt. Do not:

- forward the agent UDP port from the public Internet;
- reuse public example credentials;
- assume source IP or MAC address is authentication;
- expose the Linux simulator under a privileged account;
- treat `DOS_MCP_ROOT` as a filesystem sandbox.

See [`docs/security-model.md`](docs/security-model.md) for the design and
[`docs/operations.md`](docs/operations.md) for deployment guidance.

## Out of scope for public reports

The following documented limitations are not by themselves vulnerabilities:

- no confidentiality in protocol version 2;
- no authentication when explicitly running in documented open mode;
- no service while the foreground DOS agent runs a child command;
- BIOS keyboard injection not working with direct-controller software;
- `DOS_MCP_ROOT` being a starting directory rather than containment;
- unauthenticated, spoofable local discovery advertisements;
- documented RA-TSR incompatibility with Windows, protected-mode extenders,
  direct-input software, or another TSR loaded above its interrupt hooks;
- unavailable direct execution, memory, port, and reboot features.

Reports showing a bypass of documented authentication, replay protection,
length checks, capability boundaries, or process isolation are in scope.
