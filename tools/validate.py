"""
Confere o motor de reproducao contra invariantes que valem dentro do jogo.

    python tools/validate.py
"""

from __future__ import annotations

import sys
from collections import Counter

from breeding import Breeding


def main() -> int:
    b = Breeding()
    fails: list[str] = []

    # 1) Um Pal cruzado com ele mesmo sempre gera ele mesmo (rank alvo == proprio rank).
    #    Isso so falha se houver empate mal resolvido no CombiDuplicatePriority.
    self_pairs = [p for p in b.pool if not p.is_boss]
    for p in self_pairs:
        child = b.breed(p.id, p.id)["child"]
        if child.id != p.id and child.tribe != p.tribe:
            fails.append(f"{p.id} x {p.id} -> {child.id} (esperado {p.id})")
    print(f"[1] auto-cruzamento: {len(self_pairs) - len([f for f in fails])}/{len(self_pairs)} ok")

    # 2) Toda linha de DT_PalCombiUnique tem que ser reproduzida pelo motor.
    uniq_fail = 0
    uniq_total = 0
    for pair, rows in b.unique.items():
        for u in rows:
            pa = next((p for p in b.pals.values() if p.tribe == u["parent_a"] and not p.is_boss), None)
            pb = next((p for p in b.pals.values() if p.tribe == u["parent_b"] and not p.is_boss), None)
            if pa is None or pb is None:
                continue
            uniq_total += 1
            ga = u["gender_a"] if u["gender_a"] != "None" else "Male"
            gb = u["gender_b"] if u["gender_b"] != "None" else "Female"
            r = b.breed(pa.id, pb.id, ga, gb)
            if r["child"].id != u["child"]:
                uniq_fail += 1
                fails.append(f"unica {u['parent_a']} x {u['parent_b']} -> "
                             f"{r['child'].id} (esperado {u['child']})")
    print(f"[2] combinacoes unicas: {uniq_total - uniq_fail}/{uniq_total} ok")

    # 3) Todo Pal do pool precisa ter um item de ovo resolvido.
    sem_ovo = [p.id for p in b.pool if not p.egg]
    print(f"[3] ovo definido: {len(b.pool) - len(sem_ovo)}/{len(b.pool)} ok")
    if sem_ovo:
        fails.append("sem item de ovo: " + ", ".join(sem_ovo[:10]))

    # panorama util para a UI
    print("\nresumo:")
    print(f"  Pals no pool de resultado : {len(b.pool)}")
    print(f"  tribos distintas          : {len({p.tribe for p in b.pool})}")
    print(f"  tipos de ovo em uso       : {len(b.by_egg)}")
    top = Counter({k: len(v) for k, v in b.by_egg.items()}).most_common(5)
    for egg, n in top:
        print(f"    {egg:<26} {n} Pals")

    if fails:
        print(f"\n{len(fails)} divergencia(s):")
        for f in fails[:25]:
            print("  -", f)
        return 1
    print("\ntudo certo.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
