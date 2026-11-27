# levoit-matter

Matter firmware for Levoit Core air purifiers. It runs on the ESP32 already inside the purifier, talks to Apple Home directly, and needs no cloud, no VeSync account and no hub software.

## Models

| Model      | Speeds  | Auto | Air quality | State    |
| ---------- | ------- | ---- | ----------- | -------- |
| Core 300S  | 3       | Yes  | Yes         | Works    |
| Core 200S  | 3       | No   | No          | Untested |
| Core 400S  | 4       | Yes  | Yes         | Untested |
| Core 600S  | 4       | Yes  | Yes         | Untested |
| Sprout     | 4       | Yes  | Yes         | Untested |
| EverestAir | 3+Turbo | Yes  | Yes         | Untested |

All models run on [tuct/levoit](https://github.com/tuct/levoit)'s shared protocol core. Untested means it builds and should work, but only the Core 300S has been run on hardware — flash another and [report back](https://github.com/tadhglewis/levoit-matter/issues/7). Vital 100S/200S and Eleva 300X need an ESP32-C3 build; the Superior 6000S is a humidifier, out of scope.

## What you get in Apple Home

| Feature                    | How                                                                                                            |
| -------------------------- | -------------------------------------------------------------------------------------------------------------- |
| Power                      | Tile and Siri. Turning on resumes the last mode and speed.                                                     |
| Fan speed                  | Slider, rounded up to the next real speed                                                                      |
| Auto                       | Mode menu. Shows the speed the fan is running at.                                                              |
| Sleep, Display, Child Lock | Three switches. Sleep and Child Lock are also in their standard Matter places, which Apple Home does not show. |
| Filter life                | Percentage, with a warning when it runs low                                                                    |
| Air quality                | Level and PM2.5                                                                                                |
| Pairing                    | Scan a QR code. The phone hands over Wi-Fi by Bluetooth.                                                       |
| Firmware updates           | Matter OTA Software Update, from your computer. A bad image rolls back.                                        |

The buttons on the purifier still work, and Home follows them.

Timer, auto profiles and room size are not exposed.

## On the purifier

| To               | Do                                                              |
| ---------------- | --------------------------------------------------------------- |
| Factory reset    | Hold On/Off for 15 seconds, as the [manual](https://files.vesync.com/desc/LEVOIT_Core300S_USER_MANUAL.pdf) says (page 7). Clears pairing and Wi-Fi. Turn child lock off first. |
| Reset the filter | Hold Sleep for 3 seconds, until the filter light goes off ([manual](https://files.vesync.com/desc/LEVOIT_Core300S_USER_MANUAL.pdf), page 5) |

Wi-Fi light blinking means not paired or not connected. Solid means connected. The filter light comes on below 5%.

Removing the purifier from Home also puts it back in pairing mode.

## From your computer

| To                                | Run                                        |
| --------------------------------- | ------------------------------------------ |
| Let your computer update it, once | `./tools/matter-ota.sh pair <code>`        |
| Update the firmware               | `./tools/matter-ota.sh update`             |
| Set filter life                   | `./tools/filter.sh 60`                     |
| Read the log                      | `./tools/log.sh`                           |
| Follow the log                    | `./tools/watch.sh`                         |
| Change the token                  | `./tools/ota.sh --set-token <new>`         |

The first time, put the purifier's IP before the other arguments, for example `./tools/log.sh 192.168.1.50`. The tools remember it.

Updates use Matter OTA Software Update, sent from your computer by `tools/matter-ota.sh` with [matter.js](https://github.com/matter-js/matter.js). Only a controller you paired can send one, and the image travels over Matter's encrypted, certificate-authenticated connection, the same kind Apple Home uses. No password or token crosses the network. `pair` takes the code from Turn On Pairing Mode in the purifier's settings in Home and adds your computer as a second controller. Home stays paired. You need Node and [pnpm](https://pnpm.io/installation). The tool fetches only the matter.js package and Matter's public attestation certificates. A new image has to come up with Wi-Fi and the purifier's controller both working, or the bootloader goes back to the old one.

Home Assistant's Matter Server can send the same updates. Untested; [docs/DESIGN.md](docs/DESIGN.md#updates) has how.

## Install

You open the case, solder five wires and flash over serial. Once.

```bash
cp sdkconfig.local.example sdkconfig.local   # pick the model
idf.py build
./tools/factory.sh                           # this purifier's setup code and token
./tools/flash.sh                             # backs up the stock firmware, then flashes
```

Scan `devices/purifier/secrets/qrcode.png` in Apple Home.

Read [FLASHING.md](FLASHING.md) first. It has the wiring, and the backup you will want if you ever go back to VeSync.

## Security

The firmware contains no secrets. One build fits every purifier of a model.

`tools/factory.sh` makes a setup code and a token for one purifier and puts them in `devices/<name>/secrets/`. The chip stores a verifier for the first and a hash for the second, so neither can be read back off it.

Back that folder up. Lose it and you re-flash over serial to get a new code.

Updates need a controller you paired and travel encrypted. The log and the filter setting are plain HTTP with the token. Fine on a home network you trust. Not fine on one you share. Images are not signed: any controller you paired can install any image.

Skip `factory.sh` and the purifier uses the public Matter test code, which anyone nearby can pair with, and has no log or filter endpoint.

## Limits

Apple Home's, not ours. [docs/APPLE-HOME.md](docs/APPLE-HOME.md) has the evidence.

- The three switches arrive as "Power point 2", "Power point 3" and "Power point". They are Sleep, Display and Child Lock, in that order. [Rename them once](FLASHING.md#rename-the-switches).
- Siri only finds those switches by the names you give them.
- Air quality reads Good, Fair or Poor. Home has five levels and uses three for Matter. A blue or green ring is Good, orange is Fair, red is Poor.
- No filter reset in Home. Use the Sleep button.
- The slider redraws at the real speed only when your move changed the speed. Otherwise it sits where you left it until you reopen the app.
- Home calls it an uncertified accessory, once, and offers "Matter Accessory" as its name.
- Home will not deliver firmware updates. Use `tools/matter-ota.sh`.

Ours:

- Sleep shows as the lowest speed with the Sleep switch on.
- Filter life starts at 100% whatever the filter's age. Set it with `tools/filter.sh`.
- The attestation certificate is the SDK's test one. Home Assistant refuses it until you turn on "Enable test-net DCL" in the Matter Server add-on's configuration.
- Free memory during Bluetooth pairing has dropped to 9.6 KB on the stock chip. It has always paired. It is not much. Sizes and memory are in [docs/DESIGN.md](docs/DESIGN.md#resources).

`CONFIG_LEVOIT_DISPLAY_SWITCH` and `CONFIG_LEVOIT_CHILD_LOCK_SWITCH` remove those two switches if you want fewer tiles.

## More

- [FLASHING.md](FLASHING.md): build, flash, pair, update
- [docs/DESIGN.md](docs/DESIGN.md): how it works, and the purifier's serial protocol
- [docs/APPLE-HOME.md](docs/APPLE-HOME.md): what Apple Home does with a Matter purifier
- [Teardown and engineering demo](https://tadhglewis.github.io/levoit-matter/teardown/): the board, the protocol, the stock cloud, the flash, and Matter. Source in [docs/teardown](docs/teardown/).
- [esphome-levoit-matter](https://github.com/tadhglewis/esphome-levoit-matter): the same purifier as an ESPHome config. Best effort.

## Contributing

Only the Core 300S has been run. If you flash a 200S, 400S or 600S, open an issue with the output of `./tools/log.sh` whether it worked or not. The first status frame is in there, and that is what a model fix needs. A build with `CONFIG_LEVOIT_LOG_FRAMES=y` logs every frame, which is better still.

`./components/levoit/host_test/run.sh` has to pass. A change to the protocol code comes with a captured frame in the tests.

## Credits

The serial protocol comes from [tuct/levoit](https://github.com/tuct/levoit) and [mulcmu/esphome-levoit-core300s](https://github.com/mulcmu/esphome-levoit-core300s). Their captures are the test vectors for the models we cannot test ourselves.

What we found on a Core 300S with controller 1.0.0 went back to tuct/levoit: the 19-byte status and the status request are merged ([#140](https://github.com/tuct/levoit/pull/140), [#141](https://github.com/tuct/levoit/pull/141)).

MIT. See [LICENSE](LICENSE).
