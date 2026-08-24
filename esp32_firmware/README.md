# ESP-IDF apps (classic ESP32)

| App | Purpose |
|-----|---------|
| [`ir_bridge`](apps/ir_bridge/) | Wi‑Fi STA + MQTT + HTTP + RMT IR transmit (GPIO18) + learn (GPIO19) |
| [`ir_listen`](apps/ir_listen/) | Receive-only learner on GPIO19; identity `ir-rx-<mac>` |
| [`ir_blast`](apps/ir_blast/) | Transmit-only on GPIO18; identity `ir-tx-<mac>` |
| [`tx_probe`](apps/tx_probe/) | Identify TO-92 E/B/C on GPIO32/33/25 |
| [`tx_pulse`](apps/tx_pulse/) | GPIO18 1 kHz PWM burst for a visible LED / 2N2222 test |

```bash
# from repo root
./scripts/fw idf install
./scripts/fw idf nvs-wifi
./scripts/fw idf upload ir_bridge
./scripts/fw idf monitor ir_bridge
```

Target is **`esp32`** (see the app’s `sdkconfig.defaults`).
