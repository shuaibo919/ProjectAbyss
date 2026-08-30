import re

# Winding self-check for the marching-cubes tables as generated into
# RockMarchingCubesTables.h. For every one of the 256 cube cases we construct a
# synthetic scalar field whose corners are either exactly inside (density >= 0.5)
# or outside (density < 0.5) per the bit convention, interpolate the isosurface
# vertices on the cell edges, and check that each table triangle's geometric
# normal points away from the inside region — i.e. along (mean inside position -
# mean outside position). A triangle failing this is wound backwards and would be
# culled from outside.

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

src = open(__file__.replace(r"\tools\verify_winding.py", r"\RockMarchingCubesTables.h"), encoding="utf-8").read()

edge_table = []
tri_table = []

m = re.search(r"EDGE_TABLE\[256\]\s*=\s*\{(.*?)\};", src, re.S)
for tok in re.findall(r"0x([0-9A-Fa-f]+)", m.group(1)):
    edge_table.append(int(tok, 16))

m = re.search(r"TRI_TABLE\[256\]\[16\]\s*=\s*\{(.*?)\};", src, re.S)
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

bad = []
for case in range(256):
    inside = [i for i in range(8) if not (case >> i) & 1]
    outside = [i for i in range(8) if (case >> i) & 1]
    if not inside or not outside:
        continue
    g_inside = tuple(sum(CORNERS[i][k] for i in inside) / len(inside) for k in range(3))
    # Correct outward reference: a surface triangle faces AWAY from the inside region,
    # i.e. along (face centre - inside centroid). (g_in - g_out was a wrong guess that
    # flips the sign for single-corner cases.)

    row = tri_table[case]
    for i in range(0, len(row), 3):
        if row[i] == -1:
            break
        p0 = edge_point(row[i])
        p1 = edge_point(row[i + 1])
        p2 = edge_point(row[i + 2])
        n = cross(sub(p1, p0), sub(p2, p0))
        centre = tuple((p0[k] + p1[k] + p2[k]) / 3.0 for k in range(3))
        direction = sub(centre, g_inside)
        if dot(n, direction) < 0:
            bad.append((case, (row[i], row[i + 1], row[i + 2])))

if bad:
    print("REVERSED triangles (%d cases affected):" % len(bad))
    for case, tri in bad:
        print("  case 0x%02x tri %s" % (case, tri))
else:
    print("ALL 256 cases wound outward — tables + corner/edge conventions are consistent.")
