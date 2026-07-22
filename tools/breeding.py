"""
Motor de reproducao — implementacao de referencia (Python).

Espelha o que o jogo faz em UPalCombiMonsterParameter::FindChildCharacterID:
  1. combinacao unica (DT_PalCombiUnique) tem prioridade, em qualquer ordem
     de pais e respeitando o genero quando a linha exige um;
  2. senao, rank alvo = floor((rankA + rankB + 1) / 2) e escolhe-se o Pal com
     o CombiRank mais proximo (UPalDatabaseCharacterParameter::FindNearestCombiRank),
     desempatando pelo menor CombiDuplicatePriority.

O mod em Lua repete exatamente esta logica; este arquivo e a referencia usada
pelos testes e pode ser usado direto na linha de comando:

    python tools/breeding.py Lamball Cattiva
"""

from __future__ import annotations

import json
import sys
from dataclasses import dataclass
from pathlib import Path

DATA = Path(__file__).resolve().parent.parent / "data"


@dataclass(frozen=True)
class Pal:
    id: str
    tribe: str
    zukan: int
    names: dict
    combi_rank: int
    combi_priority: int
    ignore_combi: bool
    element1: str
    size: str
    egg: str | None
    is_boss: bool
    male_probability: int

    def name(self, lang: str = "pt-BR") -> str:
        return self.names.get(lang) or self.id


def egg_pool_order(p: "Pal") -> tuple:
    """Ordem do Paldex; quem nao tem numero (alfa/variante) vai para o fim."""
    return (p.zukan if p.zukan and p.zukan > 0 else 9999, p.id)


class Breeding:
    def __init__(self, data_dir: Path = DATA):
        pals = json.loads((data_dir / "pals.json").read_text(encoding="utf-8"))
        self.pals: dict[str, Pal] = {}
        for p in pals:
            self.pals[p["id"]] = Pal(
                id=p["id"], tribe=p["tribe"], zukan=p["zukan"], names=p["names"],
                combi_rank=p["combi_rank"], combi_priority=p["combi_priority"],
                ignore_combi=p["ignore_combi"], element1=p["element1"],
                size=p["size"], egg=p["egg"], is_boss=p["is_boss"],
                male_probability=p["male_probability"],
            )
        self.eggs = json.loads((data_dir / "eggs.json").read_text(encoding="utf-8"))

        # Pool de resultados possiveis: uma linha por tribo (a DT tem varias
        # linhas para a mesma especie — alfa "BOSS_", variantes de quest etc.,
        # todas com o mesmo CombiRank; so a especie normal pode nascer).
        self.pool = self._build_pool()

        # combinacoes unicas indexadas pelo par de tribos (sem ordem)
        self.unique: dict[frozenset, list[dict]] = {}
        for u in json.loads((data_dir / "combi_unique.json").read_text(encoding="utf-8")):
            self.unique.setdefault(frozenset((u["parent_a"], u["parent_b"])), []).append(u)

        # todo Pal que nasce de um mesmo item de ovo
        self.by_egg: dict[str, list[Pal]] = {}
        for p in self.pool:
            if p.egg:
                self.by_egg.setdefault(p.egg, []).append(p)

    def _build_pool(self) -> list[Pal]:
        by_tribe: dict[str, list[Pal]] = {}
        for p in self.pals.values():
            if not p.ignore_combi:
                by_tribe.setdefault(p.tribe, []).append(p)

        def rank_row(p: Pal) -> tuple:
            # a linha "canonica" da especie: id igual ao nome da tribo;
            # senao a que aparece no Paldex; alfa e ultimo recurso.
            return (p.id.lower() != p.tribe.lower(), p.is_boss, -(p.zukan or -1))

        return [sorted(rows, key=rank_row)[0] for rows in by_tribe.values()]

    # ---------------------------------------------------------------- regras
    def unique_child(self, a: Pal, b: Pal, gender_a: str, gender_b: str) -> str | None:
        for u in self.unique.get(frozenset((a.tribe, b.tribe)), []):
            for (pa, ga), (pb, gb) in (((u["parent_a"], u["gender_a"]), (u["parent_b"], u["gender_b"])),
                                       ((u["parent_b"], u["gender_b"]), (u["parent_a"], u["gender_a"]))):
                if pa != a.tribe or pb != b.tribe:
                    continue
                if ga != "None" and ga != gender_a:
                    continue
                if gb != "None" and gb != gender_b:
                    continue
                return u["child"]
        return None

    def nearest(self, target_rank: int) -> Pal:
        return min(self.pool,
                   key=lambda p: (abs(p.combi_rank - target_rank), p.combi_priority))

    def breed(self, a: str, b: str, gender_a: str = "Male", gender_b: str = "Female") -> dict:
        """a/b sao ids de linha (ex. 'Lamball', 'BOSS_Anubis')."""
        pa, pb = self.pals[a], self.pals[b]
        child_id = self.unique_child(pa, pb, gender_a, gender_b)
        rule = "unique"
        if child_id is None:
            rule = "rank"
            target = (pa.combi_rank + pb.combi_rank + 1) // 2
            child_id = self.nearest(target).id
        child = self.pals[child_id]
        egg = child.egg
        return {
            "parents": (pa, pb),
            "rule": rule,
            "child": child,
            "egg": self.eggs.get(egg) if egg else None,
            # todos os Pals que saem de um ovo com esta mesma aparencia
            "egg_pool": sorted(self.by_egg.get(egg, []), key=egg_pool_order),
        }

    # ------------------------------------------------------------- utilidades
    def find(self, needle: str) -> Pal | None:
        needle_l = needle.lower()
        if needle in self.pals:
            return self.pals[needle]
        for p in self.pals.values():
            if p.id.lower() == needle_l or any(n.lower() == needle_l for n in p.names.values()):
                return p
        return None


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    b = Breeding()
    pa, pb = b.find(argv[0]), b.find(argv[1])
    if pa is None or pb is None:
        print("Pal nao encontrado:", argv[0] if pa is None else argv[1])
        return 1
    r = b.breed(pa.id, pb.id)
    print(f"{pa.name()} (rank {pa.combi_rank})  x  {pb.name()} (rank {pb.combi_rank})")
    print(f"  -> {r['child'].name()}  [{r['rule']}]")
    if r["egg"]:
        print(f"  ovo: {r['egg']['names'].get('pt-BR')}  ({r['egg']['id']})")
        print(f"  saem desse ovo ({len(r['egg_pool'])}): "
              + ", ".join(p.name() for p in r["egg_pool"][:12])
              + (" ..." if len(r["egg_pool"]) > 12 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
