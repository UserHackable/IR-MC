# Control API

Default topic prefix is `ir`. HTTP listens on port **8080**.

## HTTP

| Method | Path | Body | Action |
|--------|------|------|--------|
| GET | `/` | — | Simple test UI |
| GET | `/status` | — | Identity, IP, MQTT state |
| GET | `/health` | — | `ok` |
| GET | `/devices` | — | Full catalog JSON |
| GET | `/commands` | — | Same as `/devices` |
| POST | `/ir/<device>/<command>` | optional `{"repeat":2}` | Send named command |
| POST | `/ir/send` | JSON (named, protocol, or raw) | Send |
| POST | `/ir/learn` | optional `{"timeout_ms":8000,"device":"sceptre","command":"power"}` | Capture next remote press (includes `tk64` analog stats) |
| GET/POST | `/adc/led` | — | GPIO18 DC on/off, ADC GPIO32 millivolts (÷2 tap) |
| GET | `/adc/tk64` | — | TK64 phototransistor millivolts on GPIO34 |
| PUT | `/commands` | JSON with `device`, `command`, code fields | Add / update and persist |
| DELETE | `/commands` | `{"device":"tv","command":"power"}` | Remove and persist |
| POST | `/config/mqtt` | `{"uri":"mqtt://host:1883","prefix":"ir",...}` | Save broker to NVS and reconnect |

### POST `/ir/send` examples

Named:

```json
{"device":"demo","command":"power"}
```

NEC:

```json
{"protocol":"nec","address":4,"command":8,"freq":38000,"repeat":2}
```

Raw timings (microseconds, mark/space pairs):

```json
{"freq":38000,"timings":[9000,4500,560,1690,560,560],"repeat":1}
```

```bash
curl -X POST http://IR_MC_IP:8080/ir/demo/power
curl -X POST http://IR_MC_IP:8080/ir/send \
  -H 'content-type: application/json' \
  -d '{"protocol":"nec","address":4,"command":8}'
```

## MQTT

Subscribe: `ir/#` (or `<prefix>/#`).

| Topic | Payload | Action |
|-------|---------|--------|
| `ir/<device>/<command>` | empty or `{"repeat":2}` | Named catalog lookup |
| `ir/send` | same JSON as HTTP `/ir/send` | Send |
| `ir/raw` | `{"freq":38000,"timings":[…]}` | Raw transmit |
| `ir/status` | retained JSON | Module status (published, ignored on receive) |
| `ir/ack` | JSON | Last command result (published, ignored on receive) |

LWT: retained `{"online":false}` on `ir/status` if the broker supports it.

```bash
mosquitto_pub -h 192.168.1.10 -t ir/demo/power -m ''
mosquitto_pub -h 192.168.1.10 -t ir/raw \
  -m '{"freq":38000,"timings":[9000,4500,560,1690,560,560]}'
```

## Adding a code without reflashing

```bash
curl -X PUT http://IR_MC_IP:8080/commands \
  -H 'content-type: application/json' \
  -d '{"device":"tv","command":"power","protocol":"nec","address":32,"command":16,"freq":38000,"repeat":2}'
```

The catalog is stored at `/spiffs/codes.json`. See [codes.example.json](../data/codes.example.json).
