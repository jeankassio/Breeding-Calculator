"""
Monta a thumbnail do mod (525x525, o mesmo formato que a Workshop usa) a
partir dos icones ja extraidos: dois pais, o ovo e o filhote.

    python tools/make_thumbnail.py
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
ICONS = ROOT / "mod" / "PalBreedCalc" / "icons"
OUT = ROOT / "mod" / "workshop" / "PalBreedCalc" / "thumbnail.png"

SIZE = 525
BACKGROUND = (16, 17, 22)
CARD = (26, 28, 36)
MALE = (107, 168, 255)
FEMALE = (255, 133, 184)
EGG = (255, 212, 107)
CHILD = (128, 240, 148)
TEXT = (232, 234, 240)


def icon(name: str, box: int) -> Image.Image:
    return Image.open(ICONS / f"{name}.dds").convert("RGBA").resize((box, box), Image.LANCZOS)


def font(size: int) -> ImageFont.FreeTypeFont:
    for name in ("segoeuib.ttf", "seguisb.ttf", "arialbd.ttf"):
        try:
            return ImageFont.truetype(f"C:\\Windows\\Fonts\\{name}", size)
        except OSError:
            continue
    return ImageFont.load_default()


def rounded(image: Image.Image, box: tuple[int, int, int, int], color, radius: int = 18) -> None:
    ImageDraw.Draw(image).rounded_rectangle(box, radius=radius, fill=color)


def centered(draw: ImageDraw.ImageDraw, text: str, y: int, f, color) -> None:
    width = draw.textbbox((0, 0), text, font=f)[2]
    draw.text(((SIZE - width) // 2, y), text, font=f, fill=color)


def main() -> int:
    if not ICONS.exists():
        print("icones ausentes -- rode antes: python tools/extract_icons.py")
        return 1

    sheet = Image.new("RGBA", (SIZE, SIZE), BACKGROUND)
    draw = ImageDraw.Draw(sheet)

    centered(draw, "BREEDING", 34, font(52), TEXT)
    centered(draw, "CALCULATOR", 92, font(52), TEXT)
    centered(draw, "Palworld  ·  F6", 156, font(26), (140, 144, 156))

    # pais
    parent_box = 132
    top = 212
    rounded(sheet, (52, top - 10, 52 + parent_box + 20, top + parent_box + 10), CARD)
    rounded(sheet, (SIZE - 72 - parent_box, top - 10, SIZE - 52 + 20 - 20, top + parent_box + 10), CARD)
    sheet.alpha_composite(icon("pal_SheepBall", parent_box), (62, top))
    sheet.alpha_composite(icon("pal_PinkCat", parent_box), (SIZE - 62 - parent_box, top))
    draw.text((SIZE // 2 - 12, top + parent_box // 2 - 22), "×", font=font(44), fill=(120, 124, 136))
    draw.rectangle((62, top + parent_box + 16, 62 + parent_box, top + parent_box + 20), fill=MALE)
    draw.rectangle((SIZE - 62 - parent_box, top + parent_box + 16, SIZE - 62,
                    top + parent_box + 20), fill=FEMALE)

    # ovo -> filhote
    result_box = 118
    bottom = 392
    rounded(sheet, (96, bottom - 12, 96 + result_box + 16, bottom + result_box + 12), CARD)
    rounded(sheet, (SIZE - 112 - result_box, bottom - 12, SIZE - 96 + 16 - 16,
                    bottom + result_box + 12), CARD)
    sheet.alpha_composite(icon("egg_PalEgg_Leaf_01", result_box), (104, bottom))
    sheet.alpha_composite(icon("pal_Monkey", result_box), (SIZE - 104 - result_box, bottom))

    arrow_y = bottom + result_box // 2
    draw.line((SIZE // 2 - 26, arrow_y, SIZE // 2 + 14, arrow_y), fill=CHILD, width=5)
    draw.polygon([(SIZE // 2 + 10, arrow_y - 12), (SIZE // 2 + 10, arrow_y + 12),
                  (SIZE // 2 + 30, arrow_y)], fill=CHILD)
    draw.rounded_rectangle((96, bottom - 12, 96 + result_box + 16, bottom + result_box + 12),
                           radius=18, outline=EGG, width=3)
    draw.rounded_rectangle((SIZE - 112 - result_box, bottom - 12, SIZE - 96,
                            bottom + result_box + 12), radius=18, outline=CHILD, width=3)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    sheet.convert("RGB").save(OUT)
    print(f"  {OUT.relative_to(ROOT)}  ({SIZE}x{SIZE})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
