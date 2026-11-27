# Switching with esphome-levoit-matter

This branch only. For one purifier that runs this firmware and sometimes [esphome-levoit-matter](https://github.com/tadhglewis/esphome-levoit-matter), updated over Wi-Fi both ways.

Both use this repo's partition table and the same `fctry` factory data, so the label from `tools/factory.sh` pairs either. Each removes every trace of the other:

- On its first boot it erases the whole settings partition if the other's settings are there: pairing, Wi-Fi, filter life, everything.
- Once its own image is confirmed, it erases the other app slot if that holds the other firmware.
- Neither writes to `fctry`, so only the setup code carries over. Set filter life with `tools/filter.sh` after coming back.

Build this firmware with `CONFIG_LEVOIT_HTTP_OTA=y` in `sdkconfig.local`. Leaving it needs `tools/ota.sh`: the Matter update only takes a Matter image from the same vendor and product, and ESPHome builds none.

The erase also removes `tools/matter-ota.sh`'s pairing. After coming back, delete `devices/<name>/matter/` and run `tools/matter-ota.sh pair` with a new code from Turn On Pairing Mode.

The steps are in that repo's [`DUAL-FIRMWARE.md`](https://github.com/tadhglewis/esphome-levoit-matter/blob/feat/dual-firmware/DUAL-FIRMWARE.md), on its `feat/dual-firmware` branch.
