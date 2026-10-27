# Adler

Adler is the air-conditioner controller of a NightMare room. It drives an old,
non-connected AC unit through its own infrared protocol, and it closes the loop
that infrared leaves open: a command only counts once the unit is heard
acknowledging it.

It never opens or wires into the AC. It talks to the unit the same way its
remote does, and it learns what the unit is doing from
[Watson](https://github.com/vtmattedi/Watson), the acoustic observer in the
same room, over [NightMareNetwork](https://github.com/vtmattedi/NightMareNetwork).

| Role         | What                                                                                          |
| ------------ | --------------------------------------------------------------------------------------------- |
| **observes** | Watson's fan, compressor and beep; the IR receiver (including the AC's own remote); the DS18B20 |
| **acts**     | IR transmitter, one command at a time, each waiting for its beep                               |
| **decides**  | `AcController`, the temperature policy. Being rebuilt (see [Status](#status))                  |

## Why infrared and sound

IR is fire-and-forget: the transmitter cannot tell whether the unit received a
frame. Adler confirms instead of trusting:

1. A code is queued and sent. Codes go out at least `IR_SEND_INTERVAL_MS`
   (200 ms) apart so the unit can act on each one.
2. The command waits for an acknowledgement beep, reported by Watson as
   `acoustic:beep`.
3. The first beep within `ac:config:beep_timeout` (default 5000 ms, accepted
   range 1000 to 15000 ms) confirms it. Otherwise it times out.

The IR receiver hears the AC's physical remote too. A frame from the remote
enters the same confirmation path, so Adler keeps track of what a person in the
room did by hand.

Commands are refused while Watson's observations are stale or unavailable:
without them, nothing could be confirmed.

## Resources

Watson's observations are bound as NightMare Remote Resources. The local names
belong to Adler. The source device is a runtime binding (persisted, and changed
with NightMare's `SOURCE` verb), never a name in the code.

| Local resource         | Bound to                                 | Type                        |
| ---------------------- | ---------------------------------------- | --------------------------- |
| `ac:input:fan`         | `<device>/acoustic:fan_detected`         | `RemoteSensor<bool>`        |
| `ac:input:compressor`  | `<device>/acoustic:compressor_detected`  | `RemoteSensor<bool>`        |
| `ac:input:beep`        | `<device>/acoustic:beep`                 | `RemoteEvent<std::string>`  |

Fan and compressor are state: usable only while they have a value, are
available and are not stale. The beep is an event: every occurrence matters,
nothing is retained and nothing is replayed after a reconnect.

Adler also publishes:

- `ir:recv`: an event for every frame the receiver decodes, from any remote.
- The room temperature, from the local DS18B20, or mirrored from another
  device on boards without a probe. The source is a runtime choice.
- `ac:config:beep_timeout`, writable within its range.

## Console commands

| Command               | Effect                                                    |
| --------------------- | --------------------------------------------------------- |
| `AC`                  | Sends the AC power code                                   |
| `IR <name or code>`   | Sends any code from the table, e.g. `IR POWER`            |
| `RGB [hex]`           | M5Stack NanoC6 only: sets or reads the RGB LED            |

## IR codes

`src/IrController/IrCodes.h` holds every code Adler knows:

- **The AC**: its own 24-bit pulse-distance protocol at 38 kHz (9000/4550 µs
  header, 600/1700 and 600/550 µs bits, LSB first). Power, +/-, mode, turbo,
  fan, sleep, timer and LED.
- **HY350 projector**: plain NEC, address `0x00`. It sits in the same room, so
  the same transmitter drives it.

A received frame only counts as one of these when both the raw value and the
protocol match.

## Hardware

Every board revision this firmware has run on keeps its pin map in
`include/board.h`, append-only. A unit in the field is the only thing that
knows how it is wired, so that file is the record. Exactly one revision is
selected per PlatformIO environment.

| Environment          | Board                              | Status                         |
| -------------------- | ---------------------------------- | ------------------------------ |
| `esp32c3-supermini`  | ESP32-C3 SuperMini (`BOARD_C3_V1`) | **Deployed**                   |
| `m5stack-nanoc6`     | M5Stack NanoC6 (`BOARD_C6_V1`)     | Onboard IR TX, no receiver     |
| `esp32`              | ESP32 DevKit v1 (`BOARD_ESP32_V1`) | The first Adler, kept buildable |

The deployed unit is powered from the AC's own 5 V and wired through a MycroftY
IO board:

| ESP32-C3 | Through MycroftY | To                          |
| -------- | ---------------- | --------------------------- |
| GPIO7    | IO 4             | IR receiver (TSOP) output   |
| GPIO8    | IO 3             | IR LED, driven at 38 kHz    |
| GPIO10   | IO 1             | DS18B20 data, 4.7 kΩ to 3V3 |

GPIO8 and GPIO9 are C3 strapping pins. If the unit ever stops booting, check
them first (`board.h` has the details).

The first Adler (ESP32 DevKit v1) also measured the unit's supply: a PZEM-004T
energy meter, an ACS712 current sensor and a ZMPT101B voltage sensor. This
firmware no longer drives them, but their wiring is still recorded in
`board.h`.

## Build

Credentials live in `include/creds.h`, which is gitignored. `NightMareConfig.h`
reads the broker from either `MQTT_URI` (one URI, the older format) or
`LOCAL_MQTT_HOST`/`LOCAL_MQTT_PORT` and `REMOTE_MQTT_URL`/`REMOTE_MQTT_PORT`,
plus `ROOT_CA` for TLS. Wi-Fi and the remaining network settings follow
NightMareNetwork.

```text
pio run -e esp32c3-supermini              # deployed board
pio run -e esp32c3-supermini -t upload
pio run -e m5stack-nanoc6
pio run -e esp32
```

The network runs over ESP-NOW to the NightMare Gateway, with remote MQTT
compiled in as the other profile. OTA updates are enabled.
`src/Version/script.py` bumps the firmware version on every build.

### Dependencies

- [NightMareNetwork](https://github.com/vtmattedi/NightMareNetwork): network,
  resources, console, OTA, telemetry
- Arduino-IRremote 4.7.1, vendored in `lib/`
- OneWire and DallasTemperature, for the DS18B20
- Adafruit NeoPixel, NanoC6 only

## Status

- **Done**: IR transmit and receive tasks, beep confirmation with timeout,
  physical-remote tracking, Watson bindings, temperature source selection,
  board revision registry.
- **Being rebuilt**: `AcController`, the policy that decides what to send from
  the observed state and the temperature.
- **Known limits**:
  - Confirmation is "one frame, next beep". Two frames answered by a single
    beep confirm the first and leave the second unresolved.
  - Self-echo filtering is not applied yet. `IR_SELF_ECHO_WINDOW_MS` (400 ms)
    is defined, but the receiver does not check it, so Adler's own frame
    reflecting off a wall can be queued a second time as a physical-remote
    command.
