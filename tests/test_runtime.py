import logging

import pytest

import dos_mcp.runtime as runtime_module
from dos_mcp.protocol import OPEN_MODE_KEY, derive_password_key
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
