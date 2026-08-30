import sys

# A/B cull-hole diff: counts pixels that are BACKGROUND in the single-sided render
# but covered by rock in the two-sided render. Those are triangles culled from
# outside — see-through holes. Pure Pillow; run with uv run python.

try:
    from PIL import Image
except ImportError:
    print("PIL missing; use uv run --with Pillow python ...")
    sys.exit(2)

BASE = r"D:\VibeSpace\ProjectAbyss\Game\Develop\InkShots"
A = BASE + r"\rock_winding_0.png"      # single-sided
B = BASE + r"\rock_winding_twosided.png"  # two-sided (amber rock)

im_a = Image.open(A).convert("RGB")
im_b = Image.open(B).convert("RGB")
w, h = im_a.size
assert im_b.size == im_a.size

bg = (89, 96, 107)  # 0.35, 0.38, 0.42 in 8-bit
amber = (255, 229, 153)

pa = im_a.load()
pb = im_b.load()

holes = 0
samples = []
for y in range(0, h):
    for x in range(0, w):
        ca = pa[x, y]
        cb = pb[x, y]
        is_bg_a = abs(ca[0] - bg[0]) < 14 and abs(ca[1] - bg[1]) < 14 and abs(ca[2] - bg[2]) < 14
        is_rock_b = abs(cb[0] - amber[0]) < 80 and abs(cb[1] - amber[1]) < 90 and abs(cb[2] - amber[2]) < 90
        if is_bg_a and is_rock_b:
            holes += 1
            if len(samples) < 8:
                samples.append((x, y))

print("total stone-ish bg->rock pixels: %d" % holes)
print("samples:", samples)
