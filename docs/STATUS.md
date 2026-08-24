# Project status (snapshot)

Last updated: **2026-08-23**.

## Done

- Product intent documented: network-addressable IR transceiver (IR-MC)
- ESP-IDF multi-app layout + `./scripts/fw` for classic **esp32**
- App **`ir_bridge` (0.2.0)**: NVS Wi‑Fi, UDP announce, HTTP `:8080`, MQTT, RMT TX, SPIFFS catalog, TSOP learn
- Apps **`ir_listen`** (RX-only, identity `ir-rx-<mac>`) and **`ir_blast`** (TX-only, `ir-tx-<mac>`)
- Named commands (NEC / Samsung / Sony / raw timings), add/delete without reflashing
- Learn on GPIO19; RMT RX stays armed while idle (do not restart receive on timeout)
- Sceptre volume learned as Sony 12-bit addr 1 / cmd 18 (up) and 19 (down); blaster replay heard by listener
- Secrets-safe Wi‑Fi + optional MQTT NVS provision

## Next (bench)

1. Keep the listen module next to the TV (Haruna USB); blast from GPIO18 through the transistor
2. Learn remaining Sceptre keys (power, …) and PUT them on the blaster catalog
3. Confirm the TV actually changes volume / power from `POST /ir/sceptre/…`

## Not in scope (v0)

- Raw photodiode carrier measurement
- 850 nm second LED
- TV-B-Gone blast-all
- Home Assistant MQTT discovery
- OTA firmware updates
