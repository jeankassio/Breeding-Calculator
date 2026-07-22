"""
Extrai os icones dos Pals e dos ovos do pak e grava como .dds ao lado do mod.

Por que .dds: as texturas do jogo ja sao BC (DXT5/BC7) — o mesmo formato que o
D3D11 consome direto. Basta reempacotar o mip 0 com um cabecalho DDS e o mod
cria a textura sem nenhum decodificador.

    python tools/extract_icons.py

Saida: mod/PalBreedCalc/icons/pal_<id>.dds e egg_<item>.dds
"""

from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
ICONS_OUT = ROOT / "mod" / "PalBreedCalc" / "icons"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from extract_game_data import CONFIG, ensure_mappings, load_json  # noqa: E402

ICON_TABLES = {
    "pal": "Pal/Content/Pal/DataTable/Character/DT_PalCharacterIconDataTable",
    "egg": "Pal/Content/Pal/DataTable/Item/DT_ItemIconDataTable",
}

# PF_* -> (DXGI format, bloco em bytes, pixels por bloco no eixo)
FORMATS = {
    "PF_DXT1": ("DXT1", 8, 4),
    "PF_DXT3": ("DXT3", 16, 4),
    "PF_DXT5": ("DXT5", 16, 4),
    "PF_BC4": ("BC4U", 8, 4),
    "PF_BC5": ("BC5U", 16, 4),
    "PF_BC7": ("DX10", 16, 4),
    "PF_B8G8R8A8": ("RGBA", 4, 1),
}
DXGI_BC7_UNORM = 98


def unpack_assets(paths: list[str], batch: int = 60) -> None:
    """repak so le o indice do pak, entao varias chamadas saem baratas.
    Vai em lotes porque a linha de comando do Windows estoura em ~32 KB."""
    out = BUILD / "extract"
    out.mkdir(parents=True, exist_ok=True)
    for start in range(0, len(paths), batch):
        includes: list[str] = []
        for p in paths[start:start + batch]:
            for ext in (".uasset", ".uexp", ".ubulk"):
                includes += ["-i", p + ext]
        subprocess.run([CONFIG["repak"], "unpack", "-o", str(out), "-f", "-q",
                        *includes, CONFIG["pak"]], check=True)


def tojson(asset_path: str, name: str) -> Path:
    mapping = ensure_mappings()
    src = BUILD / "extract" / Path(asset_path.replace("/", os.sep) + ".uasset")
    dst = BUILD / "json" / f"{name}.json"
    dst.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([CONFIG["uassetgui"], "tojson", str(src), str(dst),
                    CONFIG["engine_version"], mapping], check=True)
    return dst


def icon_map(json_path: Path) -> dict[str, str]:
    """{chave da linha em minusculas: caminho do pacote da textura}

    Minusculas porque a tabela de icones diverge da de parametros na caixa
    ("BadCatGirl" x "BadCatgirl", "Cowpal" x "CowPal", ...).
    """
    rows = load_json(json_path)["Exports"][0]["Table"]["Data"]
    out = {}
    for row in rows:
        for prop in row["Value"]:
            value = prop.get("Value")
            if isinstance(value, dict) and "AssetPath" in value:
                package = value["AssetPath"].get("PackageName")
                if package:
                    out[row["Name"].lower()] = package
    return out


def icon_candidates(pal: dict, table: dict[str, str]) -> list[str]:
    """Pacotes a tentar, do mais especifico ao mais generico: a propria linha,
    a tribo e, para variantes (Kitsun Noct = AmaterasuWolf_Dark), a especie
    base — algumas variantes nao tem icone proprio no pak."""
    names = [pal["id"], pal["tribe"] or ""]
    for name in list(names):
        if "_" in name:
            names.append(name.rsplit("_", 1)[0])
    packages = []
    for name in names:
        package = table.get(name.lower())
        if package and package not in packages:
            packages.append(package)
    return packages


def game_path(package: str) -> str:
    """/Game/... -> caminho dentro do pak."""
    return "Pal/Content/" + package.removeprefix("/Game/")


# FByteBulkData: payload gravado junto do .uexp em vez de ir para o .ubulk
BULKDATA_ForceInlinePayload = 0x40


def parse_texture(uexp: Path) -> dict | None:
    """Le o FTexturePlatformData cozido e devolve os dados do mip 0.

    Layout (UE5.1, depois das propriedades):
        SizeX | SizeY | PackedData | FString "PF_..." |
        FirstMipToSerialize | NumMips |
        por mip: BulkDataFlags | ElementCount | SizeOnDisk | OffsetInFile |
                 [payload, se inline] | SizeX | SizeY | SizeZ
    """
    data = uexp.read_bytes()
    index = data.find(b"PF_")
    if index < 16:
        return None
    end = data.find(b"\x00", index)
    pixel_format = data[index:end].decode("ascii", "ignore")
    # ... SizeX | SizeY | PackedData | tamanho da FString | "PF_..."
    size_x, size_y = struct.unpack_from("<ii", data, index - 16)

    position = end + 1
    _first_mip, mip_count = struct.unpack_from("<ii", data, position)
    position += 8
    if mip_count < 1:
        return None

    flags, _element_count, size_on_disk, offset = struct.unpack_from("<iiiq", data, position)
    position += 20
    inline = bool(flags & BULKDATA_ForceInlinePayload)
    payload = data[position:position + size_on_disk] if inline else b""

    return {"width": size_x, "height": size_y, "format": pixel_format,
            "inline": inline, "offset": offset, "size": size_on_disk,
            "payload": payload}


def mip0_size(width: int, height: int, pixel_format: str) -> int:
    _, block_bytes, block_pixels = FORMATS[pixel_format]
    if block_pixels == 1:
        return width * height * block_bytes
    blocks_x = max(1, (width + block_pixels - 1) // block_pixels)
    blocks_y = max(1, (height + block_pixels - 1) // block_pixels)
    return blocks_x * blocks_y * block_bytes


def dds_header(width: int, height: int, pixel_format: str) -> bytes:
    fourcc, block_bytes, block_pixels = FORMATS[pixel_format]
    DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PIXELFORMAT = 0x1, 0x2, 0x4, 0x1000
    DDSD_LINEARSIZE = 0x80000
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_LINEARSIZE

    if fourcc == "RGBA":
        pf = struct.pack("<II4sIIIII", 32, 0x41, b"\0\0\0\0", 32,
                         0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
        pitch = width * 4
    else:
        pf = struct.pack("<II4sIIIII", 32, 0x4, fourcc.encode("ascii"), 0, 0, 0, 0, 0)
        pitch = mip0_size(width, height, pixel_format)

    header = struct.pack("<4sIIIIIII", b"DDS ", 124, flags, height, width, pitch, 0, 1)
    header += b"\0" * 44                      # dwReserved1
    header += pf
    header += struct.pack("<IIIII", 0x1000, 0, 0, 0, 0)   # caps

    if fourcc == "DX10":
        header += struct.pack("<IIIII", DXGI_BC7_UNORM, 3, 0, 1, 0)
    return header


def write_dds(dest: Path, width: int, height: int, pixel_format: str, payload: bytes) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(dds_header(width, height, pixel_format) + payload)


def convert(package: str, dest: Path) -> str | None:
    """Devolve None em caso de sucesso, ou o motivo da falha."""
    base = BUILD / "extract" / Path(game_path(package).replace("/", os.sep))
    uexp = base.with_suffix(".uexp")
    ubulk = base.with_suffix(".ubulk")
    if not uexp.exists():
        return "sem .uexp"

    mip = parse_texture(uexp)
    if mip is None:
        return "formato nao reconhecido"
    if mip["format"] not in FORMATS:
        return f"formato {mip['format']} nao suportado"

    expected = mip0_size(mip["width"], mip["height"], mip["format"])
    if mip["inline"]:
        payload = mip["payload"]
    elif ubulk.exists():
        payload = ubulk.read_bytes()[mip["offset"]:mip["offset"] + mip["size"]]
    else:
        return "mip 0 esta no .ubulk, que nao foi extraido"

    if len(payload) != expected:
        return f"mip 0 incompleto ({len(payload)}/{expected})"

    write_dds(dest, mip["width"], mip["height"], mip["format"], payload)
    return None


def main() -> int:
    pals = json.loads((ROOT / "data" / "pals.json").read_text(encoding="utf-8"))
    eggs = json.loads((ROOT / "data" / "eggs.json").read_text(encoding="utf-8"))

    print("[1/4] tabelas de icones")
    unpack_assets(list(ICON_TABLES.values()))
    maps = {kind: icon_map(tojson(path, Path(path).name))
            for kind, path in ICON_TABLES.items()}
    print(f"  {len(maps['pal'])} icones de Pal, {len(maps['egg'])} de item")

    # quem precisa de icone: todo Pal que pode aparecer na janela + os ovos
    wanted: dict[str, list[str]] = {}
    for pal in pals:
        packages = icon_candidates(pal, maps["pal"])
        if packages:
            wanted[f"pal_{pal['id']}"] = packages
    # varios ovos compartilham o mesmo icone (Fire_01..05 usam PalEgg_Fire_01),
    # entao o arquivo e nomeado pelo IconName, nao pelo id do item
    for egg in eggs.values():
        package = maps["egg"].get((egg["icon"] or "").lower())
        if package:
            wanted[f"egg_{egg['icon']}"] = [package]

    every_package = {game_path(p) for packages in wanted.values() for p in packages}
    print(f"[2/4] desempacotando {len(every_package)} texturas")
    unpack_assets(sorted(every_package))

    print("[3/4] convertendo para .dds")
    ICONS_OUT.mkdir(parents=True, exist_ok=True)
    failures: dict[str, str] = {}
    written = 0
    for key, packages in sorted(wanted.items()):
        for package in packages:                 # cai para o icone da especie base
            error = convert(package, ICONS_OUT / f"{key}.dds")
            if error is None:
                written += 1
                break
        else:
            failures[key] = error or "?"

    print(f"[4/4] {written} icones em {ICONS_OUT.relative_to(ROOT)}")
    if failures:
        print(f"  {len(failures)} falharam:")
        for key, reason in list(failures.items())[:10]:
            print(f"    {key}: {reason}")

    without_entry = [p["id"] for p in pals if f"pal_{p['id']}" not in wanted]
    if without_entry:
        print(f"  {len(without_entry)} Pals sem linha na tabela de icones "
              f"(ex.: {', '.join(without_entry[:5])})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
