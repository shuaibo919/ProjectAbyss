"""Tile the TypeGallery renders into one labelled summary sheet.

Run: uv run --no-project --with pillow python Game/Develop/Tools/build_type_gallery.py
"""

from PIL import Image, ImageDraw, ImageFont
from pathlib import Path

SHOTS = Path("Reference/Shots/TypeGallery")
OUT = Path("Reference/Shots/TypeGallery_Sheet.png")

COLS = 5
TILE_W, TILE_H = 560, 420
CAPTION_H = 52
HEADER_H = 64
MARGIN = 14
GAP = 10

# (group title, [(tile file prefix, Chinese label), ...])
GROUPS = [
    ("屋顶九式（同一殿身，仅屋顶不同）", [
        ("00_roof_yingshan", "硬山"),
        ("01_roof_xuanshan", "悬山"),
        ("02_roof_wudian", "庑殿"),
        ("03_roof_xieshan", "歇山"),
        ("04_roof_juanpeng", "卷棚"),
        ("05_roof_luding", "盝顶"),
        ("06_roof_cuanjian", "攒尖"),
        ("07_roof_yuancuanjian", "圆攒尖"),
        ("08_roof_kuiding", "盔顶"),
    ]),
    ("多层楼阁（原生多层 / 重檐）", [
        ("09_storey_chongyan_xieshan", "重檐歇山"),
        ("10_storey_sanceng_ge", "三层攒尖阁"),
    ]),
    ("亭台楼阁榭廊（apply_archetype 预设）", [
        ("11_arch_0", "亭"),
        ("12_arch_1", "台"),
        ("13_arch_2", "楼"),
        ("14_arch_3", "阁"),
        ("15_arch_4", "榭"),
        ("16_arch_5", "廊"),
    ]),
    ("砖石作（城台 / AncientMasonry）", [
        ("17_masonry_gate_tower", "城门楼"),
        ("18_masonry_wall", "城墙·垛口"),
        ("19_masonry_bridge", "拱桥"),
    ]),
    ("连体（AncientBuildingCompound 预设）", [
        ("20_compound_0", "抱厦"),
        ("21_compound_1", "勾连搭"),
        ("22_compound_2", "十字脊"),
        ("23_compound_3", "工字殿"),
        ("24_compound_4", "曲尺"),
    ]),
]

TITLE = "AncientBuilding 支持的类型汇总（25 种，2026-09-29）"


def load_font(size: int):
    for name in ("msyh.ttc", "simhei.ttf", "simsun.ttc"):
        try:
            return ImageFont.truetype(f"C:/Windows/Fonts/{name}", size)
        except OSError:
            continue
    return ImageFont.load_default()


def main() -> None:
    paper = (229, 219, 205)
    ink = (58, 52, 48)
    accent = (140, 48, 40)

    title_font = load_font(44)
    header_font = load_font(34)
    caption_font = load_font(30)

    # Layout: title, then per group a header row + tile rows.
    rows = []
    for title, tiles in GROUPS:
        rows.append(("header", title))
        for i in range(0, len(tiles), COLS):
            rows.append(("tiles", tiles[i:i + COLS]))

    width = MARGIN * 2 + COLS * TILE_W + (COLS - 1) * GAP
    y = MARGIN + 78
    for kind, _ in rows:
        y += HEADER_H if kind == "header" else TILE_H + CAPTION_H + GAP
    height = y + MARGIN - GAP

    sheet = Image.new("RGB", (width, height), paper)
    draw = ImageDraw.Draw(sheet)

    draw.text((MARGIN, MARGIN), TITLE, font=title_font, fill=ink)

    y = MARGIN + 78
    for kind, payload in rows:
        if kind == "header":
            draw.rectangle([MARGIN, y + 8, MARGIN + 6, y + HEADER_H - 14], fill=accent)
            draw.text((MARGIN + 18, y + 6), payload, font=header_font, fill=ink)
            y += HEADER_H
            continue
        x = MARGIN
        for prefix, label in payload:
            img = Image.open(SHOTS / f"{prefix}.png").convert("RGB").resize((TILE_W, TILE_H), Image.LANCZOS)
            sheet.paste(img, (x, y))
            draw.rectangle([x, y, x + TILE_W, y + TILE_H], outline=(150, 138, 122), width=1)
            box = draw.textbbox((0, 0), label, font=caption_font)
            draw.text((x + (TILE_W - (box[2] - box[0])) / 2, y + TILE_H + 8), label, font=caption_font, fill=ink)
            x += TILE_W + GAP
        y += TILE_H + CAPTION_H + GAP

    sheet.save(OUT)
    print(f"{OUT}  {width}x{height}")


if __name__ == "__main__":
    main()
