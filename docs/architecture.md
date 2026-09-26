# Architecture

IR-MC is a small always-on ESP32 that **transmits IR**. The Haruna board
is a classic ESP32 (2.4 GHz only). Shop SSID `Ferney` is 5 GHz only, so that
board never joins the LAN. Mixomancer drives it over USB. MQTT and HTTP are
still in the firmware for a board that can associate. It is a **named command
+ raw timing** blaster, not a full on-device remote emulator.

```text
  Mixomancer Remotes                         phone / HA
        │                                    │
        │  /run/ir-mc/blast.sock             │  MQTT or HTTP
        ▼                                    │  (only if the chip
  ir-mc-serial-log  ── UART JSON line ──┐    │   can hear the AP)
                                         ▼    ▼
                                  ┌─────────────────────────────────┐
                                  │  ESP32  (ir_bridge)             │
                                  │   serial_cmd + optional Wi‑Fi   │
                                  │   RMT TX  GPIO18                │
                                  └──────────────┬──────────────────┘
                                                 │
                                                 ▼
                                            940 nm IR LED  →  TV / AC / …
```

## Control path

USB (the path that works on Haruna):

1. Mixomancer writes one JSON line to `/run/ir-mc/blast.sock`.
2. `ir-mc-serial-log` owns `/dev/ttyUSB0` and writes that line to the UART.
3. `serial_cmd` parses it and calls `ir_execute_json`, or answers `{"op":"status"}`.
4. The board prints `IRACK ` plus one JSON object. The bridge returns that JSON to the socket client.

Wi-Fi, when the chip can actually hear the AP:

1. Message arrives (MQTT topic or HTTP POST).
2. If it names `device/command`, look up JSON in the catalog.
3. If it is raw / protocol JSON, encode that instead.
4. RMT generates mark/space with the **per-code carrier frequency**.
5. ACK goes to MQTT `ir/ack` and to the HTTP response body.

## Catalog

Compiled-in `demo` codes exist so the firmware is testable with no SPIFFS file.
The first `PUT /commands` (or MQTT-equivalent later) writes `/spiffs/codes.json`.
After that, the on-flash file wins at boot.

Protocol objects:

| Field | Meaning |
|-------|---------|
| `protocol` | `nec`, `samsung`, `sony`/`sirc`, or `raw` |
| `address` / `command` | Protocol fields (integer) |
| `freq` | Carrier Hz (default 38000, Sony 40000) |
| `repeat` | Sony/Samsung: full-frame copies. NEC: 1 data frame + (`repeat`−1) AGC bursts (9/2.25 ms) on a 110 ms period. Soundbar keys use 4. |
| `timings` | Raw microseconds: mark, space, mark, space, … |

## Discovery

UDP broadcast every 5 s on port **3000**, same shape as other UserHackable
ESP32 tools (`role=ir_mc`). HTTP status lives at `http://<ip>/status`.
