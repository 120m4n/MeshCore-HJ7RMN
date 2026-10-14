#!/usr/bin/env python3
"""Guardián de deriva de upstream (spec 002, FR-004).

Extrae del core de Meshcore los fragmentos de los que depende el pinger (W1..W10 de
specs/002-trace-frame-host-tests/contracts/upstream-watch.md), los normaliza y compara su
SHA-256 con tests/upstream-trace.lock.

Códigos de salida: 0 = sin deriva, 1 = deriva (diff por stderr), 2 = ancla ausente/ambigua o lock inválido.
  python3 check_upstream_trace.py            # comprobar
  python3 check_upstream_trace.py --update   # reescribir el lock tras revisar un cambio
Solo stdlib.
"""
import argparse
import difflib
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_ROOT = HERE.parents[2]  # <raíz de Meshcore>/examples/trace_pinger/tools/<este archivo>
DEFAULT_LOCK = HERE.parent / "tests" / "upstream-trace.lock"

# id: (archivo, tipo, ancla de inicio, ancla de fin)
#  "lines": desde la línea con el inicio hasta la línea con el fin (inclusive).
#  "block": desde el inicio hasta el cierre de la llave del bloque; si hay ancla de fin, hasta el
#           cierre del bloque que empieza en esa ancla (W7: de writeTo al final de readFrom).
FRAGMENTS = {
    "W1": ("src/Mesh.cpp", "block", "if (pkt->isRouteDirect() && pkt->getPayloadType() == PAYLOAD_TYPE_TRACE) {", None),
    "W2": ("src/Mesh.cpp", "block", "Packet* Mesh::createTrace(", None),
    "W3": ("src/Packet.h", "lines", "#define PH_ROUTE_MASK", "#define PAYLOAD_TYPE_RAW_CUSTOM"),
    "W4": ("src/Packet.h", "lines", "uint8_t getPathHashSize()", "void setPathHashSizeAndCount("),
    "W5": ("src/Packet.cpp", "block", "bool Packet::isValidPathLen(", None),
    "W6": ("src/Packet.cpp", "block", "void Packet::calculatePacketHash(", None),
    "W7": ("src/Packet.cpp", "block", "uint8_t Packet::writeTo(", "bool Packet::readFrom("),
    "W8": ("src/MeshCore.h", "lines", "#define MAX_HASH_SIZE", "#define MAX_TRANS_UNIT"),
    "W9": ("examples/simple_repeater/MyMesh.cpp", "block", "uint32_t MyMesh::getDirectRetransmitDelay(", None),
    "W10": ("examples/simple_repeater/MyMesh.cpp", "block", "bool MyMesh::allowPacketForward(", None),
}


class AnchorError(Exception):
    pass


def strip_comments(src):
    """Quita // y /* */ respetando literales de cadena y de carácter. Conserva los saltos de línea."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c in "\"'":
            q, j = c, i + 1
            while j < n and src[j] != q:
                j += 2 if src[j] == "\\" else 1
            out.append(src[i:j + 1])
            i = j + 1
        elif src.startswith("//", i):
            while i < n and src[i] != "\n":
                i += 1
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * src.count("\n", i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def block_end(text, pos):
    """Índice justo después de la llave que cierra el primer bloque {...} que empieza en/tras `pos`."""
    depth, i, n, started = 0, pos, len(text), False
    while i < n:
        c = text[i]
        if c in "\"'":
            q, i = c, i + 1
            while i < n and text[i] != q:
                i += 2 if text[i] == "\\" else 1
        elif c == "{":
            depth += 1
            started = True
        elif c == "}":
            depth -= 1
            if started and depth == 0:
                return i + 1
        i += 1
    raise AnchorError("llave de cierre no encontrada")


def find_unique(text, anchor, frag, what):
    count = text.count(anchor)
    if count != 1:
        raise AnchorError(f"{frag}: ancla de {what} {'no encontrada' if count == 0 else 'ambigua (%d)' % count}: {anchor!r}")
    return text.index(anchor)


def extract(root, frag):
    rel, kind, start, end = FRAGMENTS[frag]
    path = root / rel
    if not path.is_file():
        raise AnchorError(f"{frag}: archivo ausente: {rel}")
    text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
    a = find_unique(text, start, frag, "inicio")
    if kind == "lines":
        b = find_unique(text, end, frag, "fin")
        line_start = text.rfind("\n", 0, a) + 1
        line_end = text.find("\n", b)
        piece = text[line_start:(len(text) if line_end < 0 else line_end)]
    else:
        stop = block_end(text, find_unique(text, end, frag, "fin") if end else a)
        piece = text[a:stop]
    return " ".join(piece.split())  # colapsa blancos y saltos de línea


def sha(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def readable(text):
    return re.sub(r"([;{}])", r"\1\n", text).splitlines()


def git_head(root):
    try:
        return subprocess.run(["git", "-C", str(root), "rev-parse", "--short", "HEAD"], capture_output=True,
                              text=True, check=True).stdout.strip()
    except Exception:
        return ""


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", type=Path, default=DEFAULT_ROOT, help="raíz del repo Meshcore (o una copia)")
    ap.add_argument("--lock", type=Path, default=DEFAULT_LOCK)
    ap.add_argument("--update", action="store_true", help="reescribir el lock")
    args = ap.parse_args()

    try:
        current = {f: extract(args.root, f) for f in FRAGMENTS}
    except AnchorError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    if args.update:
        lock = {"commit": git_head(args.root),
                "fragments": {f: {"sha256": sha(t), "chars": len(t), "text": t} for f, t in current.items()}}
        args.lock.write_text(json.dumps(lock, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"lock actualizado: {args.lock} ({len(current)} fragmentos)")
        return 0

    try:
        lock = json.loads(args.lock.read_text(encoding="utf-8"))["fragments"]
    except Exception as e:
        print(f"ERROR: lock ilegible ({args.lock}): {e}", file=sys.stderr)
        return 2

    drift = 0
    for f, text in current.items():
        old = lock.get(f)
        if old is None:
            print(f"ERROR: {f} no está en el lock; ejecuta --update tras revisar", file=sys.stderr)
            return 2
        if sha(text) == old["sha256"]:
            print(f"ok    {f} ({FRAGMENTS[f][0]})")
            continue
        drift = 1
        print(f"DRIFT {f} ({FRAGMENTS[f][0]})", file=sys.stderr)
        diff = difflib.unified_diff(readable(old.get("text", "")), readable(text), "lock", "actual", lineterm="")
        print("\n".join(diff), file=sys.stderr)
    if drift:
        print("\nupstream cambió un fragmento vigilado. Revisa el diff; si afecta la semántica actualiza\n"
              "RepeaterModel, el contrato de trama y los tests, y luego ejecuta con --update.", file=sys.stderr)
    return drift


if __name__ == "__main__":
    sys.exit(main())
