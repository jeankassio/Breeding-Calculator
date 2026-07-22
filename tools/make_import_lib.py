"""
Gera UE4SS.lib (import library) a partir da UE4SS.dll instalada.

Mods C++ do UE4SS normalmente linkam contra a lib produzida ao compilar o
UE4SS inteiro do fonte — o que aqui nao da, porque o submodulo `UEPseudo`
(headers da Unreal) nao esta mais publico. Como a DLL exporta os simbolos
decorados que precisamos (CppUserModBase, UE4SSProgram, Output...), da para
montar a import lib direto da tabela de exports.

    python tools/make_import_lib.py [caminho\\da\\UE4SS.dll]

Saida: external/lib/UE4SS.lib
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pefile

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "external" / "lib"
DEFAULT_DLL = Path(os.environ.get(
    "UE4SS_DLL",
    r"G:\SteamLibrary\steamapps\common\Palworld\Mods\NativeMods\UE4SS\UE4SS.dll"))


def find_lib_exe() -> Path:
    """lib.exe do MSVC (BuildTools ou instalacao completa)."""
    found = shutil.which("lib")
    if found:
        return Path(found)
    roots = [Path(r"C:\Program Files (x86)\Microsoft Visual Studio"),
             Path(r"C:\Program Files\Microsoft Visual Studio")]
    candidates = []
    for root in roots:
        if root.exists():
            candidates += list(root.glob("*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/lib.exe"))
    if not candidates:
        raise SystemExit("lib.exe do MSVC nao encontrado (instale as Build Tools do VS 2022)")
    return sorted(candidates)[-1]


def exported_symbols(dll: Path) -> list[tuple[str, bool]]:
    """[(nome decorado, e_dado)] — simbolos de dados precisam do sufixo DATA
    no .def, senao o linker gera acesso indireto errado."""
    pe = pefile.PE(str(dll), fast_load=True)
    pe.parse_data_directories(
        directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])

    code_sections = []
    for section in pe.sections:
        # IMAGE_SCN_CNT_CODE / MEM_EXECUTE
        executable = bool(section.Characteristics & 0x20000000)
        code_sections.append((section.VirtualAddress,
                              section.VirtualAddress + section.Misc_VirtualSize,
                              executable))

    def is_code(rva: int) -> bool:
        for start, end, executable in code_sections:
            if start <= rva < end:
                return executable
        return False

    out = []
    for sym in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if not sym.name:
            continue                      # exports por ordinal: nao interessam
        out.append((sym.name.decode("ascii", "ignore"), not is_code(sym.address)))
    return out


def main() -> int:
    dll = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_DLL
    if not dll.exists():
        raise SystemExit(f"UE4SS.dll nao encontrada: {dll}")

    symbols = exported_symbols(dll)
    data_count = sum(1 for _, is_data in symbols if is_data)
    print(f"{dll.name}: {len(symbols)} exports ({data_count} de dados)")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    def_path = OUT_DIR / "UE4SS.def"
    lines = ["LIBRARY UE4SS", "EXPORTS"]
    for name, is_data in symbols:
        lines.append(f"    {name}" + ("    DATA" if is_data else ""))
    def_path.write_text("\n".join(lines) + "\n", encoding="ascii")

    lib_exe = find_lib_exe()
    lib_path = OUT_DIR / "UE4SS.lib"
    subprocess.run([str(lib_exe), f"/def:{def_path}", "/machine:x64",
                    f"/out:{lib_path}", "/nologo"], check=True)
    print(f"gerado {lib_path.relative_to(ROOT)} ({lib_path.stat().st_size / 1024:.0f} KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
