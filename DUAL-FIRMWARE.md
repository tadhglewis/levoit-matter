# Switching with esphome-levoit-matter

This branch only. For one purifier that runs this firmware and sometimes [esphome-levoit-matter](https://github.com/tadhglewis/esphome-levoit-matter), updated over Wi-Fi both ways.

Both use this repo's partition table and the same `fctry` factory data, so the label from `tools/factory.sh` pairs either. Each erases the other's settings on its first boot, so nothing carries over: not pairing, not Wi-Fi, not filter life. Set filter life with `tools/filter.sh` after coming back.

The steps are in that repo's [`DUAL-FIRMWARE.md`](https://github.com/tadhglewis/esphome-levoit-matter/blob/dual-firmware/DUAL-FIRMWARE.md), on its `dual-firmware` branch.
