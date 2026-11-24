# Build and flash

Written for the Levoit Core 300S on its stock ESP32-SOLO-1C. Other Core models build from the same source but have never been flashed, and their boards are wired differently. Check the pads against the [tuct/levoit device guides](https://github.com/tuct/levoit/tree/main/devices) first.

**This has not run on hardware.** These steps are written from the build tooling, not from a successful flash. Expect to adjust them.

Commands assume macOS and the repo root as the working directory unless a step says otherwise.

## 1. One-time setup

```bash
git clone git@github.com:tadhglewis/levoit-matter.git
cd levoit-matter
python3 -m venv .venv
.venv/bin/pip install esptool
```

`esptool` is used below for the backup.

The build needs ESP-IDF 5.5.6. Install it the way [Espressif documents](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32/get-started/index.html), run its `export.sh`, and use `idf.py` wherever these steps say `idf`.

If ESPHome 2026.11.0 has compiled an ESP32 project on this machine, that toolchain is already in `~/Library/Caches/esphome/idf`. `source env.sh` points at it and defines the `idf` command, so nothing else needs installing.

## 2. Wire the purifier

You need a 3.3 V USB serial adapter. Do not use 5 V.

Keep the purifier unplugged from the wall the whole time the case is open. The board is powered from the adapter.

1. Unplug the purifier.
2. Remove the base cover and filter, then the eight screws underneath. Separate the base from the sleeve and unplug the logic board. Photos are in the [tuct/levoit Core 300S guide](https://github.com/tuct/levoit/tree/main/devices/levoit-core300s).
3. Solder or clip wires to the pads next to the ESP32 module:

| Board pad | Adapter |
|---|---|
| `TXD0` | RX |
| `RXD0` | TX |
| `GND` | GND |
| `+3V3` | 3.3 V |
| `IO0` | GND, only while powering on |

4. Hold `IO0` to ground, connect the adapter to USB, then release `IO0`. The chip is now in its bootloader.
5. Find the port:

```bash
ls /dev/cu.usbserial-* /dev/cu.wchusbserial-* 2>/dev/null
```

The steps below call it `$PORT`:

```bash
export PORT=/dev/cu.usbserial-XXXX
```

Every flashing command needs the chip in its bootloader. If a command cannot connect, repeat step 4.

## 3. One folder per purifier

Everything that belongs to one purifier lives in `devices/<name>/`. The tools create it. The default name is `purifier`. If you have more than one, pass `-d bedroom`, `-d office` and so on to every tool.

```
devices/purifier/
  fctry.bin            written to the chip once. Holds no readable secret.
  stock-backup.bin     the purifier's original firmware
  boot-log.txt         serial log
  secrets/
    qrcode.png         Matter setup code, as a label
    setup-codes.csv    the same code as text
    ota-token.txt      token for updates over Wi-Fi
```

`secrets/` is what a person reads. It never needs to leave this computer, except the QR code to your phone's camera. The rest of the folder is what goes onto, or came off, the chip.

**Back the whole folder up.** The setup code and token cannot be read back from the purifier, and `stock-backup.bin` is the only way back to VeSync.

## 4. Build

Skip this section if you are flashing a downloaded bundle.

```bash
cp sdkconfig.local.example sdkconfig.local
```

Edit `sdkconfig.local`:

| Setting | Rule |
|---|---|
| `CONFIG_LEVOIT_MODEL_CORE300S=y` | Or `CORE200S`, `CORE400S`, `CORE600S`. |

Build and test:

```bash
source env.sh
idf build
./components/levoit/host_test/run.sh
```

The first build takes about 15 minutes. Expect about 1.48 MB and `155 checks, 0 failures`.

These are ordinary Kconfig options. `sdkconfig.local` only seeds them, so a change to it after the first build is ignored until you delete `sdkconfig`. Or skip the file and set them with `idf menuconfig`, under "Levoit purifier". The UART pins are there too. They default per model.

`source env.sh` is needed once per terminal, if you use it.

## 5. Flash

Make this purifier's setup code and update token. Once per purifier:

```bash
./tools/factory.sh
```

Then, with the chip in its bootloader:

```bash
./tools/flash.sh $PORT
```

The first run only backs up the stock firmware to `devices/purifier/stock-backup.bin`. It checks the flash size on the way: it must report 4 MB, and if it does not, stop.

Put the chip back in its bootloader and run the same command again. This run writes the firmware and the factory data.

Release `IO0`, power cycle, and watch the log:

```bash
./tools/monitor.sh $PORT
```

Exit with `Ctrl-]`. The log is also saved to `devices/purifier/boot-log.txt`.

The factory data only needs writing once. Updates over Wi-Fi do not touch it. `erase-flash` does, and `flash.sh` puts it back.

To restore the stock firmware later:

```bash
.venv/bin/esptool --port $PORT erase-flash
.venv/bin/esptool --port $PORT write-flash 0x0 devices/purifier/stock-backup.bin
```

### Flashing from another computer

```bash
./tools/bundle.sh --with-device
```

This makes `dist/levoit-matter-<model>.zip`: the firmware, these tools, and `devices/purifier/fctry.bin`. It never includes `secrets/`. The other computer needs only Python 3. Unzip it there and run `./tools/flash.sh <port>` as above. Bring `stock-backup.bin` and `boot-log.txt` back into `devices/purifier/` here afterwards.

## 6. Pair

In Apple Home choose Add Accessory, scan `devices/purifier/secrets/qrcode.png` off the screen or a printout, and accept the uncertified accessory prompt. The phone sends Wi-Fi credentials over Bluetooth. If the camera will not read it, enter the manual code from `setup-codes.csv`.

The log should say `setup code from the factory partition`. If it says `no factory data; using the public test setup code`, the factory data was not written, and the purifier is pairable by anyone in Bluetooth range using the public code it then prints. Run `flash.sh` again.

The code stays the same across reflashes, updates and factory resets. Print the QR image and stick it on the purifier.

## 7. Update over Wi-Fi

```bash
source env.sh
idf build
./tools/ota.sh <device-ip>
```

The script reads the token from the device's `secrets/` folder. The purifier reboots into the new image. If Wi-Fi and the purifier link do not both come up within two minutes, it reboots back into the old one.

To change the token, 12 to 64 characters with no spaces:

```bash
./tools/ota.sh <device-ip> --set-token <new-token>
```

The script remembers the new one in `secrets/ota-token.current`. A factory reset discards it and the purifier goes back to the token in `secrets/ota-token.txt`. If the log says `no update token in the factory data`, the factory data was not written and updates over Wi-Fi are off.

### Resetting the filter

After fitting a new filter, hold the Sleep button for three seconds, until the filter light goes off. Filter life returns to 100%. Apple Home has no reset button for it.

### Carrying over a part-used filter

The purifier does not know how old its filter is. Stock firmware kept that count, and this firmware starts at 100%. If your filter is part-used, note the percentage in the VeSync app before flashing, then set it once the purifier is paired:

```bash
./tools/filter.sh <device-ip> 60
```

## 8. Factory reset

Hold the On/Off button for 15 seconds, the same hold Levoit's manual gives for restoring defaults. A few seconds in, the Wi-Fi light changes to show the hold has registered. Keep holding. The purifier clears Matter pairing and Wi-Fi and reboots into pairing mode. Letting go early cancels it.

This has been confirmed on a Core 300S. Other models may not report the button hold, in which case removing the purifier from its controller is the way to reset it.

Removing the purifier from the last controller does the same thing.

If that fails, erase over serial and flash again:

```bash
.venv/bin/esptool --port $PORT erase-flash
./tools/flash.sh $PORT
```

## 9. First boot checklist

Do these with the case still open and the serial cable on. Save the whole log from the first pairing attempt, whatever happens.

| # | Check | What it tells you |
|---|---|---|
| 1 | `heap free=... min=...`, printed every 60 seconds | `min` during pairing decides whether the stock chip is viable. Below about 10,000 is trouble. |
| 2 | Does the purifier keep repeating identical status frames? | It is not accepting our acknowledgement. Set `status_ack_alt` for your model in `components/levoit/src/levoit_core.cpp`. |
| 3 | `no ack for command` | The purifier is not acknowledging commands. Retry timing needs tuning. |
| 4 | `no ack for command 013140` every five seconds, and nothing else | The purifier does not answer status requests. Survivable: state then only refreshes when something changes. |
| 5 | `filter degradation direction=1` | Correct. 0 means the filter will read backwards. |
| 6 | The first status frame, logged in hex | On the 300S and 400S, byte 17 is an error field nobody has decoded. Note its value. |
| 7 | The reset hold | Hold On/Off for 15 seconds. Test it before closing the case. |

## Bench testing without the purifier

`tools/mcu_sim.py` pretends to be the purifier's controller. Wire a USB serial adapter to a classic ESP32 dev board: adapter TX to GPIO16, adapter RX to GPIO17, grounds together.

```bash
pip install pyserial
./tools/mcu_sim.py /dev/cu.usbserial-XXXX --model 300s
```

It answers commands and status requests, sends status when something changes, and takes panel presses from the keyboard. `--help` lists flags for the open protocol questions.

## Switching to or from another firmware

Flash over serial, and erase first:

```bash
.venv/bin/esptool --port $PORT erase-flash
```

Other firmwares store Wi-Fi and Matter data differently, and erasing avoids one reading the other's leftovers. You will need to pair again. Moving between firmwares over Wi-Fi is untested. Do not try it on a closed unit.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `Failed to connect to ESP32` | Not in the bootloader. Hold `IO0` to ground while powering on. |
| Boot loop mentioning `single core` | Built without single-core mode. The config sets it, so this means a stale build folder. Delete `build` and `sdkconfig`. |
| Brownout resets | The adapter cannot supply enough current on its 3.3 V pin. Use one rated for 500 mA or a separate 3.3 V supply with a shared ground. |
| `idf: command not found` | Run `source env.sh` in the repo root, or use `idf.py` from a standard ESP-IDF install. |
| `IDF_PATH` errors | `env.sh` found no ESPHome toolchain cache. Install ESP-IDF 5.5.6 as in section 1. |
