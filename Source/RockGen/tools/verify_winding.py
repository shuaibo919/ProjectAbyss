import re, os

# Winding self-check for the marching-cubes tables as generated into
# RockMarchingCubesTables.h.
#
# For every one of the 256 cube cases, checks each table triangle against two
# independent, necessary conditions:
#
#   1. local_outward: at the triangle's OWN centroid, the gradient of the trilinear
#      interpolation of a synthetic scalar field (+1 at outside corners, -1 at inside
#      corners per the bit convention) must point in roughly the same direction as the
#      triangle's geometric normal. This is the same trilinear machinery
#      RockMeshBuilder.cpp uses for real vertex normals, just with a synthetic
#      per-corner value instead of the sampled SDF -- and unlike a single whole-cell
#      "average inside corner" reference point, it stays locally meaningful for
#      saddle/diagonal cube configurations where inside corners sit on multiple sides.
#      (v1 of this script used that whole-cell-average heuristic; it is too coarse and
#      flags ~32 triangles as "reversed" that are not -- see tools/repair_winding.py.)
#   2. case_internal_consistent: within one case, every cube edge shared by two
#      triangles must be walked in opposite directions by them (the standard mesh
#      orientation-consistency invariant). A table that passes (1) triangle-by-triangle
#      but fails (2) still has a real winding bug -- some triangle is flipped relative
#      to its neighbour even though each individually "looks outward".
#
# Corner order must match RockMeshBuilder.cpp CornerOffset().
CORNERS = [
    (0, 0, 1), (1, 0, 1), (1, 0, 0), (0, 0, 0),
    (0, 1, 1), (1, 1, 1), (1, 1, 0), (0, 1, 0),
]

# Edge end corners, same numbering as the tables (RockMeshBuilder.cpp EdgeCorners()).
EDGES = [
    (0, 1), (1, 2), (2, 3), (3, 0),
    (4, 5), (5, 6), (6, 7), (7, 4),
    (0, 4), (1, 5), (2, 6), (3, 7),
]

HEADER = os.path.join(os.path.dirname(__file__), "..", "RockMarchingCubesTables.h")
src = open(HEADER, encoding="utf-8").read()

edge_table = []
tri_table = []

m = re.search(r"EDGE_TABLE\[256\]\s*=\s*\{(.*?)\};", src, re.S)
for tok in re.findall(r"0x([0-9A-Fa-f]+)", m.group(1)):
    edge_table.append(int(tok, 16))

m = re.search(r"TRI_TABLE\[256\]\[16\]\s*=\s*\{(.*?)\};", src, re.S)
for row in re.findall(r"\{([^}]*)\}", m.group(1)):
    vals = [int(tok.strip()) for tok in row.split(",") if tok.strip() != ""]
    tri_table.append(vals)

assert len(edge_table) == 256, len(edge_table)
assert len(tri_table) == 256, len(tri_table)


def edge_point(e, t=0.5):
    a = CORNERS[EDGES[e][0]]
    b = CORNERS[EDGES[e][1]]
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def sub(x, y):
    return tuple(x[i] - y[i] for i in range(3))


def cross(u, v):
    return (u[1] * v[2] - u[2] * v[1],
            u[2] * v[0] - u[0] * v[2],
            u[0] * v[1] - u[1] * v[0])


def dot(u, v):
    return sum(u[i] * v[i] for i in range(3))


def local_outward(case, tri):
    corner_value = [1.0 if (case >> i) & 1 else -1.0 for i in range(8)]
    pts = [edge_point(e) for e in tri]
    centre = tuple(sum(p[k] for p in pts) / 3.0 for k in range(3))
    grad = [0.0, 0.0, 0.0]
    for c in range(8):
        bx = centre[0] if CORNERS[c][0] else (1 - centre[0])
        by = centre[1] if CORNERS[c][1] else (1 - centre[1])
        bz = centre[2] if CORNERS[c][2] else (1 - centre[2])
        dbx = 1.0 if CORNERS[c][0] else -1.0
        dby = 1.0 if CORNERS[c][1] else -1.0
        dbz = 1.0 if CORNERS[c][2] else -1.0
        grad[0] += corner_value[c] * dbx * by * bz
        grad[1] += corner_value[c] * bx * dby * bz
        grad[2] += corner_value[c] * bx * by * dbz
    n = cross(sub(pts[1], pts[0]), sub(pts[2], pts[0]))
    return dot(n, tuple(grad)) > 0


def case_internal_consistent(tris):
    dirs = {}
    for a, b, c in tris:
        for u, v in ((a, b), (b, c), (c, a)):
            key = (min(u, v), max(u, v))
            forward = u < v
            if key in dirs and dirs[key] == forward:
                return False
            dirs[key] = forward
    return True


bad_outward = []
bad_consistency = []
for case in range(256):
    row = tri_table[case]
    tris = [tuple(row[i:i + 3]) for i in range(0, len(row), 3) if row[i] != -1]
    if not tris:
        continue

    for tri in tris:
        if not local_outward(case, tri):
            bad_outward.append((case, tri))

    if not case_internal_consistent(tris):
        bad_consistency.append(case)

if bad_outward or bad_consistency:
    if bad_outward:
        print("REVERSED triangles (%d):" % len(bad_outward))
        for case, tri in bad_outward:
            print("  case 0x%02x tri %s" % (case, tri))
    if bad_consistency:
        print("INTERNALLY INCONSISTENT cases (%d) -- a triangle is flipped relative " \
              "to a neighbour it shares a cube edge with:" % len(bad_consistency))
        for case in bad_consistency:
            print("  case 0x%02x" % case)
else:
    print("ALL 256 cases wound outward and internally consistent -- " \
          "tables + corner/edge conventions are correct.")
