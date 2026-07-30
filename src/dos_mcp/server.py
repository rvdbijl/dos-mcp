"""MCP tool presentation for DOS-like targets."""

from __future__ import annotations

import asyncio
import atexit
from collections.abc import Callable
from typing import Annotated, Any

from mcp.server import MCPServer
from mcp.types import ToolAnnotations
from pydantic import Field

from .backend import Backend
from .runtime import BackendRuntime


def create_server(backend: Backend | None = None) -> MCPServer:
    """Construct an MCP server around a backend, without starting a transport."""
    runtime = BackendRuntime(backend)
    server = MCPServer(
        "dos-mcp",
        instructions=(
            "Observe and control a DOS-like text console. Capture the screen before sending "
            "input. Keyboard input mutates the target and may run commands."
        ),
    )
    atexit.register(runtime.close)
    operation_lock = asyncio.Lock()

    async def call_backend(operation: Callable[[], dict[str, Any]]) -> dict[str, Any]:
        async with operation_lock:
            return operation()

    @server.tool(
        name="dos.get_status",
        title="Get DOS target status",
        annotations=ToolAnnotations(read_only_hint=True, open_world_hint=False),
    )
    async def get_status() -> dict[str, Any]:
        """Report target identity, connection state, and current operating phase."""
        return await call_backend(lambda: runtime.get().get_status().to_dict())

    @server.tool(
        name="dos.get_capabilities",
        title="Get DOS target capabilities",
        annotations=ToolAnnotations(read_only_hint=True, open_world_hint=False),
    )
    async def get_capabilities() -> dict[str, Any]:
        """Report exactly which target operations and screen formats are supported."""
        return await call_backend(lambda: runtime.get().get_capabilities().to_dict())

    @server.tool(
        name="dos.capture_screen",
        title="Capture DOS text screen",
        annotations=ToolAnnotations(read_only_hint=True, open_world_hint=False),
    )
    async def capture_screen() -> dict[str, Any]:
        """Capture the full fixed-width text screen, cell attributes, and cursor state."""
        return await call_backend(lambda: runtime.get().capture_screen().to_dict())

    @server.tool(
        name="dos.send_keys",
        title="Send keys to DOS target",
        annotations=ToolAnnotations(
            read_only_hint=False,
            destructive_hint=False,
            idempotent_hint=False,
            open_world_hint=False,
        ),
    )
    async def send_keys(
        text: Annotated[
            str,
            Field(description="UTF-8 text to enter before named keys; limited to 4096 bytes."),
        ] = "",
        keys: Annotated[
            list[str] | None,
            Field(
                description=(
                    "Named keys such as ENTER, ESC, TAB, arrows, CTRL_C, or F1 through F12."
                ),
                max_length=128,
            ),
        ] = None,
        inter_key_delay_ms: Annotated[
            int,
            Field(ge=0, le=1000, description="Delay between text bytes and named keys."),
        ] = 0,
        settle_ms: Annotated[
            int,
            Field(ge=0, le=2000, description="How long to collect resulting terminal output."),
        ] = 100,
    ) -> dict[str, Any]:
        """Inject text and named keys into the target's input queue."""
        return await call_backend(
            lambda: (
                runtime.get()
                .send_keys(
                    text=text,
                    keys=tuple(keys or ()),
                    inter_key_delay_ms=inter_key_delay_ms,
                    settle_ms=settle_ms,
                )
                .to_dict()
            )
        )

    return server


mcp = create_server()


def main() -> None:
    """Run the default server over MCP stdio."""
    mcp.run()


if __name__ == "__main__":
    main()
