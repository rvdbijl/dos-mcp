"""Lazy backend ownership for the stdio server."""

from __future__ import annotations

import logging
import os
from pathlib import Path

from .backend import Backend
from .backends import LinuxTerminalBackend, UdpBackend
from .protocol import OPEN_MODE_KEY, derive_password_key, parse_key
from .protocol.constants import DEFAULT_PORT


class BackendRuntime:
    def __init__(self, backend: Backend | None = None) -> None:
        self._backend = backend

    def get(self) -> Backend:
        if self._backend is None:
            target = os.environ.get("DOS_MCP_TARGET")
            if target:
                key_value = os.environ.get("DOS_MCP_KEY")
                password = os.environ.get("DOS_MCP_PASSWORD")
                if key_value is not None and password is not None:
                    raise RuntimeError(
                        "set only one of DOS_MCP_KEY and DOS_MCP_PASSWORD"
                    )
                if key_value is not None:
                    key = parse_key(key_value)
                elif password is not None:
                    key = derive_password_key(password)
                else:
                    key = OPEN_MODE_KEY
                    logging.getLogger(__name__).warning(
                        "DOS_MCP_KEY and DOS_MCP_PASSWORD are unset; "
                        "using unauthenticated open mode"
                    )
                host, separator, raw_port = target.rpartition(":")
                if not separator:
                    host, raw_port = target, str(DEFAULT_PORT)
                self._backend = UdpBackend(
                    target=(host, int(raw_port)),
                    key=key,
                )
            else:
                root = Path(os.environ.get("DOS_MCP_ROOT", os.getcwd()))
                shell = os.environ.get("DOS_MCP_SHELL", "/bin/sh")
                self._backend = LinuxTerminalBackend(root=root, shell=shell)
        return self._backend

    def close(self) -> None:
        if self._backend is not None:
            self._backend.close()
