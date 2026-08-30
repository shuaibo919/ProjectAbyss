import glob
from PIL import Image

BASE = r"D:\VibeSpace\ProjectAbyss\Game\Develop\InkShots"
ref = Image.open(BASE + r"\rock_hole_scan_ref.png").convert("RGB")
pr = ref.load()
bg = (89, 96, 107)
amber = (255, 229, 153)

for path in sorted(glob.glob(BASE + r"\rock_hole_scan_[0-9].png")):
    im = Image.open(path).convert("RGB")
    p = im.load()
    holes = []
    for y in range(im.height):
        for x in range(im.width):
            ca = p[x, y]
            cb = pr[x, y]
            # Hole = background in the single-sided ref, covered by the default
            # (two-sided) material in the scan shot.
            if (abs(cb[0] - bg[0]) < 14 and abs(cb[1] - bg[1]) < 14 and abs(cb[2] - bg[2]) < 14) and \
               not (abs(ca[0] - bg[0]) < 14 and abs(ca[1] - bg[1]) < 14 and abs(ca[2] - bg[2]) < 14):
                holes.append((x, y))
    # Cluster summary: all-holes plus bounding box.
    if holes:
        xs = [h[0] for h in holes]
        ys = [h[1] for h in holes]
        print("%s: holes=%d bbox=(%d..%d, %d..%d)" % (path.split("\\")[-1], len(holes), min(xs), max(xs), min(ys), max(ys)))
    else:
        print("%s: holes=0" % path.split("\\")[-1])
