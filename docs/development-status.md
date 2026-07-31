# Development status

Last updated: 2026-07-31

## RA-TSR memory milestone

The split resident build is in the final layout-validation pass. The loader
(`RA-TSR.EXE`) performs configuration and then overlays the resident core
(`RA-RES.EXE`), so command-line parsing and large transient buffers do not
remain resident.

Exact PSP retention is implemented from the linker/runtime extent. The core
also shares its mutually exclusive request and screen workspaces. The current
DOSBox-X build retains 70,032 bytes including the PSP, with an instrumented
32 KiB private stack and a 4,229-byte shared workspace. The multiplex memory
query reports paragraph count, stack high-water use, guard state, and workspace
size.

The full resident integration workload passes. It includes a regression probe
which presents an invalid file-operation packet before a valid request; the
invalid packet is discarded instead of occupying the single receive slot.

## Acceptance gates

- record worst-case stack use and select a smaller guarded stack for the next
  build (the current emulator workload peaks at 322 bytes);
- repeat the workload on representative physical systems.

## Deployment topology

The Linux bridge and DOS machines are intended to communicate only on the
isolated local LAN used for commissioning. The server has no Internet-facing
listener, route-dependent feature, or outbound Internet requirement.

PicoMEM-specific work remains outside this milestone.
