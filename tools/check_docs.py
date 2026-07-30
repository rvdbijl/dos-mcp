"""Validate repository-local links in Markdown documentation."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote

LINK = re.compile(r"\[[^\]]*]\(([^)]+)\)")
SKIP_PARTS = {".git", ".venv", ".pytest_cache", ".ruff_cache"}


def markdown_files(root: Path) -> list[Path]:
    return sorted(
        path
        for path in root.rglob("*.md")
        if not SKIP_PARTS.intersection(path.parts)
    )


def local_target(source: Path, raw_target: str) -> Path | None:
    target = raw_target.strip().split(maxsplit=1)[0].strip("<>")
    if (
        not target
        or target.startswith(("#", "http://", "https://", "mailto:"))
    ):
        return None
    path_text = unquote(target.split("#", 1)[0])
    return (source.parent / path_text).resolve()


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    failures: list[str] = []
    files = markdown_files(root)
    for source in files:
        content = source.read_text(encoding="utf-8")
        for match in LINK.finditer(content):
            target = local_target(source, match.group(1))
            if target is not None and not target.exists():
                relative_source = source.relative_to(root)
                failures.append(
                    f"{relative_source}: missing target {match.group(1)!r}"
                )
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"PASS: {len(files)} Markdown files have valid local links")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
