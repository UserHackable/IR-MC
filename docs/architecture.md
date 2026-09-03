# Architecture

IR-MC is a small always-on ESP32 that sits on the LAN and **transmits IR**.
Phones, computers, and home automation talk to it over **MQTT** (primary) or
**HTTP** (testing / non-MQTT clients). It does not try to be a full remote
emulator on-device; it is a **named command + raw timing** blaster.

```text
  phone / PC / HA
        │
        │  MQTT  ir/<device>/<command>
        │  HTTP  POST /ir/<device>/<command>
        ▼
  ┌─────────────────────────────────┐
  │  ESP32  (ir_bridge)             │
  │   Wi‑Fi STA                     │
  │   catalog (SPIFFS JSON)         │
  │   RMT TX  GPIO18                │
  └──────────────┬──────────────────┘
                 │ NPN / MOSFET
                 ▼
            940 nm IR LED  →  TV / AC / …
```

## Control path

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
| `repeat` | Frame repeats |
| `timings` | Raw microseconds: mark, space, mark, space, … |

## Discovery

UDP broadcast every 5 s on port **3000**, same shape as other UserHackable
ESP32 tools (`role=ir_mc`). HTTP status lives at `http://<ip>/status`.
