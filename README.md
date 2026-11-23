# levoit-matter

Local Matter firmware for Levoit Core air purifiers. No cloud, no VeSync, no Home Assistant required. Pairs with Apple Home by QR code like a shop-bought device.

It replaces the firmware on the purifier's own ESP32. No extra hardware stays inside.

**Status: compiles, unit tests pass. Has never run on hardware.** If you flash this before that line changes, you are the test.

## Supported models

| Model | Speeds | Auto | Air quality | State |
|---|---|---|---|---|
| Core 300S | 3 | Yes | Yes | The one being flashed. Untested on hardware. |
| Core 200S | 3 | No | No | Decoder checked against recorded frames. Never flashed. |
| Core 400S | 4 | Yes | Yes | From the reference driver. No recorded frames. Never flashed. |
| Core 600S | 4 | Yes | Yes | Decoder checked against recorded frames. Never flashed. |
| Vital, Sprout, EverestAir | | | | No. Different protocol. |

Only the Core 300S has an owner here. If you flash another model, keep the serial cable on and report what the log says.

## Features

| Feature | In Apple Home |
|---|---|
| Power and fan speed | Purifier tile with a slider that snaps to the purifier's speeds |
| Auto mode | Purifier mode. Unverified in Home. |
| Filter life and change-filter flag | Filter status. Unverified in Home. |
| PM2.5 and air quality level | Air quality sensor |
| Sleep, Display, Child Lock | Three separate switches |
| Pairing | Scan the QR code. The phone sends Wi-Fi over Bluetooth. |
| Updates over Wi-Fi | Pushed from your machine. A bad image rolls itself back. |
| Factory reset | Tap Display Off six times within fifteen seconds, or remove the purifier from Home. |

Not exposed: timer, auto-mode profiles, room size, the 200S night light, the 600S light sensor. Use Home automations.

## Known limits

- Sleep shows as Low speed with the Sleep switch on. Picking Low again does not leave Sleep. Use the switch or another speed.
- The three switches show as outlet-style tiles. That is what Apple gives you.
- Test vendor ID. Apple shows an "uncertified accessory" prompt once. Tap through it.
- No Matter OTA. Apple will not serve updates to uncertified devices, so updates are pushed over HTTP.
- Free memory during Bluetooth pairing on the stock chip is unmeasured. It may not fit.

## Quick start

You need the case open, a 3.3 V USB serial adapter, and ESP-IDF 5.5.6.

```bash
cp sdkconfig.local.example sdkconfig.local   # model, setup code, update token
idf.py build
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```

The log prints a QR code link. Scan it in Apple Home.

That is the short version. **Read [`FLASHING.md`](FLASHING.md) before you open the purifier.** It covers the toolchain, wiring, backing up the stock firmware, pairing, updates and what to check on the first boot.

## Settings

Set in `sdkconfig.local` before the first build, or in `idf.py menuconfig` under "Levoit purifier".

| Setting | Default | Notes |
|---|---|---|
| `CONFIG_LEVOIT_MODEL_CORE300S=y` | Core 300S | Or `CORE200S`, `CORE400S`, `CORE600S`. |
| `CONFIG_LEVOIT_SETUP_PASSCODE` | 20202021 | The default is the public test code. Pick your own. |
| `CONFIG_LEVOIT_SETUP_DISCRIMINATOR` | 3840 | 0 to 4095. |
| `CONFIG_LEVOIT_OTA_TOKEN` | empty | Empty disables updates over Wi-Fi. |
| `CONFIG_LEVOIT_UART_TX_GPIO`, `_RX_GPIO` | Per model | Only change for a board revision with swapped pins. |

## Layout

| Path | What |
|---|---|
| `main/` | The application: Matter endpoints, the purifier coordinator, the updater, the settings menu. |
| `components/levoit/` | The purifier protocol: framing, the model table, the UART link, value mapping, filter maths. Unit tests in `host_test/`. |
| `tools/` | A purifier simulator for bench testing and the update script. |
| `FLASHING.md` | Build, flash, pair, update, reset. |
| `docs/DESIGN.md` | How it works and why it is built this way. |

Size: 1.48 MB in a 1.94 MB slot. Built on `espressif/esp_matter` 1.6.1.

## Related

An ESPHome config for the same purifier lives in [esphome-levoit-matter](https://github.com/tadhglewis/esphome-levoit-matter). It is best effort and depends on changes to esphome-matter that are not upstream yet.

## Credits

The UART protocol, the per-model layouts and the recorded frames used in the tests come from [tuct/levoit](https://github.com/tuct/levoit), with notes from [mulcmu/esphome-levoit-core300s](https://github.com/mulcmu/esphome-levoit-core300s).

## Licence

MIT. See [`LICENSE`](LICENSE).
