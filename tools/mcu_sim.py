#!/usr/bin/env python3
import argparse
import sys
import threading
import time

import serial

START = 0xA5
SEND = 0x22
ACK = 0x12
ACK_ALT = 0x52


MODELS = {
    "200s": dict(status=0x016040, request=0x016140, levels=3, ack=ACK_ALT, version=(11, 0, 2)),
    "300s": dict(status=0x013040, request=0x013140, levels=3, ack=ACK, version=(7, 0, 2)),
    "400s": dict(status=0x013040, request=0x013140, levels=4, ack=ACK, version=(7, 0, 2)),
    "600s": dict(status=0x014041, request=0x014141, levels=4, ack=ACK_ALT, version=(1, 0, 2)),
}


def checksum(frame):
    return 0xFF - (sum(b for i, b in enumerate(frame) if i != 5) & 0xFF)


def encode(kind, counter, command, payload=b"", trailer=0):
    frame = bytearray([START, kind, counter, 4 + len(payload), 0, 0])
    frame += command.to_bytes(3, "big") + bytes([trailer]) + payload
    frame[5] = checksum(frame)
    return bytes(frame)


class Purifier:
    def __init__(self, args):
        self.args = args
        self.name = args.model
        self.model = MODELS[args.model]
        self.power = False
        self.mode = 0
        self.level = 1
        self.display = True
        self.lock = False
        self.air_quality = 1
        self.pm25 = 3
        self.counter = 0x10
        self.acks_to_drop = args.drop_acks
        self.inject = []

    def payload(self):
        level = self.level if self.mode == 0 or self.args.level_in_auto else 0xFF
        head = [*self.model["version"], int(self.power), self.mode, level]
        display = 0x64 if self.display else 0
        pm25 = [self.pm25 & 0xFF, self.pm25 >> 8]
        if self.name == "200s":
            return bytes(head + [display, 1, 0, 0, int(self.lock), 0])
        if self.name == "600s":
            return bytes(head + [1, 0, display, 1, 0, self.air_quality, *pm25, int(self.lock), 0,
                                 0x2E, 0x10, 0, 0, 1, 1, 0, 1])
        slots = [display, 1] if self.name == "300s" else [1, display]
        return bytes(head + slots + [0, 0, self.air_quality, *pm25, int(self.lock), 0, 0x3B, 0x01, 0])

    def filter_reset(self):
        frame = encode(SEND, self.counter, 0x01E4A5)
        self.counter = 0x10 if self.counter == 0xFF else self.counter + 1
        return frame

    def status(self):
        command = self.model["status"]
        if self.args.new_status and command == 0x013040:
            command = 0x01B040
        frame = encode(SEND, self.counter, command, self.payload())
        self.counter = 0x10 if self.counter == 0xFF else self.counter + 1
        return frame

    def apply(self, command, payload):
        if command == 0x0100A0:
            self.power = payload[0] != 0
        elif command == 0x0160A2:
            self.level = payload[2]
            self.mode = 0
        elif command == 0x01E0A5:
            self.mode = payload[0]
        elif command == 0x0105A1:
            self.display = payload[0] != 0
        elif command == 0x0100D1:
            self.lock = payload[0] != 0
        elif command in (0x0129A1, 0x01E2A5, self.model["request"]):
            pass
        else:
            return False
        return True


def parse(buffer):
    frames = []
    while len(buffer) >= 6:
        if buffer[0] != START:
            del buffer[0]
            continue
        length = 6 + buffer[3]
        if length < 10 or length > 74:
            del buffer[0]
            continue
        if len(buffer) < length:
            break
        frame = bytes(buffer[:length])
        if checksum(frame) != frame[5]:
            del buffer[0]
            continue
        del buffer[:length]
        frames.append(frame)
    return frames


def describe(p):
    mode = ["manual", "sleep", "auto"][p.mode]
    return (f"power={int(p.power)} mode={mode} level={p.level} display={int(p.display)} "
            f"lock={int(p.lock)} aq={p.air_quality} pm25={p.pm25}")


def keyboard(purifier):
    print("keys + enter: p power, d display, l lock, 1-4 level, a auto, s sleep, q air quality, + / - pm2.5, f filter reset, g garbage")
    for line in sys.stdin:
        key = line.strip()
        if key == "p":
            purifier.power = not purifier.power
        elif key == "d":
            purifier.display = not purifier.display
        elif key == "l":
            purifier.lock = not purifier.lock
        elif key.isdigit() and 1 <= int(key) <= purifier.model["levels"]:
            purifier.level, purifier.mode = int(key), 0
        elif key == "a" and purifier.name != "200s":
            purifier.mode = 2
        elif key == "s":
            purifier.mode = 1
        elif key == "q":
            purifier.air_quality = purifier.air_quality % 4 + 1
        elif key == "+":
            purifier.pm25 += 10
        elif key == "-":
            purifier.pm25 = max(0, purifier.pm25 - 10)
        elif key == "f":
            purifier.inject.append(purifier.filter_reset())
        elif key == "g":
            purifier.inject.append(bytes([0xA5, 0x12, 0x20, 0x20, 0x00]))
        print("panel:", describe(purifier))


def main():
    parser = argparse.ArgumentParser(description="Pretend to be a Levoit Core purifier control MCU")
    parser.add_argument("port")
    parser.add_argument("--model", choices=sorted(MODELS), default="300s")
    parser.add_argument("--drop-acks", type=int, default=0, help="ignore this many commands before answering")
    parser.add_argument("--new-status", action="store_true", help="send status as 01 B0 40")
    parser.add_argument("--level-in-auto", action="store_true", help="report a fan level in auto and sleep")
    parser.add_argument("--expect-ack-alt", action="store_true", help="expect 0x52 acks for status on a 300S or 400S")
    parser.add_argument("--periodic", action="store_true", help="send status every second, not only on change")
    args = parser.parse_args()

    purifier = Purifier(args)
    link = serial.Serial(args.port, 115200, timeout=0.05)
    threading.Thread(target=keyboard, args=(purifier,), daemon=True).start()

    buffer = bytearray()
    next_status = 0.0
    unacked = 0
    last_payload = None
    while True:
        buffer += link.read(128)
        for frame in parse(buffer):
            kind, counter = frame[1], frame[2]
            command = int.from_bytes(frame[6:9], "big")
            payload = frame[10:]
            if kind in (ACK, ACK_ALT):
                if command not in (purifier.model["status"], 0x01B040):
                    continue
                wanted = ACK_ALT if args.expect_ack_alt else purifier.model["ack"]
                if kind == wanted:
                    unacked = 0
                else:
                    print(f"wrong ack type 0x{kind:02X}")
                continue
            if kind != SEND:
                print(f"unknown frame type 0x{kind:02X}")
                continue
            if purifier.acks_to_drop > 0:
                purifier.acks_to_drop -= 1
                print(f"dropped command {command:06X}")
                continue
            known = purifier.apply(command, payload)
            reply = purifier.payload() if command == purifier.model["request"] else b""
            link.write(encode(ACK, counter, command, reply))
            print(f"command {command:06X} {payload.hex()} {'ok' if known else 'unknown'}: {describe(purifier)}")
        while purifier.inject:
            link.write(purifier.inject.pop(0))
        now = time.monotonic()
        if now >= next_status:
            next_status = now + 1.0
            current = purifier.payload()
            if args.periodic or current != last_payload or unacked > 0:
                if unacked >= 4:
                    print("status frames are not being acknowledged")
                    unacked = 0
                last_payload = current
                unacked += 1
                link.write(purifier.status())


if __name__ == "__main__":
    main()
