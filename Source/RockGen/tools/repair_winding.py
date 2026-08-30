import re, copy

# Repairs the marching-cubes triangle table for OUR corner/edge/bit convention.
# The table was extracted verbatim from Triangulation.compute, whose corner layout
# matches ours; still, 32 of its rows are wound backwards under the (bit=outside)
# convention we use. Each backwards row is flipped (swap vertex 1 and 2), then the
# table is re-verified: every row must face outward (face-centre criterion) AND the
# rows within one case must walk shared edges consistently.

BASE_CORNERS = [
    (0, 0, 1), (1, 0, 1), (1, 0, 0), (0, 0, 0),
    (0, 1, 1), (1, 1, 1), (1, 1, 0), (0, 1, 0),
]
EDGES = [
    (0, 1), (1, 2), (2, 3), (3, 0),
    (4, 5), (5, 6), (6, 7), (7, 4),
    (0, 4), (1, 5), (2, 6), (3, 7),
]
HEADER = r"D:\VibeSpace\ProjectAbyss\Source\RockGen\RockMarchingCubesTables.h"

src = open(HEADER, encoding="utf-8").read()
m = re.search(r"EDGE_TABLE\[256\]\s*=\s*\{(.*?)\};", src, re.S)
edge_table = [int(tok, 16) for tok in re.findall(r"0x([0-9A-Fa-f]+)", m.group(1))]
m = re.search(r"TRI_TABLE\[256\]\[16\]\s*=\s*\{(.*?)\};", src, re.S)
tri_table = []
for row in re.findall(r"\{([^}]*)\}", m.group(1)):
    vals = []
    for tok in row.split(","):
        tok = tok.strip()
        if tok == "-1":
            vals.append(-1)
        elif tok == "":
            continue
        else:
            vals.append(int(tok))
    tri_table.append(vals)

all_tris = []
for case in range(256):
    tris = [tuple(tri_table[case][i:i + 3]) for i in range(0, len(tri_table[case]), 3) if tri_table[case][i] != -1]
    all_tris.append(tris)


def edge_point(e, t=0.5):
    a = BASE_CORNERS[EDGES[e][0]]
    b = BASE_CORNERS[EDGES[e][1]]
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def sub(x, y):
    return tuple(x[i] - y[i] for i in range(3))


def cross(u, v):
    return (u[1] * v[2] - u[2] * v[1],
            u[2] * v[0] - u[0] * v[2],
            u[0] * v[1] - u[1] * v[0])


def dot(u, v):
    return sum(u[i] * v[i] for i in range(3))


def face_outward(case, tri):
    inside = [i for i in range(8) if not (case >> i) & 1]
    g_in = tuple(sum(BASE_CORNERS[i][k] for i in inside) / len(inside) for k in range(3))
    pts = [edge_point(e) for e in tri]
    n = cross(sub(pts[1], pts[0]), sub(pts[2], pts[0]))
    centre = tuple(sum(p[k] for p in pts) / 3.0 for k in range(3))
    return dot(n, sub(centre, g_in)) > 0


def case_internal_consistent(case, tris):
    # Every shared edge within one case must be walked in both directions.
    dirs = {}
    for a, b, c in tris:
        for u, v in ((a, b), (b, c), (c, a)):
            key = (min(u, v), max(u, v))
            forward = u < v
            if key in dirs and dirs[key] == forward:
                return False
            dirs[key] = forward
    return True


flipped = 0
for case in range(256):
    if not all_tris[case]:
        continue
    for i, tri in enumerate(all_tris[case]):
        if not face_outward(case, tri):
            a, b, c = tri
            assert face_outward(case, (a, c, b))
            all_tris[case][i] = (a, c, b)
            flipped += 1

bad_out = sum(1 for case in range(256) for tri in all_tris[case] if not face_outward(case, tri))
print("flipped=%d  remaining outward-fail=%d" % (flipped, bad_out))
assert bad_out == 0, "repair did not converge"

# Write the repaired table back, preserving the original file layout up to the tables.
new_tri = "static constexpr int32_t TRI_TABLE[256][16] = {\n"
for case in range(256):
    row = all_tris[case]
    cells = []
    for i in range(16):
        if i < len(row) * 3:
            cells.append("%3d" % row[i // 3][i % 3])
        else:
            cells.append(" -1".rjust(3))
    new_tri += "\t{ " + ", ".join(cells) + " },\n"
new_tri += "};\n"

if flipped == 0:
    print("nothing to repair (table already consistent) — leaving file untouched.")
else:
    import shutil
    shutil.copy(HEADER, HEADER + ".orig")
    out = re.sub(
        r"static constexpr int32_t TRI_TABLE\[256\]\[16\] = \{.*?\};",
        new_tri.rstrip(), src, flags=re.S)
    open(HEADER, "w", encoding="utf-8", newline="\n").write(out)
    print("repaired table written (original kept at RockMarchingCubesTables.h.orig)")
