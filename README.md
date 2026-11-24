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
| Filter life and change-filter flag | Filter status. Starts at 100%; `tools/filter.sh` sets it for a part-used filter. |
| PM2.5 and air quality level | Air quality sensor |
| Sleep, Display, Child Lock | Three separate switches |
| Pairing | Scan the QR code. The phone sends Wi-Fi over Bluetooth. |
| Updates over Wi-Fi | Pushed from your machine. A bad image rolls itself back. |
| Factory reset | Hold the Wi-Fi button for 15 seconds, or remove the purifier from Home. |

Not exposed: timer, auto-mode profiles, room size, the 200S night light, the 600S light sensor. Use Home automations.

## Known limits

- Sleep shows as Low speed with the Sleep switch on. Picking Low again does not leave Sleep. Use the switch or another speed.
- The three switches show as outlet-style tiles. That is what Apple gives you.
- Air quality never shows "Excellent" in Apple Home. Home's scale is Excellent, Good, Fair, Inferior, Poor. Matter's best value is "Good", and Home shows it as Good, one step below its top. The PM2.5 number is unaffected.
- Test vendor ID. Apple shows an "uncertified accessory" prompt once. Tap through it.
- No Matter OTA. Apple will not serve updates to uncertified devices, so updates are pushed over HTTP.
- Free memory during Bluetooth pairing on the stock chip is unmeasured. It may not fit.

## Quick start

You need the case open, a 3.3 V USB serial adapter, and ESP-IDF 5.5.6.

```bash
cp sdkconfig.local.example sdkconfig.local   # model
idf.py build
./tools/factory.sh                           # this purifier's setup code, QR label and update token
./tools/flash.sh /dev/cu.usbserial-XXXX      # first run backs up the stock firmware, second run flashes
./tools/monitor.sh /dev/cu.usbserial-XXXX
```

Scan `devices/purifier/secrets/qrcode.png` in Apple Home.

That is the short version. **Read [`FLASHING.md`](FLASHING.md) before you open the purifier.** It covers the toolchain, wiring, backing up the stock firmware, pairing, updates and what to check on the first boot.

## Settings

Set in `sdkconfig.local` before the first build, or in `idf.py menuconfig` under "Levoit purifier".

| Setting | Default | Notes |
|---|---|---|
| `CONFIG_LEVOIT_MODEL_CORE300S=y` | Core 300S | Or `CORE200S`, `CORE400S`, `CORE600S`. |
| `CONFIG_LEVOIT_UART_TX_GPIO`, `_RX_GPIO` | Per model | Only change for a board revision with swapped pins. |

## Per-purifier secrets

Nothing secret is in the firmware. The same build goes on every purifier of a model, and `tools/factory.sh` makes the two things that are unique to one unit:

| Secret | Where you keep it | What the purifier stores |
|---|---|---|
| Matter setup code | `secrets/qrcode.png`, `secrets/setup-codes.csv` | A one-way verifier |
| Update token | `secrets/ota-token.txt` | A salted hash |

Each purifier gets a folder, `devices/<name>/`, holding its factory data, its stock firmware backup, and a `secrets/` subfolder with the two files above. The folder is gitignored. Back it up: neither secret can be read back off the chip.

On the purifier, both live in a small factory partition that firmware updates and factory resets leave alone.

The update token can be changed over Wi-Fi with `./tools/ota.sh <ip> --set-token <new>`. A factory reset returns to the original.

A purifier flashed without factory data falls back to the public Matter test code, has updates over Wi-Fi turned off, and says so in its log. Fine on a bench, not in a home.

## How it works

One ESP-IDF application on `espressif/esp_matter` 1.6.1, 1.48 MB in a 1.94 MB slot. The purifier protocol is a separate component in `components/levoit/` with its own unit tests. [`docs/DESIGN.md`](docs/DESIGN.md) explains the design and why it is built this way. [`docs/APPLE-HOME.md`](docs/APPLE-HOME.md) records how Apple Home behaves with it, including the limits we could not work around.

## Related

An ESPHome config for the same purifier lives in [esphome-levoit-matter](https://github.com/tadhglewis/esphome-levoit-matter). It is best effort and depends on changes to esphome-matter that are not upstream yet.

## Credits

The UART protocol, the per-model layouts and the recorded frames used in the tests come from [tuct/levoit](https://github.com/tuct/levoit), with notes from [mulcmu/esphome-levoit-core300s](https://github.com/mulcmu/esphome-levoit-core300s).

## Licence

MIT. See [`LICENSE`](LICENSE).
