import itertools, re

# Search for the (corner mirror, bit inversion) combination that makes the extracted
# Bourke table wind outward for ALL 256 cases under our edge numbering. The table was
# extracted from Triangulation.compute (a shader), whose corner numbering has its own
# axis convention; the cpp build uses a hand-written CornerOffset. A mismatch of that
# kind flips a subset of cases — exactly the symptom at hand.

BASE_CORNERS = [
    (0, 0, 1), (1, 0, 1), (1, 0, 0), (0, 0, 0),
    (0, 1, 1), (1, 1, 1), (1, 1, 0), (0, 1, 0),
]
EDGES = [
    (0, 1), (1, 2), (2, 3), (3, 0),
    (4, 5), (5, 6), (6, 7), (7, 4),
    (0, 4), (1, 5), (2, 6), (3, 7),
]

src = open(r"D:\VibeSpace\ProjectAbyss\Source\RockGen\RockMarchingCubesTables.h", encoding="utf-8").read()
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
assert len(edge_table) == 256 and len(tri_table) == 256


def mirror(corners, axis, flip):
    out = []
    for c in corners:
        v = list(c)
        if flip:
            v[axis] = 1 - v[axis]
        out.append(tuple(v))
    return out


def edgelines(corner_table):
    return [(corner_table[a], corner_table[b]) for a, b in EDGES]


def check(corner_table, invert_bits):
    bad = 0
    for case in range(256):
        bits = ((~case) & 0xFF) if invert_bits else case
        inside = [i for i in range(8) if not (bits >> i) & 1]
        outside = [i for i in range(8) if (bits >> i) & 1]
        if not inside or not outside:
            continue
        g_in = tuple(sum(corner_table[i][k] for i in inside) / len(inside) for k in range(3))
        g_out = tuple(sum(corner_table[i][k] for i in outside) / len(outside) for k in range(3))
        dirv = tuple(g_in[k] - g_out[k] for k in range(3))
        for i in range(0, len(tri_table[case]), 3):
            if tri_table[case][i] == -1:
                break
            pts = [tuple(a + (b - a) * 0.5 for a, b in zip(*edgelines(corner_table)[e])) for e in tri_table[case][i:i + 3]]
            p0, p1, p2 = pts
            u = tuple(p1[k] - p0[k] for k in range(3))
            v = tuple(p2[k] - p0[k] for k in range(3))
            n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            if sum(n[k] * dirv[k] for k in range(3)) < 0:
                bad += 1
    return bad


for invert in (False, True):
    for res in itertools.product((True, False), repeat=3):
        corners = list(BASE_CORNERS)
        for axis, r in enumerate(res):
            corners = mirror(corners, axis, r)
        bad = check(corners, invert)
        print("corner-flip=%s invert_bits=%s -> bad_tris=%d" % (res, invert, bad))
        if bad == 0:
            print("  MATCH: use this convention")
