import re

CORNERS = [(0,0,1),(1,0,1),(1,0,0),(0,0,0),(0,1,1),(1,1,1),(1,1,0),(0,1,0)]
EDGES = [(0,1),(1,2),(2,3),(3,0),(4,5),(5,6),(6,7),(7,4),(0,4),(1,5),(2,6),(3,7)]

src = open(r"D:\VibeSpace\ProjectAbyss\Source\RockGen\RockMarchingCubesTables.h", encoding="utf-8").read()
m = re.search(r"TRI_TABLE\[256\]\[16\]\s*=\s*\{(.*?)\};", src, re.S)
rows = []
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
    rows.append(vals)

print("tri_table rows:", len(rows))
print("row 0:", rows[0][:4])
print("row 1:", rows[1][:4])

inside = [i for i in range(8) if not (1 >> i) & 1]
outside = [i for i in range(8) if (1 >> i) & 1]
print("inside:", inside, "outside:", outside)
g_in = tuple(sum(CORNERS[i][k] for i in inside) / len(inside) for k in range(3))
g_out = tuple(sum(CORNERS[i][k] for i in outside) / len(outside) for k in range(3))
print("g_in:", g_in, "g_out:", g_out)
direction = tuple(g_in[k] - g_out[k] for k in range(3))
print("direction:", direction)

def edge_point(e, t=0.5):
    a = CORNERS[EDGES[e][0]]
    b = CORNERS[EDGES[e][1]]
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))

tri = rows[1][:3]
print("tri edges:", tri)
for k in tri:
    print("edge", k, "->", edge_point(k))

p0, p1, p2 = [edge_point(e) for e in tri]
u = tuple(p1[i] - p0[i] for i in range(3))
v = tuple(p2[i] - p0[i] for i in range(3))
n = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
print("n:", n, "dot:", sum(n[i]*direction[i] for i in range(3)))
