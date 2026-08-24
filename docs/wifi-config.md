# Wi‑Fi and MQTT configuration (secrets-safe)

## Goals

- Device joins a known home network.
- **Passwords never enter git.**
- MQTT broker settings can live next to Wi‑Fi in the same NVS image.

## Layout

```text
secrets/
  wifi.yaml.example     # committed template
  wifi.yaml             # gitignored — real credentials
  mqtt.yaml.example     # committed template
  mqtt.yaml             # gitignored — broker URI / optional user+pass
  generated/            # gitignored — NVS dumps
```

### `secrets/wifi.yaml`

```yaml
---
- ssid: ExampleHome
  password: "correct-horse-battery"
```

Only the **first** entry is written by `./scripts/fw idf nvs-wifi` today.

### `secrets/mqtt.yaml` (optional)

```yaml
---
uri: mqtt://192.168.1.10:1883
username: ""
password: ""
prefix: ir
```

If this file is present, it is packed into the **same** NVS partition as Wi‑Fi.
Flashing NVS replaces the whole partition, so both namespaces are generated
together.

You can also set MQTT later without reflashing:

```bash
curl -X POST http://IR_MC_IP:8080/config/mqtt \
  -H 'content-type: application/json' \
  -d '{"uri":"mqtt://192.168.1.10:1883","prefix":"ir"}'
```

## Host workflow

```bash
cp secrets/wifi.yaml.example secrets/wifi.yaml
cp secrets/mqtt.yaml.example secrets/mqtt.yaml   # optional
$EDITOR secrets/wifi.yaml

./scripts/fw idf nvs-wifi
./scripts/fw idf upload ir_bridge
./scripts/fw idf monitor ir_bridge
```

`ir_bridge` reads NVS namespace `wifi` keys `ssid` and `password`. It does
**not** embed the password in the app binary. HTTP works even if MQTT is unset.
