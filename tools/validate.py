"""
Confere o motor de reproducao contra invariantes que valem dentro do jogo.

    python tools/validate.py
"""

from __future__ import annotations

import sys
from collections import Counter

from breeding import Breeding

# Cruzamentos com resultado conhecido no jogo (Palworld 1.0), tirados das
# paginas de cada Pal no Game8. Servem de amarra para a regra de desempate:
# quase todo CombiRank e multiplo de 10, entao metade dos pares empata entre
# dois ranks vizinhos e so um criterio de desempate acerta estes 15.
#
#   game8.co/games/Palworld/archives/439909  (Lamball)
#   game8.co/games/Palworld/archives/439928  (Daedream)

# Conferidos com https://palbreeding.com/ (Palworld 1.0).
#   287 = os 204 numeros do Paldex + 84 variantes - Astralym (#204, o unico Pal
#         sem elemento e sem item de ovo: e o chefe final, nao tem spawn e nao
#         entra na fazenda de reproducao).
#   O site anuncia 299 Pals, que sao esses 287 + Astralym + 11 monstros da
#   colaboracao com Terraria (Green Slime, Eye of Cthulhu, ...), que nao se
#   reproduzem e por isso nao entram na lista de pais.
EXPECTED_SPECIES = 287
ANUBIS_PAIRS = 234

KNOWN_COMBOS = [
    ("Lamball", "Cattiva", "Daedream"),
    ("Venusa", "Daedream", "Reindrix"),
    ("Gloopie Primo", "Daedream", "Univolt"),
    ("Panthalus", "Daedream", "Incineram"),
    ("Petallia Ignis", "Daedream", "Valentail"),
    ("Finsider", "Daedream", "Direhowl"),
    ("Solmora Lux", "Daedream", "Fenglope"),
    ("Wispaw", "Daedream", "Jellroy"),
    ("Venusa", "Lamball", "Lunaris"),
    ("Bakemi", "Lamball", "Dumud"),
    ("Turtacle", "Lamball", "Tocotoco"),
    ("Polapup", "Lamball", "Puffolt"),
    ("Solmora", "Lamball", "Dazemu"),
    ("Panthalus", "Lamball", "Bakemi"),
    ("Wispaw", "Lamball", "Jelliette"),
]


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

    # 4) Resultados conferidos com o JOGO (paginas do Game8, Palworld 1.0).
    #    Estes casos existem porque a regra de desempate estava invertida e
    #    ninguem percebia: eles sao pares comuns, do inicio do jogo, em que o
    #    rank alvo cai exatamente entre dois Pals.
    known_fail = 0
    for na, nb, esperado in KNOWN_COMBOS:
        pa, pb, pe = b.find(na), b.find(nb), b.find(esperado)
        if pa is None or pb is None or pe is None:
            known_fail += 1
            fails.append(f"conhecido: nome nao encontrado em {na} x {nb} -> {esperado}")
            continue
        got = b.breed(pa.id, pb.id)["child"]
        if got.tribe != pe.tribe:
            known_fail += 1
            fails.append(f"conhecido: {na} x {nb} -> {got.name('en')} "
                         f"(o jogo da {esperado})")
    print(f"[4] combos conferidos com o jogo: "
          f"{len(KNOWN_COMBOS) - known_fail}/{len(KNOWN_COMBOS)} ok")

    # 5) Cobertura da lista de selecao. Os numeros vem de fora (palbreeding.com
    #    e o Paldex) -- depois de um patch do jogo eles mudam e precisam ser
    #    reconferidos, nao "consertados" no braco.
    zukan_na_lista = {p.zukan for p in b.species if p.zukan and p.zukan > 0}
    zukan_na_dt = {p.zukan for p in b.pals.values() if p.zukan and p.zukan > 0}
    sem_numero = [p.id for p in b.species if not p.zukan or p.zukan <= 0]
    # Um numero so pode faltar se aquele Pal nao tem item de ovo (nao entra na
    # fazenda). Hoje isso vale so para Astralym (#204).
    faltando = []
    for z in sorted(zukan_na_dt - zukan_na_lista):
        linhas = [p for p in b.pals.values() if p.zukan == z]
        if any(p.egg for p in linhas):
            faltando.append(f"#{z} ({linhas[0].name('en')})")
    print(f"[5] Paldex na lista: {len(zukan_na_lista)}/{len(zukan_na_dt)} numeros "
          f"(fora: so os sem ovo), {len(b.species)} selecionaveis")
    if faltando:
        fails.append(f"Pals com ovo fora da lista: {faltando}")
    if sem_numero:
        fails.append(f"selecionaveis sem numero de Paldex: {sem_numero}")
    if len(b.species) != EXPECTED_SPECIES:
        fails.append(f"lista com {len(b.species)} especies (esperado {EXPECTED_SPECIES})")

    # 6) Contagem da busca reversa, conferida com o palbreeding.com.
    anubis = b.find("Anubis")
    pares = 0
    for i, x in enumerate(b.species):
        for y in b.species[i:]:
            if (b.breed(x.id, y.id)["child"].tribe == anubis.tribe
                    or b.breed(y.id, x.id)["child"].tribe == anubis.tribe):
                pares += 1
    print(f"[6] pares que geram Anubis: {pares} (palbreeding.com: {ANUBIS_PAIRS})")
    if pares != ANUBIS_PAIRS:
        fails.append(f"Anubis tem {pares} pares (palbreeding.com diz {ANUBIS_PAIRS})")

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
