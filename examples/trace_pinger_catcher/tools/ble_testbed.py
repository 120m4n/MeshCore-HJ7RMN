#!/usr/bin/env python3
"""Testbed BLE de la tarjeta pinger+catcher (hardware real, sin la app).

Se conecta como lo hace MeshAzimuth (NUS, una sola conexión) y valida el protocolo 3:
HELLO, MODE, gating por modo, MEAS contra una baliza CW real, PING, comandos eliminados.

    pip install bleak
    python3 tools/ble_testbed.py [--freq 914.000] [--min-dbm -70] [--name MeshAz-735E58C8]

Requisitos: la placa flasheada (recién arrancada o no), una baliza CW en --freq a poca distancia
(el MEAS debe superar --min-dbm) y permiso de Bluetooth para la terminal. No hace falta otra
placa: el PING solo comprueba que transmite (acaba en `timeout`, nadie responde al TRACE).
Sale con código 1 si falla alguna comprobación.
"""
import argparse
import asyncio
import sys

from bleak import BleakClient, BleakScanner

RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # escritura (host -> placa)
TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # notify (placa -> host)
PREFIX = "MeshAz-"

results = []
lines = asyncio.Queue()
partial = ""


def on_notify(_, data):
    global partial
    partial += data.decode(errors="replace")
    while "\n" in partial:
        line, partial = partial.split("\n", 1)
        if line.strip():
            lines.put_nowait(line.strip())


async def cmd(cli, text, timeout=3.0, until=None):
    """Envía una línea y devuelve las respuestas hasta la primera que empiece por `until`
    (o hasta `timeout` sin más líneas). Vacía antes las líneas tardías de órdenes anteriores."""
    while not lines.empty():
        lines.get_nowait()
    await cli.write_gatt_char(RX, (text + "\n").encode(), response=False)
    out = []
    try:
        while True:
            line = await asyncio.wait_for(lines.get(), timeout)
            out.append(line)
            if until is None or line.startswith(until):
                break
    except asyncio.TimeoutError:
        pass
    return out


def check(name, ok, info=""):
    results.append(bool(ok))
    print(("PASS " if ok else "FAIL ") + name, info)


async def run(a):
    found = await BleakScanner.discover(10, return_adv=True)
    names = {k: (adv.local_name or d.name or "") for k, (d, adv) in found.items()}
    boards = [(k, n) for k, n in names.items() if n.startswith(PREFIX) and (not a.name or n == a.name)]
    check(f"escaneo {PREFIX}*", boards, [n for _, n in boards])
    print("   placas legacy visibles (deben ser 0):", [n for n in names.values() if n.startswith("PingPong")])
    if not boards:
        return
    async with BleakClient(found[boards[0][0]][0]) as cli:
        await cli.start_notify(TX, on_notify)
        await asyncio.sleep(0.5)

        o = await cmd(cli, "HELLO")
        check("HELLO proto=3 fw=0.8.0", any("proto=3" in l and "fw=0.8.0" in l for l in o), o)

        await cmd(cli, "MODE pinger"); await cmd(cli, "MODE catcher")   # estado conocido sea cual sea el de partida
        o = await cmd(cli, "MODE catcher")
        check("MODE catcher idempotente", o == ["#OK mode=catcher"], o)
        o = await cmd(cli, "PING")
        check("catcher rechaza PING", o == ["!ERR mode"], o)

        o = await cmd(cli, f"SET freq={a.freq}")
        check("SET freq", o and o[0].startswith("#OK"), o)
        o = await cmd(cli, "MEAS 64", until="M,")
        m = [l for l in o if l.startswith("M,")]
        if m:
            f = m[0].split(",")
            mean, sd = float(f[3]), float(f[4])
            check(f"MEAS baliza (media > {a.min_dbm} dBm)", mean > a.min_dbm, m[0])
            check("MEAS estable (sd < 3 dB)", sd < 3, f"sd={sd}")
        else:
            check("MEAS responde", False, o)

        o = await cmd(cli, "MODE pinger")
        check("MODE pinger", o == ["#OK mode=pinger"], o)
        o = await cmd(cli, "CFG?")
        check("radio vuelve a 915.800 al cambiar de modo", any("freq=915.800" in l for l in o), o)
        o = await cmd(cli, "MEAS 16")
        check("pinger rechaza MEAS", o == ["!ERR mode"], o)

        o = await cmd(cli, "CFG?")
        if any("hash= " in l or l.rstrip().endswith("hash=") for l in o):   # sin hash tras arrancar
            o = await cmd(cli, "PING 1")
            check("PING sin hash -> !ERR state nohash", o == ["!ERR state nohash"], o)
        o = await cmd(cli, "SET hash=AB")
        check("SET hash", o and o[0].startswith("#OK"), o)
        o = await cmd(cli, "PING 1", timeout=8.0, until="S,")   # P,… y luego el resumen S,…
        print("   PING 1 ->", o)
        check("PING transmite (P,… sin !ERR)", any(l.startswith("P,") for l in o) and not any(l.startswith("!ERR") for l in o))
        await cmd(cli, "STOP", until="#OK")

        o = await cmd(cli, "MODE catcher", timeout=4)
        check("vuelta a catcher sin reconectar", o == ["#OK mode=catcher"] and cli.is_connected, o)
        await cmd(cli, f"SET freq={a.freq}", until="#OK")
        o = await cmd(cli, "MEAS 64", until="M,")
        check("MEAS tras ida y vuelta", any(l.startswith("M,") for l in o), o)

        for c in ("PONG", "RF", "LAST?", "MON on", "SAVE"):
            o = await cmd(cli, c)
            check(f"{c} eliminado", o and o[0].startswith("!ERR unknown"), o)

    print(f"\n{sum(results)}/{len(results)} OK")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--freq", default="914.000", help="MHz de la baliza CW (por defecto 914.000)")
    p.add_argument("--min-dbm", type=float, default=-70, help="mínimo aceptable de la media del MEAS")
    p.add_argument("--name", help="nombre BLE exacto si hay varias placas")
    asyncio.run(run(p.parse_args()))
    sys.exit(0 if all(results) else 1)
