# levoit-matter

Local Matter firmware for Levoit Core air purifiers. No cloud, no VeSync, no Home Assistant. Pairs with Apple Home by QR code like a shop-bought device.

It replaces the firmware on the purifier's own ESP32. Nothing extra goes inside.

**Status:** running on a Core 300S in Apple Home. The other models build from the same code and have never been flashed.

## Models

| Model | Speeds | Auto | Air quality | State |
|---|---|---|---|---|
| Core 300S | 3 | Yes | Yes | Works. Tested on a unit whose controller reports 1.0.0. |
| Core 200S | 3 | No | No | Untested. Decoder checked against recorded frames. |
| Core 400S | 4 | Yes | Yes | Untested. |
| Core 600S | 4 | Yes | Yes | Untested. Decoder checked against recorded frames. |
| Vital, Sprout, EverestAir | | | | No. Different protocol. |

## Features

| Feature | In Apple Home |
|---|---|
| Power | Tile, Siri. Turning on resumes the last mode and speed. |
| Fan speed | Slider. Rounds to the nearest of the purifier's speeds. |
| Auto mode | Mode menu. Shows the speed the fan is actually running at. |
| Sleep mode | Switch |
| Display on and off | Switch |
| Child lock | Switch |
| Filter life | Percentage and a change-filter warning |
| Air quality | Level and PM2.5 |
| Pairing | Scan the QR code. The phone sends Wi-Fi over Bluetooth. |
| Identify | The Wi-Fi light changes while Home asks the purifier to identify itself |

The purifier's own buttons keep working, and Home follows them.

Not exposed: timer, auto profiles, room size, the 200S night light, the 600S light sensor.

## On the purifier

| Action | How |
|---|---|
| Factory reset | Hold the centre button for 15 seconds. The Wi-Fi light changes a few seconds in. Keep holding. Clears pairing and Wi-Fi and returns to pairing mode. |
| Reset the filter | Hold Sleep for 3 seconds, until the filter light goes off. |
| Wi-Fi light | Blinking: not connected or not paired. Solid: connected. |
| Filter light | On when filter life is below 5%. |

Removing the purifier from its last controller also factory-resets it.

## From your computer

| Task | Command |
|---|---|
| Update the firmware over Wi-Fi | `./tools/ota.sh <ip>` |
| Set filter life, for a part-used filter | `./tools/filter.sh <ip> 60` |
| Read the purifier's log | `./tools/log.sh <ip>` |
| Follow the log live | `./tools/watch.sh <ip>` |
| Change the update token | `./tools/ota.sh <ip> --set-token <new>` |

An update that fails to come up healthy rolls itself back.

## Install

You need the case open, a 3.3 V USB serial adapter and ESP-IDF 5.5.6.

```bash
cp sdkconfig.local.example sdkconfig.local   # pick the model
idf.py build
./tools/factory.sh                           # this purifier's setup code, QR label and update token
./tools/flash.sh /dev/cu.usbserial-XXXX      # first run backs up the stock firmware
./tools/flash.sh /dev/cu.usbserial-XXXX      # second run flashes
```

Then scan `devices/purifier/secrets/qrcode.png` in Apple Home.

**Read [`FLASHING.md`](FLASHING.md) before you open the purifier.** It covers wiring, the backup, and what to check on the first boot. It also links the existing teardown and flashing guides for these purifiers, from [tuct/levoit](https://github.com/tuct/levoit/tree/main/devices), [acvigue](https://github.com/acvigue/esphome-levoit-air-purifier), [mulcmu](https://github.com/mulcmu/esphome-levoit-core300s) and [ESPHome](https://esphome.io/guides/physical_device_connection/).

To flash from a computer without the build tools, `./tools/bundle.sh --with-device` packs the firmware and tools into one zip.

## Per-purifier secrets

Nothing secret is in the firmware. The same build goes on every purifier of a model. `tools/factory.sh` makes the two things unique to one unit:

| Secret | You keep | The purifier stores |
|---|---|---|
| Matter setup code | `secrets/qrcode.png`, `secrets/setup-codes.csv` | A one-way verifier |
| Update token | `secrets/ota-token.txt` | A salted hash |

They live in `devices/<name>/`, with the purifier's stock firmware backup. The folder is gitignored. **Back it up.** Neither secret can be read back off the chip.

A purifier flashed without this falls back to the public Matter test code and has updates over Wi-Fi turned off.

## Limits

These are Apple Home's, and the firmware cannot work around them. Details in [`docs/APPLE-HOME.md`](docs/APPLE-HOME.md).

- The three switches arrive as unnamed outlet tiles. Rename them once in Home.
- Siri only reaches the switches by the names you give them.
- Air quality tops out at "Good". Home's "Excellent" has no Matter equivalent.
- No filter reset in Home. Use the Sleep button.
- Home shows an "uncertified accessory" prompt once, and always offers "Matter Accessory" as the name.
- No firmware updates through Home.
- After you drag the slider, Home keeps showing where you left it until the app is reopened, even though the purifier has rounded to a real speed.

And the firmware's own:

- Sleep shows as the lowest speed with the Sleep switch on.
- After a reboot, controls can be ignored for up to a minute, until the purifier reports its state.
- Filter life starts at 100% whatever the filter's age. Set it with `tools/filter.sh`.
- Free memory during Bluetooth pairing has dipped as low as 9.6 KB on the stock chip.

`CONFIG_LEVOIT_DISPLAY_SWITCH` and `CONFIG_LEVOIT_CHILD_LOCK_SWITCH` leave out those two switches if you want fewer tiles.

## How it works

One ESP-IDF application on `espressif/esp_matter` 1.6.1, 1.48 MB in a 1.94 MB slot. The purifier protocol is a separate component in `components/levoit/` with its own unit tests.

- [`docs/DESIGN.md`](docs/DESIGN.md): the design, and the purifier's serial protocol, including the parts nobody had documented.
- [`docs/APPLE-HOME.md`](docs/APPLE-HOME.md): how Apple Home behaves with a Matter purifier.

## Related

An ESPHome config for the same purifier is in [esphome-levoit-matter](https://github.com/tadhglewis/esphome-levoit-matter). Best effort.

## Credits

The UART protocol, the per-model layouts and the recorded frames in the tests come from [tuct/levoit](https://github.com/tuct/levoit), with notes from [mulcmu/esphome-levoit-core300s](https://github.com/mulcmu/esphome-levoit-core300s).

## Licence

MIT. See [`LICENSE`](LICENSE).
