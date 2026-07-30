# Contributing

Thank you for helping make real DOS systems safely manageable from modern
tools.

Read these before changing code:

1. [`PROJECT.md`](PROJECT.md) — complete project brief;
2. [`AGENTS.md`](AGENTS.md) — concise non-negotiable constraints;
3. [`docs/architecture.md`](docs/architecture.md) — layer boundaries;
4. [`docs/security-model.md`](docs/security-model.md) — trust and policy;
5. [`docs/development.md`](docs/development.md) — implementation workflow.

## Scope

The current codebase includes distinct foreground `RAGENT` and resident
`RA-TSR` targets. Preserve both paths; do not turn the foreground program
into a compatibility alias or move DOS work into callbacks. Resident changes
need explicit reentrancy, memory-lifetime, unload, and hardware-verification
analysis. Keep PicoMEM/PicoMEM2 work out unless a task explicitly opens that
milestone.

## Development setup

```bash
uv sync
uv run ruff check .
uv run python tools/check_docs.py
uv run pytest
```

DOS changes also require:

```bash
make -C dos WATCOM=/path/to/watcom all
```

When available, run the full emulator workflow described in
[`docs/testing.md`](docs/testing.md).

## Pull requests

Keep changes focused and explain:

- the user-visible or protocol behavior;
- safety consequences;
- tests run;
- platforms actually verified;
- remaining assumptions;
- documentation updated.

For wire-format changes, include synchronized Python and C vectors. For new
mutations, document retry/idempotency behavior and authorization at both the
bridge and target.

Do not claim physical 8088, adapter, BIOS, or DOS-version support from
emulator evidence alone.

## Coding expectations

- Preserve the backend/MCP/transport/target separation.
- Treat remote and target data as untrusted.
- Keep DOS callbacks bounded and free of DOS calls.
- Preserve 8088 instruction compatibility.
- Use fixed buffers and validate arithmetic before copying.
- Keep stdout clean for MCP.
- Add tests at the lowest useful layer and end-to-end where risk warrants it.
- Update the documentation index when adding a guide.

## Commit hygiene

Before proposing a change:

```bash
uv run ruff check .
uv run python tools/check_docs.py
uv run pytest
git diff --check
```

Do not commit:

- `.venv`, caches, or DOS build products;
- packet-driver binaries;
- production protocol keys;
- generated emulator logs;
- unrelated local changes.

## Security issues

Do not open public issues for vulnerabilities or exposed keys. Follow
[`SECURITY.md`](SECURITY.md).

## Licensing

A final repository license has not yet been selected. Avoid adding copied
code, fonts, packet drivers, or other third-party assets until their license
and the project's licensing direction are documented.
