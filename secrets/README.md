# Secrets (gitignored)

| File | Purpose |
|------|---------|
| `wifi.yaml` | Real SSIDs/passwords — copy from `wifi.yaml.example` |
| `mqtt.yaml` | Optional broker URI / credentials — copy from `mqtt.yaml.example` |
| `generated/` | NVS bin/csv from `./scripts/fw idf nvs-wifi` |

Never commit real credentials.

`./scripts/fw idf nvs-wifi` writes Wi‑Fi and, if `mqtt.yaml` exists, MQTT
into **one** NVS image. Flashing NVS replaces the whole partition, so both
namespaces are generated together.
