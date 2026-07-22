"""
Gera mod-cpp/src/PalData.gen.cpp a partir de data/*.json.

Chamado no fim de extract_game_data.py — nao precisa rodar direto, mas da:

    python tools/gen_cpp_data.py
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "mod-cpp" / "src" / "PalData.gen.cpp"


def c_string(text: str | None) -> str:
    """Literal C com os bytes UTF-8 escapados (nada de depender da codificacao
    do arquivo fonte nem do code page do compilador)."""
    if text is None:
        return "nullptr"
    out = ['"']
    for byte in text.encode("utf-8"):
        char = chr(byte)
        if char in '"\\':
            out.append("\\" + char)
        elif 0x20 <= byte < 0x7F:
            out.append(char)
        else:
            out.append(f"\\{byte:03o}")
    out.append('"')
    return "".join(out)


def build_pool(pals: list[dict]) -> set[str]:
    """Uma linha por tribo — a mesma regra de tools/breeding.py."""
    by_tribe: dict[str, list[dict]] = {}
    for p in pals:
        if not p["ignore_combi"] and p["tribe"]:
            by_tribe.setdefault(p["tribe"], []).append(p)

    def rank(p: dict) -> tuple:
        return (p["id"].lower() != p["tribe"].lower(), p["is_boss"], -(p["zukan"] or -1))

    return {sorted(rows, key=rank)[0]["id"] for rows in by_tribe.values()}


def main() -> int:
    data = ROOT / "data"
    pals = json.loads((data / "pals.json").read_text(encoding="utf-8"))
    eggs = json.loads((data / "eggs.json").read_text(encoding="utf-8"))
    unique = json.loads((data / "combi_unique.json").read_text(encoding="utf-8"))
    pool = build_pool(pals)

    lines = [
        "// GERADO POR tools/gen_cpp_data.py -- NAO EDITE A MAO",
        "#include \"PalData.hpp\"",
        "",
        "namespace palbreed",
        "{",
        "    const PalInfo kPals[] = {",
    ]
    for p in pals:
        lines.append("        {%s, %s, %s, %s, %d, %d, %d, %s, %s, %s, %d, %s}," % (
            c_string(p["id"]), c_string(p["names"]["pt-BR"]), c_string(p["names"]["en"]),
            c_string(p["tribe"]), p["zukan"] or -1, p["combi_rank"], p["combi_priority"],
            c_string(p["element1"]), c_string(p["size"]), c_string(p["egg"]),
            p["male_probability"], "true" if p["id"] in pool else "false",
        ))
    lines += ["    };",
              f"    const std::size_t kPalCount = {len(pals)};", "",
              "    const UniqueCombo kUnique[] = {"]
    for u in unique:
        lines.append("        {%s, %s, %s, %s, %s}," % (
            c_string(u["parent_a"]), c_string(u["gender_a"]),
            c_string(u["parent_b"]), c_string(u["gender_b"]), c_string(u["child"])))
    lines += ["    };",
              f"    const std::size_t kUniqueCount = {len(unique)};", "",
              "    const EggInfo kEggs[] = {"]
    for egg in eggs.values():
        lines.append("        {%s, %s, %s, %s}," % (
            c_string(egg["id"]), c_string(egg["names"]["pt-BR"]),
            c_string(egg["names"]["en"]), c_string(egg["icon"])))
    lines += ["    };",
              f"    const std::size_t kEggCount = {len(eggs)};",
              "} // namespace palbreed", ""]

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines), encoding="ascii")
    print(f"  {OUT.relative_to(ROOT)}  ({OUT.stat().st_size / 1024:.0f} KB, "
          f"{len(pals)} Pals, {len(pool)} no pool)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
