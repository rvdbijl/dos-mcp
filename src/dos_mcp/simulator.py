"""Command-line Linux-backed UDP DOS-agent simulator."""

from __future__ import annotations

import argparse
import logging
import signal
import threading
from pathlib import Path

from .agent_server import UdpAgentServer
from .backends import LinuxTerminalBackend
from .protocol import OPEN_MODE_KEY, derive_password_key, parse_key
from .protocol.constants import DEFAULT_PORT


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default=f"127.0.0.1:{DEFAULT_PORT}")
    credentials = parser.add_mutually_exclusive_group()
    credentials.add_argument("--key", help="raw 32-hex protocol key")
    credentials.add_argument(
        "--password",
        help="passphrase from which to derive the 128-bit protocol key",
    )
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--shell", default="/bin/sh")
    parser.add_argument("--allow-file-read", action="store_true")
    parser.add_argument("--allow-file-write", action="store_true")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    logging.basicConfig(level=logging.INFO)
    host, raw_port = args.bind.rsplit(":", 1)
    stop = threading.Event()
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    backend = LinuxTerminalBackend(
        root=args.root,
        shell=args.shell,
        allow_file_read=args.allow_file_read,
        allow_file_write=args.allow_file_write,
    )
    if args.key is not None:
        key = parse_key(args.key)
    elif args.password is not None:
        key = derive_password_key(args.password)
    else:
        key = OPEN_MODE_KEY
        logging.warning(
            "--key and --password are omitted; using unauthenticated open mode"
        )
    server = UdpAgentServer(
        backend,
        key=key,
        bind=(host, int(raw_port)),
    )
    logging.info("DOS agent simulator listening on %s:%s", *server.address)
    try:
        server.serve_forever(stop)
    finally:
        server.close()


if __name__ == "__main__":
    main()
