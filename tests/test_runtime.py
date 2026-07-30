import json
import logging
import time

import pytest

import dos_mcp.runtime as runtime_module
from dos_mcp.discovery import DiscoveredTarget, DiscoveryAdvertisement
from dos_mcp.protocol import OPEN_MODE_KEY, Capability, derive_password_key
from dos_mcp.runtime import BackendRuntime


def configure_target(monkeypatch: pytest.MonkeyPatch) -> dict[str, object]:
    captured: dict[str, object] = {}

    def fake_udp_backend(**kwargs: object) -> object:
        captured.update(kwargs)
        return object()

    monkeypatch.setenv("DOS_MCP_TARGET", "192.0.2.10:21301")
    monkeypatch.delenv("DOS_MCP_KEY", raising=False)
    monkeypatch.delenv("DOS_MCP_PASSWORD", raising=False)
    monkeypatch.setattr(runtime_module, "UdpBackend", fake_udp_backend)
    return captured


def test_runtime_derives_key_from_password(monkeypatch: pytest.MonkeyPatch) -> None:
    captured = configure_target(monkeypatch)
    monkeypatch.setenv("DOS_MCP_PASSWORD", "a much easier credential")

    BackendRuntime().get()

    assert captured == {
        "target": ("192.0.2.10", 21301),
        "key": derive_password_key("a much easier credential"),
        "allow_file_read": False,
        "allow_file_write": False,
    }


def test_runtime_allows_explicit_open_mode(
    monkeypatch: pytest.MonkeyPatch,
    caplog: pytest.LogCaptureFixture,
) -> None:
    captured = configure_target(monkeypatch)

    with caplog.at_level(logging.WARNING):
        BackendRuntime().get()

    assert captured["key"] == OPEN_MODE_KEY
    assert "unauthenticated open mode" in caplog.text


def test_runtime_rejects_key_and_password_together(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    configure_target(monkeypatch)
    monkeypatch.setenv("DOS_MCP_KEY", "00112233445566778899AABBCCDDEEFF")
    monkeypatch.setenv("DOS_MCP_PASSWORD", "password")

    with pytest.raises(RuntimeError, match="only one"):
        BackendRuntime().get()


def test_runtime_rejects_explicit_empty_password(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    configure_target(monkeypatch)
    monkeypatch.setenv("DOS_MCP_PASSWORD", "")

    with pytest.raises(ValueError, match="must not be empty"):
        BackendRuntime().get()


def test_runtime_routes_multiple_named_targets(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    created: list[dict[str, object]] = []

    class FakeUdp:
        def __init__(self, **kwargs: object) -> None:
            created.append(kwargs)

        def close(self) -> None:
            pass

    monkeypatch.delenv("DOS_MCP_TARGET", raising=False)
    monkeypatch.setenv(
        "DOS_MCP_TARGETS",
        json.dumps(
            {
                "desk8088": "192.0.2.8:21300",
                "lab386": "192.0.2.38:22300",
            }
        ),
    )
    monkeypatch.setenv("DOS_MCP_PASSWORD", "shared test password")
    monkeypatch.setattr(runtime_module, "UdpBackend", FakeUdp)

    runtime = BackendRuntime()
    assert [target["selector"] for target in runtime.list_targets()] == [
        "desk8088",
        "lab386",
    ]
    with pytest.raises(RuntimeError, match="target is required"):
        runtime.get()
    runtime.get("lab386")

    assert created[0]["target"] == ("192.0.2.38", 22300)


def test_runtime_routes_a_named_discovered_target(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    created: list[dict[str, object]] = []
    advertisement = DiscoveryAdvertisement(
        name="WORKBENCH",
        port=21300,
        capabilities=Capability.STATUS | Capability.TEXT_CAPTURE,
        agent_id=bytes.fromhex("acde48444d02"),
    )

    class FakeListener:
        def __init__(self, *, port: int) -> None:
            assert port == 21301
            self.pending = [
                DiscoveredTarget(
                    advertisement,
                    ("192.0.2.86", 21300),
                    time.monotonic(),
                )
            ]

        def drain(self) -> tuple[DiscoveredTarget, ...]:
            values = tuple(self.pending)
            self.pending.clear()
            return values

        def close(self) -> None:
            pass

    class FakeUdp:
        def __init__(self, **kwargs: object) -> None:
            created.append(kwargs)

        def close(self) -> None:
            pass

    monkeypatch.delenv("DOS_MCP_TARGET", raising=False)
    monkeypatch.delenv("DOS_MCP_TARGETS", raising=False)
    monkeypatch.setenv("DOS_MCP_DISCOVERY", "1")
    monkeypatch.setenv("DOS_MCP_PASSWORD", "discovery test password")
    monkeypatch.setattr(runtime_module, "DiscoveryListener", FakeListener)
    monkeypatch.setattr(runtime_module, "UdpBackend", FakeUdp)

    runtime = BackendRuntime()
    listed = runtime.list_targets()
    runtime.get("WORKBENCH")

    assert listed[0]["selector"] == "WORKBENCH@acde48444d02"
    assert listed[0]["agent_id"] == "acde48444d02"
    assert 0 <= listed[0]["last_seen_seconds_ago"] < 1
    assert created[0]["target"] == ("192.0.2.86", 21300)
