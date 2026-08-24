#!/usr/bin/env python3
"""Append ESP32 IRMSG lines from USB-UART to a rotating logfile.

Haruna: systemd unit ir-mc-serial-log.service.
Stop the service before flashing: sudo systemctl stop ir-mc-serial-log
"""
from __future__ import annotations

import os
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import serial

PORT = os.environ.get("IR_SERIAL_PORT", "/dev/ttyUSB0")
BAUD = int(os.environ.get("IR_SERIAL_BAUD", "115200"))
LOG = Path(os.environ.get("IR_SERIAL_LOG", "/home/rob/User-Hackable/IR-MC/logs/ir-serial.log"))
MAX_BYTES = int(os.environ.get("IR_SERIAL_MAX_BYTES", str(8 * 1024 * 1024)))
KEEP = int(os.environ.get("IR_SERIAL_KEEP", "8"))
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
            ser.timeout = 1
            ser.dsrdtr = False
            ser.rtscts = False
            ser.dtr = False
            ser.rts = False
            ser.open()
            return ser
        except Exception as exc:
            print(f"serial open failed: {exc}", file=sys.stderr, flush=True)
            time.sleep(2)


def main() -> None:
    LOG.parent.mkdir(parents=True, exist_ok=True)
    ser = open_serial()
    buf = b""
    print(f"logging IR from {PORT} -> {LOG}", flush=True)
    while True:
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
            continue
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = ANSI.sub("", raw.decode("utf-8", "replace")).strip()
            if not KEEP_RE.search(line):
                continue
            ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")
            rotate(LOG)
            with LOG.open("a", encoding="utf-8") as fh:
                fh.write(f"{ts} {line}\n")
                fh.flush()


if __name__ == "__main__":
    main()
