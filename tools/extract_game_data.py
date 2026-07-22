"""
Extrai do jogo (Pal-Windows.pak) tudo o que a calculadora de reproducao precisa.

Pipeline:
  1. repak     -> desempacota os .uasset/.uexp das DataTables necessarias
  2. UAssetGUI -> converte cada .uasset em JSON (usando o Palworld.usmap)
  3. este script -> normaliza os JSONs em data/*.json  +  data/breeding_data.lua

Rode:  python tools/extract_game_data.py
Opcional: --skip-unpack / --skip-tojson  para reaproveitar o cache em build/.

Tudo o que e especifico da maquina esta em CONFIG (ou variaveis de ambiente).
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
DATA = ROOT / "data"

CONFIG = {
    "pak": os.environ.get(
        "PALWORLD_PAK",
        r"G:\SteamLibrary\steamapps\common\Palworld\Pal\Content\Paks\Pal-Windows.pak",
    ),
    "repak": os.environ.get("REPAK_EXE", r"D:\mods_palworld\_tools\repak\repak.exe"),
    "uassetgui": os.environ.get("UASSETGUI_EXE", r"D:\mods_palworld\_tools\UAssetGUI.exe"),
    "usmap": os.environ.get("PALWORLD_USMAP", r"D:\mods_palworld\_tools\Palworld.usmap"),
    "engine_version": "VER_UE5_1",
}

# Idiomas exportados para os nomes dos Pals/itens (pt-BR primeiro = idioma padrao do mod)
LANGS = ["pt-BR", "en"]

# Assets necessarios dentro do pak (sem extensao; .uasset e .uexp sao pegos juntos)
ASSETS = [
    "Pal/Content/Pal/DataTable/Character/DT_PalMonsterParameter",
    "Pal/Content/Pal/DataTable/Character/DT_PalCombiUnique",
    "Pal/Content/Pal/DataTable/Item/DT_ItemDataTable",
    # tabelas de texto no idioma de origem: cobrem as chaves que as pastas
    # L10N nao trazem (so existe DT_*NameText_Common traduzido)
    "Pal/Content/Pal/DataTable/Text/DT_PalNameText",
    "Pal/Content/Pal/DataTable/Text/DT_PalNameText_Common",
    "Pal/Content/Pal/DataTable/Text/DT_ItemNameText",
    "Pal/Content/Pal/DataTable/Text/DT_ItemNameText_Common",
] + [f"Pal/Content/L10N/{l}/Pal/DataTable/Text/DT_PalNameText_Common" for l in LANGS] + [
    f"Pal/Content/L10N/{l}/Pal/DataTable/Text/DT_ItemNameText_Common" for l in LANGS
]

# EPalElementType -> sufixo do item de ovo (PalEgg_<elemento>_<tamanho>)
EGG_ELEMENT = {
    "Normal": "Normal",
    "Fire": "Fire",
    "Water": "Water",
    "Leaf": "Leaf",
    "Electricity": "Electricity",
    "Ice": "Ice",
    "Earth": "Earth",
    "Dark": "Dark",
    "Dragon": "Dragon",
}

# EPalSizeType -> sufixo numerico do item de ovo
EGG_SIZE = {"XS": "01", "S": "02", "M": "03", "L": "04", "XL": "05"}


# --------------------------------------------------------------------------
# etapa 1/2: desempacotar + converter
# --------------------------------------------------------------------------
def run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    print("  $", " ".join(str(c) for c in cmd))
    return subprocess.run(cmd, check=True, **kw)


def unpack() -> None:
    """repak: extrai apenas os assets da lista (o pak inteiro tem 40 GB)."""
    out = BUILD / "extract"
    out.mkdir(parents=True, exist_ok=True)
    includes: list[str] = []
    for a in ASSETS:
        includes += ["-i", f"{a}.uasset", "-i", f"{a}.uexp"]
    run([CONFIG["repak"], "unpack", "-o", str(out), "-f", *includes, CONFIG["pak"]])


def ensure_mappings() -> str:
    """UAssetGUI so aceita o .usmap por *nome*, a partir da pasta Mappings dele."""
    name = Path(CONFIG["usmap"]).stem
    dest = Path(os.environ["LOCALAPPDATA"]) / "UAssetGUI" / "Mappings"
    dest.mkdir(parents=True, exist_ok=True)
    target = dest / f"{name}.usmap"
    if not target.exists() or target.stat().st_mtime < Path(CONFIG["usmap"]).stat().st_mtime:
        shutil.copyfile(CONFIG["usmap"], target)
        print(f"  mappings instalados em {target}")
    return name


def tojson() -> None:
    """UAssetGUI: .uasset -> .json. Sem o usmap as DataTables saem como RawExport."""
    mapping = ensure_mappings()
    outdir = BUILD / "json"
    outdir.mkdir(parents=True, exist_ok=True)
    for a in ASSETS:
        src = BUILD / "extract" / Path(a.replace("/", os.sep) + ".uasset")
        dst = outdir / (json_key(a) + ".json")
        run([CONFIG["uassetgui"], "tojson", str(src), str(dst),
             CONFIG["engine_version"], mapping])
        check_parsed(dst)


def json_key(asset_path: str) -> str:
    """Nome curto e unico do JSON (o mesmo DT existe em varios idiomas)."""
    name = asset_path.rsplit("/", 1)[-1]
    if "/L10N/" in asset_path:
        lang = asset_path.split("/L10N/")[1].split("/")[0]
        return f"{name}__{lang}"
    return name


def check_parsed(path: Path) -> None:
    d = load_json(path)
    kind = d["Exports"][0]["$type"]
    if "DataTableExport" not in kind:
        raise SystemExit(
            f"{path.name}: UAssetGUI devolveu {kind.split(',')[0]} em vez de DataTableExport.\n"
            "  -> o .usmap nao foi aplicado (confira Palworld.usmap / a pasta Mappings)."
        )


# --------------------------------------------------------------------------
# etapa 3: normalizacao
# --------------------------------------------------------------------------
def load_json(path: Path):
    with open(path, encoding="utf-8-sig") as fh:
        return json.load(fh)


def table_rows(name: str) -> list[dict]:
    d = load_json(BUILD / "json" / f"{name}.json")
    return d["Exports"][0]["Table"]["Data"]


def fields(row: dict) -> dict:
    """Achata as propriedades da linha em {nome: valor}."""
    return {p["Name"]: p.get("Value") for p in row["Value"]}


def text_table(name: str) -> dict[str, str]:
    """DT_*NameText* -> {chave em minusculas: texto}.

    As chaves variam de caixa entre as tabelas (PAL_NAME_Windchimes x a tribo
    WindChimes), entao o indice e sempre case-insensitive. "-" e placeholder
    de traducao pendente e conta como ausente.
    """
    out = {}
    for row in table_rows(name):
        props = row["Value"]
        if props:
            text = (props[0].get("CultureInvariantString") or "").strip()
            if text and text != "-":
                out[row["Name"].lower()] = text
    return out


def localized(base: str, lang: str) -> dict[str, str]:
    """Textos de um idioma: as tabelas de origem servem de fallback para as
    chaves que a pasta L10N daquele idioma nao traduz."""
    out = text_table(base)
    out.update(text_table(f"{base}_Common"))
    out.update(text_table(f"{base}_Common__{lang}"))
    return out


def pick(tables: dict[str, dict[str, str]], lang: str, keys: list[str], fallback: str) -> str:
    """Primeiro texto encontrado: idioma pedido antes dos demais, chaves na
    ordem dada (id da linha, tribo, id sem prefixo de alfa/raide...)."""
    for candidate in (lang, *LANGS):
        for key in keys:
            text = tables[candidate].get(key.lower())
            if text:
                return text
    return fallback


ROW_PREFIXES = ("BOSS_", "RAID_", "SUMMON_", "GYM_", "Quest_")


def name_keys(prefix: str, char_id: str, tribe: str | None, override: str | None) -> list[str]:
    base = [override or char_id, char_id]
    if tribe:
        base.append(tribe)
    for p in ROW_PREFIXES:
        if char_id.startswith(p):
            base.append(char_id[len(p):])
    return [f"{prefix}{k}" for k in dict.fromkeys(k for k in base if k)]


def build_pals() -> tuple[list[dict], dict]:
    names = {lang: localized("DT_PalNameText", lang) for lang in LANGS}

    pals: list[dict] = []
    for row in table_rows("DT_PalMonsterParameter"):
        f = fields(row)
        if not f.get("IsPal"):
            continue
        char_id = row["Name"]
        keys = name_keys("PAL_NAME_", char_id, f.get("Tribe"), f.get("OverrideNameTextID"))
        pals.append({
            "id": char_id,                                  # linha da DT_PalMonsterParameter
            "tribe": f.get("Tribe"),                         # EPalTribeID (o que a reproducao usa)
            "zukan": f.get("ZukanIndex"),
            "zukan_suffix": f.get("ZukanIndexSuffix") or "",
            "names": {lang: pick(names, lang, keys, char_id) for lang in LANGS},
            "combi_rank": f.get("CombiRank"),
            "combi_priority": f.get("CombiDuplicatePriority"),
            "ignore_combi": bool(f.get("IgnoreCombi")),
            "male_probability": f.get("MaleProbability"),
            "element1": f.get("ElementType1"),
            "element2": f.get("ElementType2"),
            "size": f.get("Size"),
            "rarity": f.get("Rarity"),
            "is_boss": bool(f.get("IsBoss")),
            "is_tower_boss": bool(f.get("IsTowerBoss")),
            "is_raid_boss": bool(f.get("IsRaidBoss")),
            "egg": egg_item(f),
        })

    item_names = {lang: localized("DT_ItemNameText", lang) for lang in LANGS}
    eggs = {}
    for row in table_rows("DT_ItemDataTable"):
        item_id = row["Name"]
        if not item_id.startswith("PalEgg_"):
            continue
        eggs[item_id] = {
            "id": item_id,
            "icon": fields(row).get("IconName"),
            "names": {lang: pick(item_names, lang, [f"ITEM_NAME_{item_id}"], item_id)
                      for lang in LANGS},
        }
    return pals, eggs


def egg_item(f: dict) -> str | None:
    """Item de ovo gerado por um Pal = PalEgg_<elemento primario>_<tamanho>."""
    element = EGG_ELEMENT.get(f.get("ElementType1") or "Normal")
    size = EGG_SIZE.get(f.get("Size"))
    if not element or not size:
        return None
    return f"PalEgg_{element}_{size}"


def build_unique() -> list[dict]:
    out = []
    for row in table_rows("DT_PalCombiUnique"):
        f = fields(row)
        out.append({
            "parent_a": f.get("ParentTribeA"),
            "gender_a": f.get("ParentGenderA") or "None",
            "parent_b": f.get("ParentTribeB"),
            "gender_b": f.get("ParentGenderB") or "None",
            "child": f.get("ChildCharacterID"),
        })
    return out


# --------------------------------------------------------------------------
# saida
# --------------------------------------------------------------------------
def write_json(path: Path, payload) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(payload, fh, ensure_ascii=False, indent=1)
    print(f"  {path.relative_to(ROOT)}  ({path.stat().st_size / 1024:.0f} KB)")


def lua_value(v) -> str:
    if v is None:
        return "nil"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v)
    return '"' + str(v).replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_lua(path: Path, pals: list[dict], eggs: dict, unique: list[dict]) -> None:
    """Base embutida no mod: serve de fallback e traz os nomes traduzidos
    (o mod le CombiRank e companhia direto do jogo, mas os nomes vem daqui)."""
    lang = LANGS[0]
    out = ["-- GERADO POR tools/extract_game_data.py -- NAO EDITE A MAO",
           f"-- idioma: {lang}",
           "return {", "  pals = {"]
    for p in pals:
        fields = {
            "id": p["id"], "tribe": p["tribe"], "zukan": p["zukan"],
            "name": p["names"][lang], "name_en": p["names"]["en"],
            "combi_rank": p["combi_rank"], "combi_priority": p["combi_priority"],
            "ignore_combi": p["ignore_combi"], "male_probability": p["male_probability"],
            "element1": p["element1"], "element2": p["element2"], "size": p["size"],
            "rarity": p["rarity"], "is_boss": p["is_boss"], "egg": p["egg"],
        }
        body = ", ".join(f"{k} = {lua_value(v)}" for k, v in fields.items())
        out.append(f'    [{lua_value(p["id"])}] = {{ {body} }},')
    out += ["  },", "  eggs = {"]
    for egg in eggs.values():
        body = ", ".join(f"{k} = {lua_value(v)}" for k, v in
                         (("id", egg["id"]), ("icon", egg["icon"]),
                          ("name", egg["names"][lang]), ("name_en", egg["names"]["en"])))
        out.append(f'    [{lua_value(egg["id"])}] = {{ {body} }},')
    out += ["  },", "  unique = {"]
    for u in unique:
        body = ", ".join(f"{k} = {lua_value(u[k])}" for k in
                         ("parent_a", "gender_a", "parent_b", "gender_b", "child"))
        out.append(f"    {{ {body} }},")
    out += ["  },", "}", ""]

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(out), encoding="utf-8")
    print(f"  {path.relative_to(ROOT)}  ({path.stat().st_size / 1024:.0f} KB)")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-unpack", action="store_true")
    ap.add_argument("--skip-tojson", action="store_true")
    args = ap.parse_args()

    if not args.skip_unpack:
        print("[1/3] repak unpack")
        unpack()
    if not args.skip_tojson:
        print("[2/3] UAssetGUI tojson")
        tojson()

    print("[3/3] normalizando")
    pals, eggs = build_pals()
    unique = build_unique()

    breedable = [p for p in pals if not p["ignore_combi"]]
    write_json(DATA / "pals.json", pals)
    write_json(DATA / "eggs.json", eggs)
    write_json(DATA / "combi_unique.json", unique)
    write_lua(ROOT / "mod" / "PalBreedCalc" / "Scripts" / "data.lua", pals, eggs, unique)

    print(f"\n{len(pals)} Pals ({len(breedable)} reproduziveis), "
          f"{len(unique)} combinacoes unicas, {len(eggs)} tipos de ovo.")


if __name__ == "__main__":
    sys.exit(main())
