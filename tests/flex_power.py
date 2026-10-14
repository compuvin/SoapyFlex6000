#!/usr/bin/env python3
"""Read Flex TX power settings for this temporary GUI client.

Uses only Python's standard library. No transmit, tune, or power-setting
commands are sent. Flex can report different TX state to different clients,
so this does not inspect another application's existing GUI session.
"""

import argparse
import math
import re
import socket
import sys
import time


FIELD = re.compile(r"(?:^|\s)([a-zA-Z_][a-zA-Z_0-9]*)=([^\s]+)")


class FlexConnection:
    def __init__(self, address):
        self.socket = socket.create_connection((address, 4992), timeout=3)
        self.pending = b""
        self.sequence = 0
        self.status = []

    def close(self):
        self.socket.close()

    def read_line(self, deadline):
        while b"\n" not in self.pending:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("timed out waiting for a Flex reply")
            self.socket.settimeout(remaining)
            data = self.socket.recv(4096)
            if not data:
                raise ConnectionError("Flex closed the TCP connection")
            self.pending += data
        line, self.pending = self.pending.split(b"\n", 1)
        return line.decode("utf-8", "replace").rstrip("\r")

    def command(self, text):
        self.sequence += 1
        number = self.sequence
        self.socket.sendall(f"C{number}|{text}\n".encode())
        deadline = time.monotonic() + 3
        while True:
            line = self.read_line(deadline)
            if line.startswith("S") and "|" in line:
                self.status.append(line.split("|", 1)[1])
            if line.startswith(f"R{number}|"):
                parts = line.split("|", 2)
                if len(parts) < 2 or int(parts[1], 16) != 0:
                    raise RuntimeError(f"Flex refused {text}: {line}")
                return

    def drain(self, seconds=0.4):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                line = self.read_line(deadline)
            except TimeoutError:
                break
            if line.startswith("S") and "|" in line:
                self.status.append(line.split("|", 1)[1])


def fields(text):
    return dict(FIELD.findall(text))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("addr", help="Flex radio IPv4 address")
    parser.add_argument("--bands", action="store_true", help="also list reported per-band power settings")
    args = parser.parse_args()

    radio = FlexConnection(args.addr)
    try:
        radio.command("client gui")
        radio.command("sub tx all")
        radio.command("radio uptime")  # Flush the initial TX status on this connection.
        radio.drain()
        power = {}
        bands = []
        for status in radio.status:
            if status.startswith("transmit band "):
                bands.append(status)
            elif status.startswith("transmit "):
                power.update(fields(status))
    finally:
        radio.close()

    if "rfpower" not in power:
        raise RuntimeError("the radio did not report TX RF power to this client")
    rfpower = int(power["rfpower"])
    db = "-inf" if rfpower == 0 else f"{10 * math.log10(rfpower / 100):.1f}"
    print(f"RF power/drive: {rfpower}/100 ({db} dB relative to 100)")
    if "tunepower" in power:
        print(f"Tune power:     {power['tunepower']}/100")
    if "max_power_level" in power:
        print(f"Max power:      {power['max_power_level']}/100")
    if "tx_rf_power_changes_allowed" in power:
        print(f"Power changes allowed: {power['tx_rf_power_changes_allowed'] == '1'}")
    if args.bands:
        for band in bands:
            number = band.split(" ", 3)[2]
            values = fields(band)
            print(f"Band {values.get('band_name', number):>5}: "
                  f"rfpower={values.get('rfpower', '?')} "
                  f"tunepower={values.get('tunepower', '?')}")
    print("Scope: temporary GUI client; another application's active setting may differ.")
    print("This is a drive setting, not a measurement of transmitted watts.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        print(f"Flex power check failed: {error}", file=sys.stderr)
        sys.exit(1)
