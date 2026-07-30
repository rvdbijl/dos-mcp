"""Lazy backend ownership for the stdio server."""

from __future__ import annotations

import os
from pathlib import Path

from .backend import Backend
from .backends import LinuxTerminalBackend, UdpBackend
from .protocol import parse_key
from .protocol.constants import DEFAULT_PORT


class BackendRuntime:
    def __init__(self, backend: Backend | None = None) -> None:
        self._backend = backend

    def get(self) -> Backend:
        if self._backend is None:
            target = os.environ.get("DOS_MCP_TARGET")
            if target:
                key_value = os.environ.get("DOS_MCP_KEY")
                if not key_value:
                    raise RuntimeError("DOS_MCP_KEY is required with DOS_MCP_TARGET")
                host, separator, raw_port = target.rpartition(":")
                if not separator:
                    host, raw_port = target, str(DEFAULT_PORT)
                self._backend = UdpBackend(
                    target=(host, int(raw_port)),
                    key=parse_key(key_value),
                )
            else:
                root = Path(os.environ.get("DOS_MCP_ROOT", os.getcwd()))
                shell = os.environ.get("DOS_MCP_SHELL", "/bin/sh")
                self._backend = LinuxTerminalBackend(root=root, shell=shell)
        return self._backend

    def close(self) -> None:
        if self._backend is not None:
            self._backend.close()
