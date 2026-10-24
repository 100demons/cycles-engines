"""Recompression aPLib (variante Elektron, tools/mtlib/aplib.py) d'une section AGRANDIE (notes/17).

mtlib.aplib.repack ré-émet le flux d'origine et passe en littéraux les octets modifiés ; il ne sait pas
ajouter d'octets après la fin. repack_grow fait la même chose pour la partie d'origine, puis compresse la
partie ajoutée avec un compresseur glouton simple (correspondances de 4 octets et plus, dans la partie
ajoutée seulement). Mêmes règles d'encodage que _Writer.match : décalage >= 1, longueur >= 2
(>= 3 au-delà de FAR_THRESHOLD), réutilisation du dernier décalage quand il est égal.
"""
from mtlib import aplib

MIN_MATCH = 4
MAX_MATCH = 0xFFFF
WINDOW = 1 << 20


def _tail(w, data, start, last_off):
    n = len(data)
    table = {}
    i = start
    while i < n:
        best_len = best_off = 0
        if i + MIN_MATCH <= n:
            key = data[i:i + MIN_MATCH]
            cand = table.get(key)
            if cand is not None and i - cand <= WINDOW:
                length = MIN_MATCH
                limit = min(n - i, MAX_MATCH)
                while length < limit and data[cand + length] == data[i + length]:
                    length += 1
                best_len, best_off = length, i - cand
            table[key] = i
        if best_len >= MIN_MATCH:
            w.match(best_off, best_len, last_off)
            last_off = best_off
            for k in range(i + 1, min(i + best_len, n - MIN_MATCH + 1)):
                table[data[k:k + MIN_MATCH]] = k
            i += best_len
        else:
            w.literal(data[i])
            i += 1
    return last_off


def repack_grow(data, ops, dirty, orig_len):
    """Comme aplib.repack pour data[:orig_len] (dirty couvre tout data), puis la partie ajoutée."""
    w = aplib._Writer()
    last_off = 1
    lo = dirty.find(b"\x01", 0, orig_len)
    hi = dirty.rfind(b"\x01", 0, orig_len)
    if lo < 0:
        lo = hi = -1
    for kind, pos, off, n in ops:
        if kind == aplib.LITERAL:
            w.literal(data[pos])
            continue
        end = pos + n
        src = pos - off
        touched = False
        if lo >= 0 and src <= hi and end > lo:
            touched = (dirty.find(b"\x01", pos, end) >= 0 or dirty.find(b"\x01", src, src + n) >= 0)
        if touched:
            for k in range(pos, end):
                w.literal(data[k])
        else:
            w.match(off, n, last_off)
            last_off = off
    _tail(w, data, orig_len, last_off)
    w.end()
    body = w.o
    stream_len = len(body) - aplib.SECT_HDR
    body[0:4] = stream_len.to_bytes(4, "big")
    body[4:8] = (sum(body[aplib.SECT_HDR:]) & 0xFFFFFFFF).to_bytes(4, "big")
    return bytes(body)


def pack(data):
    """Flux aPLib (même variante, même compresseur glouton que la partie ajoutée), SANS l'en-tête de 8 o : charge
    utile rangée compressée dans l'image (append.compress = "aplib", notes/50), décompressée au démarrage par
    tools/machines/syntakt_bridge/stub.S (APLIB). Même algorithme octet pour octet que aplibPack de
    docs/flasher/builder.js."""
    w = aplib._Writer()
    _tail(w, data, 0, 1)
    w.end()
    return bytes(w.o[aplib.SECT_HDR:])
