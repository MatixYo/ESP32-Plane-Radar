#!/usr/bin/env python3
"""Seed src/data/city_exonyms_data.cpp with Italian exonyms for airport cities.

Sources:
  * Wikidata  — airport (P238 IATA) -> served place (P931) -> en / it labels
  * OurAirports airports.csv — the set of real `municipality` strings, used to
    drop Wikidata noise (regions, counties, islands that host no airport city)

The Italian side is transliterated to ASCII because the embedded VLW UI font
only ships the Latin basic glyphs.

This is a SEED, not a source of truth: Wikidata mixes plain transliterations
("Celjabinsk") in with real exonyms ("Mosca"). Review the diff and prune before
committing — the checked-in list is hand-curated.
"""

from __future__ import annotations

import csv
import io
import re
import sys
import unicodedata
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_CPP = ROOT / "src" / "data" / "city_exonyms_data.cpp"

AIRPORTS_URL = (
    "https://raw.githubusercontent.com/davidmegginson/ourairports-data/main/"
    "airports.csv"
)
WDQS_URL = "https://query.wikidata.org/sparql"
SPARQL = """
SELECT DISTINCT ?enLabel ?itLabel WHERE {
  ?airport wdt:P238 ?iata .
  ?airport wdt:P931 ?place .
  ?place rdfs:label ?enLabel . FILTER(LANG(?enLabel) = "en")
  ?place rdfs:label ?itLabel . FILTER(LANG(?itLabel) = "it")
  FILTER(STR(?enLabel) != STR(?itLabel))
}
"""

UA = "ESP32-Plane-Radar exonym builder (github.com/MatixYo/ESP32-Plane-Radar)"

# Italian words that mark a Wikidata description / admin area rather than a city.
BAD_IT = re.compile(
    r"\b(provincia|contea|governatorato|regione|distretto|prefettura|"
    r"dipartimento|circondario|municipalit|comune di|reggenza|lega|atollo|"
    r"isola|isole|arcipelago|penisola|fiume|lago|monte|montagna|baia|"
    r"circuito|gran premio|sport|county|region|province|department|"
    r"in [A-Z])\b",
    re.I,
)
_TRANS = {
    "ø": "o", "Ø": "O", "ł": "l", "Ł": "L", "đ": "d", "Đ": "D", "ß": "ss",
    "æ": "ae", "Æ": "Ae", "œ": "oe", "Œ": "Oe", "ð": "d", "þ": "th", "ı": "i",
}


def deaccent(s: str) -> str:
    out = []
    for ch in unicodedata.normalize("NFKD", s):
        if unicodedata.combining(ch):
            continue
        out.append(_TRANS.get(ch, ch))
    return "".join(out)


def normalize_key(s: str) -> str:
    return re.sub(r"\s+", " ", deaccent(s).strip().lower())


def fetch_airport_municipalities() -> set[str]:
    with urllib.request.urlopen(AIRPORTS_URL, timeout=90) as resp:
        text = resp.read().decode("utf-8")
    keep = {"large_airport", "medium_airport"}
    out: set[str] = set()
    for row in csv.DictReader(io.StringIO(text)):
        if row.get("type") not in keep:
            continue
        muni = (row.get("municipality") or "").strip()
        if muni:
            out.add(normalize_key(muni))
    return out


def fetch_wikidata_pairs() -> list[tuple[str, str]]:
    url = WDQS_URL + "?" + urllib.parse.urlencode({"query": SPARQL})
    req = urllib.request.Request(
        url, headers={"Accept": "text/csv", "User-Agent": UA}
    )
    with urllib.request.urlopen(req, timeout=120) as resp:
        text = resp.read().decode("utf-8")
    return [
        (r["enLabel"].strip(), r["itLabel"].strip())
        for r in csv.DictReader(io.StringIO(text))
    ]


def build_rows() -> list[tuple[str, str]]:
    municipalities = fetch_airport_municipalities()
    pairs = fetch_wikidata_pairs()

    rows: dict[str, str] = {}
    for en, it in pairs:
        if not en or not it or it[0].islower():
            continue
        if BAD_IT.search(it) or len(it) > 22 or len(en) > 30:
            continue
        key = normalize_key(en)
        if key not in municipalities:
            continue
        it_ascii = deaccent(it)
        if not re.fullmatch(r"[A-Za-z .'\-]+", it_ascii):
            continue
        if normalize_key(it_ascii) == key:
            continue
        rows.setdefault(key, it_ascii)

    return sorted(rows.items())


def render_cpp(rows: list[tuple[str, str]]) -> str:
    lines = [
        "// SEED from scripts/build_city_exonyms.py — hand-curate before trusting.",
        '#include "data/city_exonyms.h"',
        "",
        "namespace data::city_exonyms {",
        "",
        "const Exonym kExonyms[] = {",
    ]
    for en, it in rows:
        lines.append(f'    {{"{en}", "{it}"}},')
    lines += [
        "};",
        "",
        "const size_t kExonymCount = sizeof(kExonyms) / sizeof(kExonyms[0]);",
        "",
        "}  // namespace data::city_exonyms",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    rows = build_rows()
    if "--write" in sys.argv:
        OUT_CPP.write_text(render_cpp(rows), encoding="utf-8")
        print(f"wrote {OUT_CPP.relative_to(ROOT)} ({len(rows)} entries) — REVIEW IT")
    else:
        sys.stdout.write(render_cpp(rows))
        print(f"\n// {len(rows)} candidates — pass --write to overwrite the .cpp",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
