"""
Compara o motor C++ do mod com a implementacao de referencia em Python,
cruzando TODOS os pares possiveis (pool x pool).

Requer o executavel de conferencia:
    powershell -ExecutionPolicy Bypass -File mod-cpp\\build.ps1
    python tools/validate_cpp.py
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from breeding import Breeding

ROOT = Path(__file__).resolve().parent.parent
DUMP = ROOT / "mod-cpp" / "build" / "engine_dump.exe"


def main() -> int:
    if not DUMP.exists():
        print(f"{DUMP.relative_to(ROOT)} nao existe -- compile o mod primeiro")
        return 2

    print("rodando o motor C++...")
    out = subprocess.run([str(DUMP)], check=True, capture_output=True, text=True).stdout
    rows = [line.split(";") for line in out.splitlines()[1:] if line]
    print(f"{len(rows)} cruzamentos")

    py = Breeding()
    egg_sizes = {egg: len(pals) for egg, pals in py.by_egg.items()}

    bad = 0
    for male, female, child, rule, egg, egg_pool in rows:
        want = py.breed(male, female)
        want_egg = want["child"].egg or ""
        if (child != want["child"].id or rule != want["rule"]
                or egg != want_egg or int(egg_pool) != egg_sizes.get(want_egg, 0)):
            bad += 1
            if bad <= 10:
                print(f"  {male} x {female}: cpp={child}/{rule}/{egg} "
                      f"python={want['child'].id}/{want['rule']}/{want_egg}")

    print(f"conferidos: {len(rows) - bad}/{len(rows)}")

    # busca inversa: os mesmos cruzamentos, agrupados por filhote. Um par conta
    # uma vez so, mesmo quando as duas ordens de genero funcionam.
    forward = {(male, female): (child, rule) for male, female, child, rule, _, _ in rows}
    ids = [p.id for p in py.pool]
    expected: dict[str, list[tuple]] = {}
    for i, a in enumerate(ids):
        for b in ids[i:]:
            ab = forward[(a, b)]
            ba = forward[(b, a)]
            for child, rule, gender_specific in ((ab[0], ab[1], ab[0] != ba[0]),):
                expected.setdefault(child, []).append((rule == "unique", gender_specific))
            if ab[0] != ba[0]:
                expected.setdefault(ba[0], []).append((ba[1] == "unique", True))

    out = subprocess.run([str(DUMP), "reverse"], check=True,
                         capture_output=True, text=True).stdout
    reverse_bad = 0
    reverse_rows = [line.split(";") for line in out.splitlines()[1:] if line]
    for child, pairs, unique_pairs, gender_specific in reverse_rows:
        mine = expected.get(child, [])
        want = (len(mine), sum(1 for u, _ in mine if u), sum(1 for _, g in mine if g))
        got = (int(pairs), int(unique_pairs), int(gender_specific))
        if want != got:
            reverse_bad += 1
            if reverse_bad <= 10:
                print(f"  {child}: cpp={got} python={want}")
    print(f"busca inversa: {len(reverse_rows) - reverse_bad}/{len(reverse_rows)}")

    return 1 if (bad or reverse_bad) else 0


if __name__ == "__main__":
    sys.exit(main())
