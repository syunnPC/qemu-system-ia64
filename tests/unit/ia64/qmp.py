"""Small synchronous QMP client used by IA-64 microprogram tests."""

from __future__ import annotations

import json
import select
import time
from collections import deque
from typing import Any, TextIO


class QmpError(RuntimeError):
    pass


class QmpClient:
    def __init__(self, reader: TextIO, writer: TextIO, timeout_s: float = 5.0):
        self.reader = reader
        self.writer = writer
        self.timeout_s = timeout_s
        self.events: deque[dict[str, Any]] = deque()
        self.serial = 0
        self._read_buffer = bytearray()

        greeting = self._read_message(timeout_s)
        if "QMP" not in greeting:
            raise QmpError(f"invalid QMP greeting: {greeting!r}")
        self.execute("qmp_capabilities")

    def _read_message(self, timeout_s: float) -> dict[str, Any]:
        deadline = time.monotonic() + timeout_s
        while True:
            newline = self._read_buffer.find(b"\n")
            if newline >= 0:
                raw = bytes(self._read_buffer[:newline])
                del self._read_buffer[:newline + 1]
                try:
                    line = raw.decode("utf-8").strip()
                except UnicodeDecodeError as exc:
                    raise QmpError(f"invalid UTF-8 in QMP response: {raw!r}") from exc
                if not line:
                    continue
                try:
                    return json.loads(line)
                except json.JSONDecodeError as exc:
                    raise QmpError(f"invalid QMP JSON: {line!r}") from exc

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise QmpError("timed out waiting for QMP response")
            fd = self.reader.fileno()
            ready, _, _ = select.select([fd], [], [], remaining)
            if not ready:
                raise QmpError("timed out waiting for QMP response")
            chunk = self.reader.buffer.read1(65536)
            if not chunk:
                raise QmpError("QMP stream closed unexpectedly")
            self._read_buffer.extend(chunk)

    def _send(self, message: dict[str, Any]) -> None:
        self.writer.write(json.dumps(message, separators=(",", ":")) + "\n")
        self.writer.flush()

    def quit(self) -> None:
        """Request shutdown. QEMU may exit before delivering the reply."""
        self._send({"execute": "quit"})

    def execute(self, command: str, arguments: dict[str, Any] | None = None,
                timeout_s: float | None = None) -> Any:
        self.serial += 1
        ident = f"ia64-{self.serial}"
        request: dict[str, Any] = {"execute": command, "id": ident}
        if arguments:
            request["arguments"] = arguments
        self._send(request)

        deadline = time.monotonic() + (timeout_s or self.timeout_s)
        while True:
            message = self._read_message(max(0.0, deadline - time.monotonic()))
            if "event" in message:
                self.events.append(message)
                continue
            if message.get("id") != ident:
                raise QmpError(f"unexpected QMP response: {message!r}")
            if "error" in message:
                error = message["error"]
                raise QmpError(f"QMP {command} failed: {error!r}")
            if "return" not in message:
                raise QmpError(f"malformed QMP response: {message!r}")
            return message["return"]

    def wait_event(self, name: str, timeout_s: float | None = None) -> dict[str, Any]:
        deadline = time.monotonic() + (timeout_s or self.timeout_s)
        while True:
            for event in tuple(self.events):
                if event.get("event") == name:
                    self.events.remove(event)
                    return event
            message = self._read_message(max(0.0, deadline - time.monotonic()))
            if "event" not in message:
                raise QmpError(f"unexpected response while waiting for {name}: {message!r}")
            if message.get("event") == name:
                return message
            self.events.append(message)

    def hmp(self, command: str, timeout_s: float | None = None) -> str:
        result = self.execute(
            "human-monitor-command", {"command-line": command},
            timeout_s=timeout_s)
        if not isinstance(result, str):
            raise QmpError(f"HMP returned non-string result: {result!r}")
        return result
