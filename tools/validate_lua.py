"""
Compara o motor Lua do mod (mod/PalBreedCalc/Scripts) com a implementacao de
referencia em Python, rodando o Lua de verdade via lupa.

    python tools/validate_lua.py [n_amostras]
"""

from __future__ import annotations

import random
import sys
from pathlib import Path

import lupa

from breeding import Breeding, egg_pool_order

ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = ROOT / "mod" / "PalBreedCalc" / "Scripts"


def lua_engine():
    lua = lupa.LuaRuntime(unpack_returned_tuples=True)
    lua.execute(f'package.path = [[{SCRIPTS.as_posix()}/?.lua]] .. ";" .. package.path')
    return lua.execute("""
        local breeding = require("breeding")
        local data = require("data")
        local pool = breeding.buildPool(data.pals)
        local species, speciesByTribe = breeding.buildSpecies(data.pals, pool)
        local db = {
            pals = data.pals,
            pool = pool,
            rankPool = breeding.buildRankPool(data.pals, pool, data.unique),
            species = species,
            speciesByTribe = speciesByTribe,
            uniqueIndex = breeding.indexUnique(data.unique),
            eggs = data.eggs,
        }
        return {
            poolSize = #db.pool,
            speciesSize = #db.species,
            breed = function(a, b, ga, gb)
                local r = breeding.breed(db, db.pals[a], db.pals[b], ga, gb)
                return r.child.id, r.rule
            end,
            eggPool = function(eggId)
                local ids = {}
                for i, p in ipairs(breeding.eggPool(db, eggId)) do ids[i] = p.id end
                return table.concat(ids, ",")
            end,
            pairsFor = function(childId)
                local pairs_ = breeding.pairsFor(db, db.pals[childId])
                local uniq, gendered = 0, 0
                for _, p in ipairs(pairs_) do
                    uniq = uniq + (p.from_unique and 1 or 0)
                    gendered = gendered + (p.gender_specific and 1 or 0)
                end
                return { #pairs_, uniq, gendered }
            end,
        }
    """)


def main() -> int:
    samples = int(sys.argv[1]) if len(sys.argv) > 1 else 5000
    py = Breeding()
    lua = lua_engine()

    print(f"pool: python {len(py.pool)}  lua {lua.poolSize}")
    if len(py.pool) != lua.poolSize:
        print("  divergencia no tamanho do pool")
        return 1

    pairs: list[tuple[str, str, str, str]] = []
    for rows in py.unique.values():                     # todas as combinacoes unicas
        for u in rows:
            pa = next((p for p in py.pool if p.tribe == u["parent_a"]), None)
            pb = next((p for p in py.pool if p.tribe == u["parent_b"]), None)
            if pa and pb:
                ga = u["gender_a"] if u["gender_a"] != "None" else "Male"
                gb = u["gender_b"] if u["gender_b"] != "None" else "Female"
                pairs.append((pa.id, pb.id, ga, gb))

    rng = random.Random(20260722)
    ids = [p.id for p in py.pool]
    pairs += [(rng.choice(ids), rng.choice(ids), "Male", "Female") for _ in range(samples)]

    bad = 0
    for a, b, ga, gb in pairs:
        want = py.breed(a, b, ga, gb)
        child, rule = lua.breed(a, b, ga, gb)
        if child != want["child"].id or rule != want["rule"]:
            bad += 1
            if bad <= 10:
                print(f"  {a} x {b}: lua={child}/{rule} python={want['child'].id}/{want['rule']}")

    print(f"cruzamentos conferidos: {len(pairs) - bad}/{len(pairs)}")

    # o conjunto de Pals por ovo tambem precisa bater
    egg_bad = 0
    for egg, pals in py.by_egg.items():
        want = ",".join(p.id for p in sorted(pals, key=egg_pool_order))
        if lua.eggPool(egg) != want:
            egg_bad += 1
            if egg_bad <= 3:
                print(f"  ovo {egg}:\n    lua   ={lua.eggPool(egg)}\n    python={want}")
    print(f"listas de ovo conferidas: {len(py.by_egg) - egg_bad}/{len(py.by_egg)}")

    # busca reversa (amostrada): total de pares por filhote, quantos unicos e
    # quantos de genero fixo, contra o breed() do proprio Python (species x species)
    forward = {}
    for a in py.species:
        for b in py.species:
            r = py.breed(a.id, b.id)
            forward[(a.id, b.id)] = (r["child"].id, r["rule"])

    sample = [p for i, p in enumerate(py.species) if i % 6 == 0]
    reverse_bad = 0
    for child in sample:
        want_total = want_unique = want_gendered = 0
        for i, a in enumerate(py.species):
            for b in py.species[i:]:
                ab = forward[(a.id, b.id)]
                ba = forward[(b.id, a.id)]
                ab_ok = ab[0] == child.id
                ba_ok = ba[0] == child.id
                if not ab_ok and not ba_ok:
                    continue
                want_total += 1
                rule = ab[1] if ab_ok else ba[1]
                want_unique += 1 if rule == "unique" else 0
                want_gendered += 1 if ab_ok != ba_ok else 0
        got = lua.pairsFor(child.id)
        if (want_total, want_unique, want_gendered) != (got[1], got[2], got[3]):
            reverse_bad += 1
            print(f"  reversa {child.id}: lua={got} python="
                  f"({want_total},{want_unique},{want_gendered})")
    print(f"busca reversa conferida: {len(sample) - reverse_bad}/{len(sample)} filhotes")

    return 1 if (bad or egg_bad or reverse_bad) else 0


if __name__ == "__main__":
    sys.exit(main())
