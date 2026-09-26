# Control API

Default topic prefix is `ir`. HTTP listens on port **80** when Wi-Fi is up.
The Haruna board is reached over USB instead. See [STATUS.md](STATUS.md).

## USB

`ir-mc-serial-log.service` on Haruna listens on `/run/ir-mc/blast.sock` (mode `0666`) and owns `/dev/ttyUSB0`. One JSON object per line, same body as `POST /ir/send`. The bridge returns one JSON line. The board's own line is prefixed `IRACK `.

```json
{"protocol":"sony","address":1,"command":21,"bits":12,"freq":40000,"repeat":5}
```

`{"op":"status"}` does not transmit. Response includes `"ok":true` and `"tx":true`.

Stop the service before flashing. CP2102 boards need `idf.py -b 115200`.

## HTTP

| Method | Path | Body | Action |
|--------|------|------|--------|
| GET | `/` | — | Phone remote UI (Sceptre TV + soundbar) |
| GET | `/lab` | — | Learn / raw / ADC lab UI |
| GET | `/status` | — | Identity, IP, MQTT state |
| GET | `/health` | — | `ok` |
| GET | `/devices` | — | Full catalog JSON |
| GET | `/commands` | — | Same as `/devices` |
| POST | `/ir/<device>/<command>` | optional `{"repeat":2,"duty":0.15}` | Send named command; response includes TSOP echo |
| POST | `/ir/send` | JSON (named, protocol, or raw) | Send; same echo payload |
| POST | `/ir/probe` | optional `{"duty":0.15,"repeat":3}` | Dummy Sony addr 31 (TV ignores). Reports whether TK15 saw clean vs glued/jammed frames — close-range proxy for the TV decoder |
| GET/POST | `/ir/duty` | POST `{"duty":0.33}` | Carrier duty cycle (optical brightness). Default 0.33; clamp 0.05–0.50 |
| POST | `/ir/learn` | optional `{"timeout_ms":8000,"device":"sceptre","command":"power"}` | Capture next remote press (includes `tk64` analog stats) |
| GET/POST | `/adc/led` | — | GPIO18 DC on/off, ADC GPIO32 millivolts (÷2 tap) |
| GET | `/adc/tk64` | — | TK64 phototransistor millivolts on GPIO34 |
| PUT | `/commands` | JSON with `device`, `command`, code fields | Add / update and persist |
| DELETE | `/commands` | `{"device":"tv","command":"power"}` | Remove and persist |
| POST | `/config/mqtt` | `{"uri":"mqtt://host:1883","prefix":"ir",...}` | Save broker to NVS and reconnect |
| POST | `/config/net` | `{"hostname":"ir","ip":"192.168.72.20","gateway":"192.168.72.1","netmask":"255.255.255.0"}` | Save static IP / hostname to NVS and reboot. Empty `ip` returns to DHCP. |

### POST `/ir/send` examples

Named:

```json
{"device":"demo","command":"power"}
```

NEC:

```json
{"protocol":"nec","address":4,"command":8,"freq":38000,"repeat":2}
```

NEC `repeat` is 1 full data frame plus (`repeat`−1) AGC bursts (9 ms / 2.25 ms)
on a 110 ms period, not copies of the 32-bit frame. Soundbar keys use 4.

Raw timings (microseconds, mark/space pairs):

```json
{"freq":38000,"timings":[9000,4500,560,1690,560,560],"repeat":1}
```

`duty` on any send/probe is a one-shot override (restored after the blast). Every TX response includes:

```json
{
  "ok": true,
  "echo_quality": "clean",
  "too_close": false,
  "duty": 0.33,
  "echo": [{"protocol":"sony","address":31,"command":0,"bits":12,"n":25}],
  "tk64": {"gpio":34,"idle_mv":142,"min_mv":142,"max_mv":142,"no_swing":true}
}
```

`echo_quality`: `clean` (separated frames, decoded), `glued` (repeats ran together), `jammed` (RX buffer filled), `distorted` (garbled), `no_echo` (TK15 heard nothing). `too_close` is true when the local TSOP — same distance from the LED as the TV window — did not see a clean burst. Lower `duty` or move the emitter.

```bash
curl -X POST http://IR_MC_IP/ir/demo/power
curl -X POST http://IR_MC_IP/ir/send \
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
curl -X PUT http://IR_MC_IP/commands \
  -H 'content-type: application/json' \
  -d '{"device":"tv","command":"power","protocol":"nec","address":32,"command":16,"freq":38000,"repeat":2}'
```

The catalog is stored at `/spiffs/codes.json`. See [codes.example.json](../data/codes.example.json).
