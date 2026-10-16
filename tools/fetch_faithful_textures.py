"""Fetches the biome block textures from the Faithful 64x pack and bakes in biome tints.

Vanilla foliage and grass textures are grey and tinted per biome at runtime; the
engine only tints a fixed set of names, so tinted variants are baked here instead.

Usage: python tools/fetch_faithful_textures.py
"""
import io
import pathlib
import urllib.request

from PIL import Image

COMMIT = "1cd1dfbc5405742e09c2ea4eafd0839086d00f42"
BASE = f"https://raw.githubusercontent.com/Faithful-Resource-Pack/Faithful-64x-Java/{COMMIT}/assets/minecraft/textures/block/"
OUT = pathlib.Path(__file__).resolve().parent.parent / "assets" / "sprites" / "blocks"

COPY = [
    "snow", "grass_block_snow", "ice", "packed_ice", "red_sand", "clay", "coarse_dirt",
    "terracotta", "orange_terracotta", "yellow_terracotta", "white_terracotta",
    "red_terracotta", "brown_terracotta", "light_gray_terracotta",
    "sandstone", "sandstone_top", "sandstone_bottom", "podzol_top", "podzol_side",
    "spruce_log", "spruce_log_top", "birch_log", "birch_log_top", "jungle_log", "jungle_log_top",
    "acacia_log", "acacia_log_top", "dark_oak_log", "dark_oak_log_top",
    "cactus_top", "dead_bush", "short_dry_grass", "cornflower", "dandelion", "azure_bluet", "blue_orchid",
]

# Vanilla foliage colours for each biome family.
TINTED = {
    "spruce_leaves": ("spruce_leaves", 0x619961),
    "birch_leaves": ("birch_leaves", 0x80A755),
    "jungle_leaves": ("jungle_leaves", 0x30BB0B),
    "acacia_leaves": ("acacia_leaves", 0xAEA42A),
    "dark_oak_leaves": ("dark_oak_leaves", 0x59AE30),
    "fern": ("fern", 0x86B783),
}

GRASS_VARIANTS = {"dry": 0xBFB755, "swamp": 0x6A7039, "lush": 0x59C93C, "cold": 0x86B783}


def fetch(name: str) -> Image.Image:
    with urllib.request.urlopen(BASE + name + ".png", timeout=30) as resp:
        return Image.open(io.BytesIO(resp.read())).convert("RGBA")


def tint(img: Image.Image, rgb: int) -> Image.Image:
    r, g, b = (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255
    out = img.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            pr, pg, pb, pa = px[x, y]
            px[x, y] = (pr * r // 255, pg * g // 255, pb * b // 255, pa)
    return out


def main() -> None:
    for name in COPY:
        fetch(name).save(OUT / f"{name}.png")

    for out_name, (src, rgb) in TINTED.items():
        tint(fetch(src), rgb).save(OUT / f"{out_name}.png")

    # The cactus model is inset 1/16 per side; the engine renders full cubes, so crop the inset away.
    side = fetch("cactus_side")
    inset = side.width // 16
    side.crop((inset, 0, side.width - inset, side.height)).resize(side.size, Image.NEAREST).save(OUT / "cactus_side.png")

    grass_top, dirt, overlay = fetch("grass_block_top"), fetch("dirt"), fetch("grass_block_side_overlay")
    for variant, rgb in GRASS_VARIANTS.items():
        tint(grass_top, rgb).save(OUT / f"grass_block_top_{variant}.png")
        side_img = dirt.copy()
        side_img.alpha_composite(tint(overlay, rgb))
        side_img.save(OUT / f"grass_block_side_{variant}.png")

    print(f"wrote {len(COPY) + len(TINTED) + 1 + 2 * len(GRASS_VARIANTS)} textures to {OUT}")


if __name__ == "__main__":
    main()
