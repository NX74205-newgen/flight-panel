"""Generates src/airlines_db.h from Wikipedia "List of airline codes" (CC BY-SA): ICAO -> name.

IATA codes are not included: they are widely reused (FS = FlyArystan and Flyr, DV = SCAT and
Nantucket...), and without a reliable "currently operating" flag the pick would be random. FR24
callsigns carry the ICAO code (KZR921 -> KZR), which is enough.

Usage:  python tools/genairlines.py [cache-dir]
Downloads 27 Wikipedia subpages (0-9, A..Z) as wikitext. With a cache dir they are saved/reused.

ICAO codes are reused too (one code may have several airlines in its history), so for each code
the "best" entry is chosen: one with an active IATA code and no signs of having ceased operations.
"""
import re
import subprocess
import sys
import time
import unicodedata
import urllib.parse
import urllib.error
import urllib.request
from pathlib import Path

PAGES = ["0–9"] + [chr(c) for c in range(ord("A"), ord("Z") + 1)]
UA = "FlightPanel/1.0 (personal LED panel project; generator script)"
MAX_LEN = 26
DEAD = re.compile(r"defunct|ceased|bankrupt|dissolved|merged|liquidat|former|previous|no longer|"
                  r"renamed|rebrand|replaced|suspended|closed|absorbed|became|historic|until \d{4}|"
                  r"\b(19|20)\d\d\s*[-–]\s*(19|20)\d\d", re.I)
LEGAL = re.compile(r"[,\s]+(S\.?A\.?|S\.?A\.?S\.?|Ltd\.?|Limited|Inc\.?|LLC|L\.L\.C\.|JSC|PJSC|OJSC|CJSC|"
                   r"GmbH|AG|B\.?V\.?|N\.?V\.?|Co\.?|Corp\.?|Corporation|Company|PLC|Pty|S\.p\.A\.|"
                   r"S\.?R\.?L\.?|Sdn Bhd|Bhd|K\.K\.)$", re.I)


def fetch(page: str, cache: Path | None) -> str:
    if cache:
        f = cache / f"airline_codes_{page}.txt"
        if f.exists():
            return f.read_text(encoding="utf-8")
    url = ("https://en.wikipedia.org/w/index.php?action=raw&title="
           + urllib.parse.quote(f"List of airline codes ({page})"))
    for attempt in range(4):
        try:
            try:
                req = urllib.request.Request(url, headers={"User-Agent": UA})
                with urllib.request.urlopen(req, timeout=60) as r:
                    text = r.read().decode("utf-8")
            except urllib.error.URLError:
                # Python's certificate store can be outdated — fall back to the system curl
                text = subprocess.run(["curl", "-sf", "-m", "60", "-A", UA, url],
                                      capture_output=True, check=True).stdout.decode("utf-8")
            if text.lstrip().startswith("<!DOCTYPE"):
                raise RuntimeError("error page")
            break
        except Exception:  # noqa: BLE001
            if attempt == 3:
                raise
            time.sleep(2 + attempt * 3)
    if cache:
        cache.mkdir(parents=True, exist_ok=True)
        (cache / f"airline_codes_{page}.txt").write_text(text, encoding="utf-8")
    time.sleep(0.5)
    return text


def clean(cell: str) -> str:
    s = re.sub(r"<ref[^>]*/>|<ref[^>]*>.*?</ref>", "", cell, flags=re.S)
    s = re.sub(r"\{\{[^{}]*\}\}", "", s)
    s = re.sub(r"\[\[(?:[^\]|]*\|)?([^\]]*)\]\]", r"\1", s)        # [[link|text]] -> text
    s = re.sub(r"\[https?://\S+\s+([^\]]*)\]", r"\1", s)            # [url text] -> text
    s = re.sub(r"<[^>]+>", "", s)
    s = re.sub(r"'''?", "", s)
    s = s.replace("&nbsp;", " ").replace("&amp;", "&")
    return re.sub(r"\s+", " ", s).strip()


def ascii_fold(s: str) -> str:
    s = unicodedata.normalize("NFKD", s)
    return "".join(c for c in s if ord(c) < 128 and not unicodedata.combining(c))


def short_name(name: str) -> str:
    name = re.sub(r"\s*\(.*?\)\s*", " ", name).strip()
    for _ in range(2):
        name = LEGAL.sub("", name).strip(" ,")
    name = ascii_fold(name).replace("\\", "").replace('"', "'").strip()
    return name[:MAX_LEN].rstrip(" ,-")


def rows(text: str):
    # table: rows are separated by "|-", cells are lines starting with "|" (or separated by "||")
    body = text.split("{|", 1)[-1]
    for chunk in re.split(r"\n\|-[^\n]*", body):
        cells = []
        for line in chunk.split("\n"):
            if line.startswith("|}") or line.startswith("{|") or line.startswith("|+") or line.startswith("!"):
                continue
            if line.startswith("|"):
                cells += [c for c in line[1:].split("||")]
            elif cells:
                cells[-1] += " " + line
        if len(cells) >= 3:
            yield [clean(c) for c in cells] + [""] * (6 - len(cells))


def main():
    cache = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    best_icao = {}
    total = 0
    for page in PAGES:
        for iata, icao, name, _tel, _country, comment, *_ in rows(fetch(page, cache)):
            iata, icao = iata.strip().upper(), icao.strip().upper()
            name = short_name(name)
            if not name or len(name) < 2:
                continue
            total += 1
            dead = bool(DEAD.search(comment)) or bool(DEAD.search(name))
            score = (0 if dead else 4) + (2 if re.fullmatch(r"[A-Z0-9]{2}", iata) else 0)
            if re.fullmatch(r"[A-Z]{3}", icao):
                if icao not in best_icao or score > best_icao[icao][0]:
                    best_icao[icao] = (score, name)

    def emit(prefix, table, width):
        codes = sorted(table)
        out = [f"static constexpr int {prefix}_SIZE = {len(codes)};",
               f"static const char {prefix}_CODES[] ="]
        per = 72 // width
        for i in range(0, len(codes), per):
            out.append('  "' + "".join(codes[i:i + per]) + '"')
        out[-1] += ";"
        out.append(f"static const char *const {prefix}_NAME[] = {{")
        for i in range(0, len(codes), 5):
            out.append("  " + " ".join(f'"{table[c][1]}",' for c in codes[i:i + 5]))
        out.append("};")
        return out

    out = ["// Generated by tools/genairlines.py from Wikipedia \"List of airline codes\" (CC BY-SA 4.0).",
           "// Do not edit by hand.", "#pragma once", ""]
    out += emit("AIRLINE_ICAO", best_icao, 3)
    dst = Path(__file__).resolve().parent.parent / "src" / "airlines_db.h"
    dst.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"{dst}: {total} rows -> {len(best_icao)} ICAO codes")


if __name__ == "__main__":
    main()
