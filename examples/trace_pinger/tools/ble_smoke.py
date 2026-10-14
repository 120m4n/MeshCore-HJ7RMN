#!/usr/bin/env python3
"""Prueba de humo del pinger por BLE NUS (spec 003, T013). Requiere `pip install bleak`.

Escanea `YagiPinger-*`, conecta, se suscribe a TX y ejecuta HELLO, CFG?, SET hash y N pings,
comprobando el formato de cada línea y midiendo la latencia comando→P,.

  python3 ble_smoke.py [--hash 4F] [--pings 5] [--name YagiPinger-1A2B]
"""
import argparse
import asyncio
import re
import statistics
import sys
import time

from bleak import BleakClient, BleakScanner

NUS = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # central -> pinger
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # pinger -> central

P_LINE = re.compile(r"^P,\d+,[0-9A-F]{8},(ok,\d+,[+-]\d+\.\d\d,[+-]\d+\.\d\d,[+-]\d+\.\d,\d+|(timeout|busy|txerr),,,,,\d+)$")


class Link:
    def __init__(self):
        self.buf = b""
        self.lines: asyncio.Queue = asyncio.Queue()

    def on_notify(self, _handle, data: bytearray):
        self.buf += bytes(data)
        while b"\n" in self.buf:
            line, self.buf = self.buf.split(b"\n", 1)
            if line.strip():
                self.lines.put_nowait((time.monotonic(), line.decode(errors="replace").rstrip("\r")))

    async def expect(self, prefix: str, timeout: float):
        end = time.monotonic() + timeout
        while True:
            remaining = end - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"sin respuesta '{prefix}' en {timeout}s")
            t, line = await asyncio.wait_for(self.lines.get(), remaining)
            if line.startswith(prefix):
                return t, line


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hash", default="4F")
    ap.add_argument("--pings", type=int, default=5)
    ap.add_argument("--name")
    args = ap.parse_args()

    print("escaneando…")
    found = await BleakScanner.discover(timeout=8.0, service_uuids=[NUS])
    pingers = [d for d in found if (d.name or "").startswith("YagiPinger-") and (not args.name or d.name == args.name)]
    if not pingers:
        print("no se encontró ningún YagiPinger-*", file=sys.stderr)
        return 1
    dev = pingers[0]
    print(f"encontrado: {dev.name} ({dev.address})")

    link = Link()
    t0 = time.monotonic()
    async with BleakClient(dev) as client:
        print(f"conectado en {time.monotonic() - t0:.2f} s; MTU={client.mtu_size}")
        await client.start_notify(TX, link.on_notify)

        async def send(cmd: str):
            await client.write_gatt_char(RX, (cmd + "\n").encode(), response=False)

        await send("HELLO");   _, l = await link.expect("#HELLO", 3); print(l)
        assert "proto=1" in l, "proto distinto de 1"
        await send("STOP");    _, l = await link.expect("#OK stop", 3)
        await send("MON off"); _, l = await link.expect("#OK mon=off", 3)
        await send("CFG?");    _, l = await link.expect("#CFG", 3); print(l)
        await send(f"SET hash={args.hash}"); _, l = await link.expect("#OK hash=", 3)

        lat, ok = [], 0
        for i in range(args.pings):
            t_send = time.monotonic()
            await send("PING")
            t_rx, line = await link.expect("P,", 8)
            assert P_LINE.match(line), f"formato inesperado: {line}"
            lat.append((t_rx - t_send) * 1000)
            ok += line.split(",")[3] == "ok"
            print(f"  {line}   ({lat[-1]:.0f} ms)")
            await asyncio.sleep(max(0, 2.0 - (time.monotonic() - t_send)))   # intervalo ≥ 2 s (límite de TX)

        # PING con otro pendiente → !ERR state busy
        await send("PING"); await asyncio.sleep(0.2); await send("PING")
        _, l = await link.expect("!ERR state busy", 3); print("  doble PING:", l)
        await link.expect("P,", 8)

        # Línea de más de 160 B en varios trozos → !ERR syntax
        junk = "X" * 200 + "\n"
        for k in range(0, len(junk), 20):
            await client.write_gatt_char(RX, junk[k:k + 20].encode(), response=False)
        _, l = await link.expect("!ERR syntax", 3); print("  overflow:", l)

        print(f"\n{ok}/{args.pings} ecos; latencia comando→P, media {statistics.mean(lat):.0f} ms "
              f"(mín {min(lat):.0f}, máx {max(lat):.0f})")
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
