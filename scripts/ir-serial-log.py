#!/usr/bin/env python3
"""Own the ESP32 USB UART: log IR lines, and blast JSON commands from a socket.

Mixomancer writes one JSON object per line to the socket. This process writes
that line to the board and returns the IRACK JSON line.

Haruna: systemd unit ir-mc-serial-log.service.
Stop the service before flashing: sudo systemctl stop ir-mc-serial-log
"""
from __future__ import annotations

import os
import re
import select
import socket
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import serial

PORT = os.environ.get("IR_SERIAL_PORT", "/dev/ttyUSB0")
BAUD = int(os.environ.get("IR_SERIAL_BAUD", "115200"))
LOG = Path(os.environ.get("IR_SERIAL_LOG", "/home/rob/User-Hackable/IR-MC/logs/ir-serial.log"))
SOCK_PATH = os.environ.get("IR_BLAST_SOCK", "/run/ir-mc/blast.sock")
MAX_BYTES = int(os.environ.get("IR_SERIAL_MAX_BYTES", str(8 * 1024 * 1024)))
KEEP = int(os.environ.get("IR_SERIAL_KEEP", "8"))
ACK_WAIT = float(os.environ.get("IR_ACK_WAIT", "8"))
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
KEEP_RE = re.compile(r"IRMSG |ir_rx:")


def rotate(path: Path) -> None:
    if not path.exists() or path.stat().st_size < MAX_BYTES:
        return
    for i in range(KEEP - 1, 0, -1):
        src = path.with_name(f"{path.name}.{i}")
        dst = path.with_name(f"{path.name}.{i + 1}")
        if src.exists():
            src.replace(dst)
    path.replace(path.with_name(f"{path.name}.1"))


def open_serial():
    while True:
        try:
            os.system(f"stty -F {PORT} {BAUD} raw -echo -hupcl >/dev/null 2>&1")
            ser = serial.Serial()
            ser.port = PORT
            ser.baudrate = BAUD
            ser.timeout = 0
            ser.dsrdtr = False
            ser.rtscts = False
            ser.dtr = False
            ser.rts = False
            ser.open()
            return ser
        except Exception as exc:
            print(f"serial open failed: {exc}", file=sys.stderr, flush=True)
            time.sleep(2)


def log_line(line: str) -> None:
    if not KEEP_RE.search(line):
        return
    ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")
    rotate(LOG)
    with LOG.open("a", encoding="utf-8") as fh:
        fh.write(f"{ts} {line}\n")
        fh.flush()


def reply(conn: socket.socket, payload: bytes) -> None:
    try:
        if not payload.endswith(b"\n"):
            payload += b"\n"
        conn.sendall(payload)
    except OSError:
        pass
    try:
        conn.close()
    except OSError:
        pass


def open_socket() -> socket.socket:
    path = Path(SOCK_PATH)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        path.unlink()
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(str(path))
    os.chmod(path, 0o666)
    srv.listen(4)
    srv.setblocking(False)
    return srv


def take_client(srv: socket.socket, ser: serial.Serial, pending: dict) -> None:
    try:
        conn, _addr = srv.accept()
    except OSError:
        return
    conn.settimeout(1.0)
    buf = b""
    try:
        while b"\n" not in buf and len(buf) < 2048:
            chunk = conn.recv(512)
            if not chunk:
                break
            buf += chunk
    except OSError:
        reply(conn, b'{"ok":false,"error":"read"}')
        return
    line = buf.split(b"\n", 1)[0].strip()
    if not line.startswith(b"{"):
        reply(conn, b'{"ok":false,"error":"json"}')
        return
    if pending.get("conn") is not None:
        reply(conn, b'{"ok":false,"error":"busy"}')
        return
    try:
        ser.write(line + b"\n")
        ser.flush()
    except Exception as exc:
        print(f"serial write failed: {exc}", file=sys.stderr, flush=True)
        reply(conn, b'{"ok":false,"error":"serial"}')
        return
    pending["conn"] = conn
    pending["deadline"] = time.monotonic() + ACK_WAIT


def finish_pending(pending: dict, payload: bytes) -> None:
    conn = pending.get("conn")
    pending["conn"] = None
    pending["deadline"] = 0.0
    if conn is not None:
        reply(conn, payload)


def main() -> None:
    LOG.parent.mkdir(parents=True, exist_ok=True)
    ser = open_serial()
    srv = open_socket()
    buf = b""
    pending: dict = {"conn": None, "deadline": 0.0}
    print(f"IR bridge {PORT} -> {LOG}; commands on {SOCK_PATH}", flush=True)
    while True:
        if pending["conn"] is not None and time.monotonic() > pending["deadline"]:
            finish_pending(pending, b'{"ok":false,"error":"timeout"}')
        timeout = 0.5
        if pending["conn"] is not None:
            timeout = max(0.05, pending["deadline"] - time.monotonic())
        try:
            readable, _, _ = select.select([ser.fileno(), srv], [], [], timeout)
        except (OSError, ValueError) as exc:
            print(f"select failed: {exc}", file=sys.stderr, flush=True)
            time.sleep(1)
            try:
                ser.close()
            except Exception:
                pass
            ser = open_serial()
            buf = b""
            finish_pending(pending, b'{"ok":false,"error":"serial"}')
            continue
        if srv in readable:
            take_client(srv, ser, pending)
        if ser.fileno() not in readable:
            continue
        try:
            chunk = ser.read(1024)
        except Exception as exc:
            print(f"serial read failed: {exc}", file=sys.stderr, flush=True)
            try:
                ser.close()
            except Exception:
                pass
            time.sleep(1)
            ser = open_serial()
            buf = b""
            finish_pending(pending, b'{"ok":false,"error":"serial"}')
            continue
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = ANSI.sub("", raw.decode("utf-8", "replace")).strip()
            if not line:
                continue
            marker = "IRACK "
            idx = line.find(marker)
            if idx >= 0 and pending["conn"] is not None:
                finish_pending(pending, line[idx + len(marker) :].encode())
                continue
            log_line(line)


if __name__ == "__main__":
    main()
