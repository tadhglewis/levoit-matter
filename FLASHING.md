# Build and flash

This is written for a Core 300S, because that is the one that has been done. The other models have different boards. Check yours against the [tuct/levoit device guides](https://github.com/tuct/levoit/tree/main/devices) before you solder anything.

Commands assume macOS and the repo root.

## Before you start

Update the purifier in the VeSync app one last time. After this firmware goes on, VeSync can never update the purifier's own controller again, and older controller firmware misreported PM2.5 and fan speed.

Note the filter percentage VeSync shows. You will need it at the end.

## 1. Tools

```bash
git clone https://github.com/tadhglewis/levoit-matter.git
cd levoit-matter
```

To build you need ESP-IDF 5.5.6. Install it [the way Espressif says](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32/get-started/index.html) and run its `export.sh`.

If ESPHome 2026.11.0 has ever compiled an ESP32 project on this machine, you already have that toolchain. `source env.sh` points at ESPHome's copy and gives you an `idf` command that stands in for `idf.py`.

To flash a prebuilt bundle you need Python 3 and nothing else.

## 2. Wire it

You need a 3.3 V USB serial adapter. 5 V will kill the board.

Unplug the purifier from the wall and leave it unplugged the whole time the case is open. The adapter powers the board.

1. Take off the base cover and the filter. Remove the eight screws underneath. Lift the base off the sleeve and unplug the logic board.
2. Solder or clip wires to the pads beside the ESP32. On some boards they are through-holes and pins will hold without solder.

| Board | Adapter |
|---|---|
| `TXD0` | RX |
| `RXD0` | TX |
| `GND` | GND |
| `+3V3` | 3.3 V |
| `IO0` | GND, only while powering on |

3. Leave `IO0` loose for now. The tools tell you when to ground it.

The tools find the adapter on their own when it is the only USB serial device plugged in. If they find more than one, they list them; pass the right one as the first argument.

## 3. Build

Skip this if you have a prebuilt bundle.

```bash
cp sdkconfig.local.example sdkconfig.local
```

Set the model in it: `CONFIG_LEVOIT_MODEL_CORE300S=y`, or `CORE200S`, `CORE400S`, `CORE600S`.

```bash
idf.py build
./components/levoit/host_test/run.sh
```

The first build takes about 15 minutes. You should get a 1.48 MB image and `0 failures`.

`sdkconfig.local` only seeds the settings. Change it after the first build and nothing happens until you delete `sdkconfig`. The same settings are in `idf.py menuconfig` under "Levoit purifier".

## 4. Flash

Make this purifier's setup code and update token. Once per purifier.

```bash
./tools/factory.sh
```

Then start the flash, and only then power the chip up with `IO0` held to ground:

```bash
./tools/flash.sh
```

It waits for the bootloader, then reads the stock firmware in 16 chunks, about seven minutes in all. Each chunk is kept, so if the connection drops, do the grounded power-up again and it carries on where it stopped. When the backup is whole it asks for one more grounded power-up and writes the firmware.

To check the backup against the chip before anything is written, run `./tools/flash.sh --backup-only`, then `./tools/verify_when_ready.py`. Neither writes to the chip.

Let go of `IO0`, power cycle, and watch it boot:

```bash
./tools/monitor.sh
```

`Ctrl-]` exits. You want to see `setup code from the factory partition`. If it says `no factory data; using the public test setup code`, the factory data did not go on and anyone in Bluetooth range can pair your purifier. Run `flash.sh` again.

If it still fails, lower the speed with `--baud 57600`. `--now` skips the wait when the chip is already in its bootloader.

## 5. The device folder

The tools keep everything for one purifier in `devices/<name>/`. The name defaults to `purifier`. With more than one, pass `-d bedroom` to every tool.

```
devices/purifier/
  fctry.bin            goes on the chip. No readable secret in it.
  stock-backup.bin     the original firmware
  backup-parts/        chunks of the backup, safe to delete once it is verified
  boot-log.txt         serial log
  secrets/
    qrcode.png         Matter setup code
    setup-codes.csv    the same code as text
    ota-token.txt      token for updates over Wi-Fi
    ota-token.current  appears if you change the token, and wins
```

Back this folder up somewhere that is not this laptop. The setup code and token cannot be read back off the purifier. `stock-backup.bin` is the only way back to VeSync.

## 6. Pair

In Apple Home, Add Accessory, scan `secrets/qrcode.png` off the screen or a printout. Accept the uncertified accessory prompt. If the camera will not read it, type the manual code from `setup-codes.csv`.

Home Assistant rejects the purifier's test certificate by default. In the Matter Server add-on's configuration, turn on "Enable test-net DCL" and restart the add-on first. The add-on log says "This device uses a test/development certificate" if you forgot.

Home Assistant shows the three switches as Switch (3), (4) and (5): Sleep, Display and Child Lock. Rename them on each entity's settings page. The names survive updates.

Print the QR code and stick it on the purifier. The code never changes, across updates and factory resets alike.

Then set the filter life you noted from VeSync. The firmware cannot know how old your filter is and starts at 100%.

```bash
./tools/filter.sh 192.168.1.50 60
```

Use your purifier's address, from your router's list of devices. The tools remember it in the device folder, so the other tools need no address after this. Pass one again if it changes.

### Rename the switches

Home ignores the names the firmware sends, so the three switches arrive as numbered outlet tiles. Home says "Power point" in Australian English and "Outlet" in US English.

| Home calls it | Rename to | Turn it on and |
|---|---|---|
| Power point 2 | Sleep | the fan drops to its quietest speed and the panel goes dark |
| Power point 3 | Display | the panel lights come on |
| Power point | Child Lock | the panel buttons stop responding |

That is the order Home lists them in, and yes, the unnumbered one is the last endpoint. Seen on one pairing, so check the third column before you trust it. Run the purifier at a manual speed while you check, because Display does nothing when the purifier is off or in Sleep.

To rename one, long-press the purifier tile, scroll to Accessories, tap the switch, then tap its name. Do it again after every re-pair. Home forgets the names when you remove the accessory.

While you are there, rename the purifier itself from "Matter Accessory". Siri uses these names and nothing else.

## 7. Update

```bash
idf.py build
./tools/ota.sh
```

The purifier reboots into the new image. If Wi-Fi, the update server and the link to the purifier's controller are not all up within two minutes, it goes back to the old one.

Raise `PROJECT_VER_NUMBER` in `CMakeLists.txt` on every release. Apple Home only re-reads a purifier's details when that number goes up.

To change the update token, 12 to 64 characters, no spaces:

```bash
./tools/ota.sh --set-token <new-token>
```

A factory reset puts the original token back.

## 8. Reset

Factory reset: hold On/Off for 15 seconds. That is the [manual's](https://files.vesync.com/desc/LEVOIT_Core300S_USER_MANUAL.pdf) own reset (page 7). The Wi-Fi symbol above On/Off is a light, not a button. The Wi-Fi light changes a few seconds in. Keep holding. Letting go early cancels it. Child lock blocks the button, so turn the lock off at the panel first.

Filter reset: hold Sleep for 3 seconds, until the filter light goes off ([manual](https://files.vesync.com/desc/LEVOIT_Core300S_USER_MANUAL.pdf), page 5).

If the purifier is beyond help, erase it and start again:

```bash
.venv/bin/esptool erase-flash
./tools/flash.sh
```

To go back to stock:

```bash
.venv/bin/esptool erase-flash
.venv/bin/esptool write-flash 0x0 devices/purifier/stock-backup.bin
```

Both need the chip in its bootloader, and esptool finds the port itself.

## Flashing from another computer

```bash
./tools/bundle.sh
```

That writes `dist/levoit-matter-<model>.zip` with the firmware and the tools. The other computer needs Python 3. Unzip, run `factory.sh`, then `flash.sh` twice as above.

The bundle holds nothing specific to one purifier, so it is safe to hand to someone else.

## Debugging

`./tools/log.sh` prints the last 6 KB of the purifier's log over Wi-Fi. `./tools/watch.sh` follows it. Both need the purifier paired. A reboot clears the log.

For a model or controller the firmware does not understand, build with `CONFIG_LEVOIT_LOG_FRAMES=y`. The log then has one hex line for every frame in both directions. Follow it with `./tools/watch.sh 'link: (rx|tx)'`. Turn it off again afterwards, because it fills the log.

`tools/mcu_sim.py` plays the purifier's controller on a serial port, for testing on a bare ESP32 dev board. Adapter TX to GPIO16, RX to GPIO17, grounds together.

| Symptom | Cause |
|---|---|
| `Failed to connect to ESP32` | Not in the bootloader. Hold `IO0` to ground while powering on. |
| Boot loop mentioning `single core` | Stale build. Delete `build` and `sdkconfig`. |
| Brownout resets | The adapter cannot supply the current. Use one rated for 500 mA. |
| `no ack for command` in the log | The purifier's controller is not answering. On the bench with the board unplugged from the purifier, that is expected. |
| Home shows the purifier but no control works | The firmware has not understood a status frame yet. Read the log. |

## Other guides

The soldering and the backup are the same as for flashing ESPHome onto this purifier, which other people have documented with photos.

| Guide | Use it for |
|---|---|
| [tuct/levoit, Core 300S](https://github.com/tuct/levoit/tree/main/devices/levoit-core300s) | Teardown photos and where the pads are |
| [tuct/levoit, other devices](https://github.com/tuct/levoit/tree/main/devices) | The same for the 200S, 400S and 600S |
| [acvigue/esphome-levoit-air-purifier](https://github.com/acvigue/esphome-levoit-air-purifier) | A second write-up of the solder points |
| [mulcmu/esphome-levoit-core300s](https://github.com/mulcmu/esphome-levoit-core300s) | Board photos and a permanent wiring harness |
| [ESPHome: physically connecting to your device](https://esphome.io/guides/physical_device_connection/) | Serial adapters and bootloader mode, if this is your first time |
| [Espressif: boot mode selection](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html) | Why the chip will not connect |

Follow them for the hardware. Do not follow them for the flashing.

- This firmware is four pieces at four addresses. `flash.sh` writes them. One image written at `0x0`, which is what those guides do, will not boot.
- The ESPHome web flasher will not work, for the same reason.
- [nico202/levoit-core300s-ota-wifi-flash](https://github.com/nico202/levoit-core300s-ota-wifi-flash) gets ESPHome onto a Core 300S over Wi-Fi with the case shut. It cannot install this. It replaces the application only, and this firmware needs its own partition table.
