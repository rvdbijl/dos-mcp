"""Lazy ownership and target selection for the stdio server."""

from __future__ import annotations

import json
import logging
import os
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .backend import Backend
from .backends import LinuxTerminalBackend, UdpBackend
from .discovery import DISCOVERY_PORT, DiscoveryListener
from .protocol import OPEN_MODE_KEY, derive_password_key, parse_key
from .protocol.constants import DEFAULT_PORT


@dataclass(slots=True)
class _TargetRecord:
    selector: str
    name: str
    source: str
    address: tuple[str, int] | None
    backend: Backend | None = None
    metadata: dict[str, Any] | None = None
    last_seen: float | None = None

    def to_dict(self) -> dict[str, Any]:
        value = {
            "selector": self.selector,
            "name": self.name,
            "source": self.source,
        }
        if self.address is not None:
            value["host"], value["port"] = self.address
        if self.metadata:
            value.update(self.metadata)
        if self.last_seen is not None:
            value["last_seen_seconds_ago"] = max(
                0.0, time.monotonic() - self.last_seen
            )
        value["connected"] = self.backend is not None
        return value


class BackendRuntime:
    """Resolve one or many logical targets without leaking routing into tools."""

    def __init__(self, backend: Backend | None = None) -> None:
        self._records: dict[str, _TargetRecord] = {}
        if backend is not None:
            self._records["default"] = _TargetRecord(
                selector="default",
                name="default",
                source="injected",
                address=None,
                backend=backend,
            )
        self._configured = backend is not None
        self._key: bytes | None = None
        self._allow_file_read = False
        self._allow_file_write = False
        self._discovery: DiscoveryListener | None = None

    def get(self, target: str | None = None) -> Backend:
        self._configure()
        self._refresh_discovery()
        record = self._resolve(target)
        if record.backend is None:
            if record.address is None or self._key is None:
                raise RuntimeError(f"target {record.selector!r} has no backend")
            record.backend = UdpBackend(
                target=record.address,
                key=self._key,
                allow_file_read=self._allow_file_read,
                allow_file_write=self._allow_file_write,
            )
        return record.backend

    def list_targets(self) -> list[dict[str, Any]]:
        self._configure()
        self._refresh_discovery()
        return [
            self._records[selector].to_dict()
            for selector in sorted(self._records, key=str.casefold)
        ]

    def close(self) -> None:
        for record in self._records.values():
            if record.backend is not None:
                record.backend.close()
        if self._discovery is not None:
            self._discovery.close()

    def _configure(self) -> None:
        if self._configured:
            return
        self._configured = True
        self._allow_file_read = _enabled("DOS_MCP_ALLOW_FILE_READ")
        self._allow_file_write = _enabled("DOS_MCP_ALLOW_FILE_WRITE")
        single = os.environ.get("DOS_MCP_TARGET")
        multiple = os.environ.get("DOS_MCP_TARGETS")
        discovery = _enabled("DOS_MCP_DISCOVERY")
        if single and multiple:
            raise RuntimeError("set only one of DOS_MCP_TARGET and DOS_MCP_TARGETS")

        if single or multiple or discovery:
            self._key = _credential_key()
            if single:
                self._add_static("dos", single)
            if multiple:
                self._add_static_targets(multiple)
            if discovery:
                port = int(os.environ.get("DOS_MCP_DISCOVERY_PORT", DISCOVERY_PORT))
                self._discovery = DiscoveryListener(port=port)
            return

        root = Path(os.environ.get("DOS_MCP_ROOT", os.getcwd()))
        shell = os.environ.get("DOS_MCP_SHELL", "/bin/sh")
        self._records["local"] = _TargetRecord(
            selector="local",
            name="local",
            source="linux",
            address=None,
            backend=LinuxTerminalBackend(
                root=root,
                shell=shell,
                allow_file_read=self._allow_file_read,
                allow_file_write=self._allow_file_write,
            ),
        )

    def _add_static_targets(self, value: str) -> None:
        try:
            parsed = json.loads(value)
        except json.JSONDecodeError as exc:
            raise RuntimeError("DOS_MCP_TARGETS must be a JSON object") from exc
        if not isinstance(parsed, dict) or not parsed:
            raise RuntimeError("DOS_MCP_TARGETS must be a nonempty JSON object")
        for name, endpoint in parsed.items():
            if not isinstance(name, str) or not name or not isinstance(endpoint, str):
                raise RuntimeError("DOS_MCP_TARGETS maps names to host:port strings")
            if name in self._records:
                raise RuntimeError(f"duplicate target selector {name!r}")
            self._add_static(name, endpoint)

    def _add_static(self, name: str, endpoint: str) -> None:
        self._records[name] = _TargetRecord(
            selector=name,
            name=name,
            source="static",
            address=_parse_endpoint(endpoint),
        )

    def _refresh_discovery(self) -> None:
        if self._discovery is None:
            return
        for target in self._discovery.drain():
            advertisement = target.advertisement
            selector = advertisement.selector
            metadata = target.to_dict()
            metadata.pop("selector", None)
            metadata.pop("name", None)
            metadata.pop("host", None)
            metadata.pop("port", None)
            metadata.pop("source", None)
            metadata.pop("last_seen_seconds_ago", None)
            existing = self._records.get(selector)
            if existing is None:
                self._records[selector] = _TargetRecord(
                    selector=selector,
                    name=advertisement.name,
                    source="discovery",
                    address=target.address,
                    metadata=metadata,
                    last_seen=target.last_seen,
                )
            else:
                if existing.address != target.address and existing.backend is not None:
                    existing.backend.close()
                    existing.backend = None
                existing.address = target.address
                existing.metadata = metadata
                existing.last_seen = target.last_seen

    def _resolve(self, target: str | None) -> _TargetRecord:
        if target is None:
            if len(self._records) == 1:
                return next(iter(self._records.values()))
            if not self._records:
                raise RuntimeError(
                    "no DOS targets are known; wait for discovery or configure one"
                )
            raise RuntimeError("target is required when multiple DOS systems are known")
        if target in self._records:
            return self._records[target]
        matches = [
            record
            for record in self._records.values()
            if record.name.casefold() == target.casefold()
        ]
        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            raise RuntimeError(
                f"target name {target!r} is ambiguous; use its name@agent-id selector"
            )
        raise RuntimeError(f"unknown DOS target {target!r}")


def _parse_endpoint(value: str) -> tuple[str, int]:
    host, separator, raw_port = value.rpartition(":")
    if not separator:
        host, raw_port = value, str(DEFAULT_PORT)
    if not host:
        raise RuntimeError("target host must not be empty")
    port = int(raw_port)
    if not 1 <= port <= 0xFFFF:
        raise RuntimeError("target port is outside 1-65535")
    return host, port


def _credential_key() -> bytes:
    key_value = os.environ.get("DOS_MCP_KEY")
    password = os.environ.get("DOS_MCP_PASSWORD")
    if key_value is not None and password is not None:
        raise RuntimeError("set only one of DOS_MCP_KEY and DOS_MCP_PASSWORD")
    if key_value is not None:
        return parse_key(key_value)
    if password is not None:
        return derive_password_key(password)
    logging.getLogger(__name__).warning(
        "DOS_MCP_KEY and DOS_MCP_PASSWORD are unset; "
        "using unauthenticated open mode"
    )
    return OPEN_MODE_KEY


def _enabled(name: str) -> bool:
    value = os.environ.get(name, "")
    if value not in ("", "0", "1"):
        raise RuntimeError(f"{name} must be 0 or 1")
    return value == "1"
