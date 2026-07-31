# Development status

Last updated: 2026-07-31

## RA-TSR memory milestone

The split resident build is in the final layout-validation pass. The loader
(`RA-TSR.EXE`) performs configuration and then overlays the resident core
(`RA-RES.EXE`), so command-line parsing and large transient buffers do not
remain resident.

Exact PSP retention is implemented from the linker/runtime extent. The core
also shares its mutually exclusive request and screen workspaces. The current
DOSBox-X map retains about 74 KiB including the PSP, with an instrumented
32 KiB private stack and a 4,229-byte shared workspace. The multiplex memory
query reports paragraph count, stack high-water use, guard state, and workspace
size.

## Acceptance gates

- complete the full DOSBox-X load/query, watchdog, text/VGA capture, keyboard,
  file round-trip, and unload flow;
- record worst-case stack use and select a smaller guarded stack for the next
  build;
- run Python tests, static checks, 16-bit builds, and DOS self-tests;
- refresh the tracked `bin/` commissioning bundle and documentation.

## Deployment topology

The Linux bridge and DOS machines are intended to communicate only on the
isolated local LAN used for commissioning. The server has no Internet-facing
listener, route-dependent feature, or outbound Internet requirement.

PicoMEM-specific work remains outside this milestone.
