"""Command-line Linux-backed UDP DOS-agent simulator."""

from __future__ import annotations

import argparse
import logging
import signal
import threading
from pathlib import Path

from .agent_server import UdpAgentServer
from .backends import LinuxTerminalBackend
from .protocol import parse_key
from .protocol.constants import DEFAULT_PORT


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default=f"127.0.0.1:{DEFAULT_PORT}")
    parser.add_argument("--key", required=True, help="32 hexadecimal PSK characters")
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--shell", default="/bin/sh")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    host, raw_port = args.bind.rsplit(":", 1)
    stop = threading.Event()
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    backend = LinuxTerminalBackend(root=args.root, shell=args.shell)
    server = UdpAgentServer(
        backend,
        key=parse_key(args.key),
        bind=(host, int(raw_port)),
    )
    logging.basicConfig(level=logging.INFO)
    logging.info("DOS agent simulator listening on %s:%s", *server.address)
    try:
        server.serve_forever(stop)
    finally:
        server.close()


if __name__ == "__main__":
    main()
