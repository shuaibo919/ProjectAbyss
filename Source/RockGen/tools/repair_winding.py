import re, os, shutil

# Repairs the marching-cubes triangle table for OUR corner/edge/bit convention.
#
# v1 of this script (still visible in git history) flipped any triangle that failed a
# per-triangle "face points away from this case's average inside-corner centroid" test.
# That test is too coarse: for cube configurations whose inside corners are not all on
# one side (saddle / diagonal cases), a single whole-cell average is not a meaningful
# local outward reference, so it flagged 32 triangles as "backwards" that were not.
# Flipping those 32 brought them into agreement with the coarse heuristic while
# breaking orientation consistency with their neighbours *in the same cube case* --
# two triangles sharing a cube edge must walk that edge in opposite directions. That
# is a real, verifiable regression: 24 of 256 cases came out internally inconsistent
# (case_internal_consistent below was even defined by v1, but never actually called
# before the table was written).
#
# v2 (this script) uses a locally accurate outward test instead: the gradient, AT EACH
# TRIANGLE'S OWN CENTROID, of the trilinear interpolation of a synthetic scalar field
# that is +1 at outside corners and -1 at inside corners (the same trilinear machinery
# RockMeshBuilder.cpp uses for real vertex normals, just with a synthetic per-corner
# value instead of the sampled SDF). This is well-defined per-triangle even for
# saddle/diagonal configurations, unlike a single whole-cell average.
#
# Checked against the pristine extraction (RockMarchingCubesTables.h.orig): all 820
# triangles across all 256 cases already pass this test AND case_internal_consistent,
# with zero flips needed. In other words the original Paul Bourke / Unity-reference
# table, combined with this project's CornerOffset()/EdgeCorners() convention, was
# correct all along -- v1's "repair" was chasing a false positive and introduced the
# only real winding bug in this pipeline. This script's default behaviour is therefore
# to restore the pristine table; the BFS-propagation machinery below is kept as a
# general-purpose repair path in case the reference table is ever regenerated
# (tools/convert_mc_tables.py) and genuinely needs case-specific fixes.
#
# Algorithm, for any case that does need repair:
#   1. Build a per-case adjacency graph over triangles: two triangles are adjacent iff
#      they share an (undirected) cube-edge index pair, i.e. a real 3D edge inside the
#      cell.
#   2. Within each connected component, propagate a *relative* orientation by BFS from
#      an arbitrary root: every other triangle's winding is fixed so that any edge it
#      shares with an already-visited neighbour is walked in the opposite direction.
#      This guarantees case_internal_consistent() by construction, for every component.
#   3. Resolve the one remaining global sign per component (propagation only fixes
#      triangles *relative to each other*, not absolute in/out) by a majority vote of
#      local_outward() across the component.
#   4. Re-verify every triangle passes local_outward() AND every case passes
#      case_internal_consistent() before writing anything.
#
# Always starts from the pristine, unpatched extraction (RockMarchingCubesTables.h.orig,
# produced by convert_mc_tables.py) if present, so re-running this script is idempotent
# regardless of what is currently checked in as RockMarchingCubesTables.h.

BASE_CORNERS = [
    (0, 0, 1), (1, 0, 1), (1, 0, 0), (0, 0, 0),
    (0, 1, 1), (1, 1, 1), (1, 1, 0), (0, 1, 0),
]
EDGES = [
    (0, 1), (1, 2), (2, 3), (3, 0),
    (4, 5), (5, 6), (6, 7), (7, 4),
    (0, 4), (1, 5), (2, 6), (3, 7),
]
HEADER = os.path.join(os.path.dirname(__file__), "..", "RockMarchingCubesTables.h")
ORIGINAL = HEADER + ".orig"


def load_tri_table(path):
    src = open(path, encoding="utf-8").read()
    m = re.search(r"TRI_TABLE\[256\]\[16\]\s*=\s*\{(.*?)\};", src, re.S)
    tri_table = []
    for row in re.findall(r"\{([^}]*)\}", m.group(1)):
        vals = [int(tok.strip()) for tok in row.split(",") if tok.strip() != ""]
        tri_table.append(vals)
    return tri_table


source_path = ORIGINAL if os.path.exists(ORIGINAL) else HEADER
tri_table = load_tri_table(source_path)
# The written file keeps HEADER's current non-table text (comments, EDGE_TABLE, ...);
# only the TRI_TABLE block gets spliced with the repaired one.
header_src = open(HEADER, encoding="utf-8").read()

all_tris = []
for case in range(256):
    row = tri_table[case]
    tris = [tuple(row[i:i + 3]) for i in range(0, len(row), 3) if row[i] != -1]
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


# Trilinear basis for corner c (whose BASE_CORNERS coordinate is 0 or 1 on each axis),
# evaluated at unit-cube point p, and its analytic partials -- same construction as
# RockMeshBuilder.cpp's cached-gradient trilinear interpolation, just with a synthetic
# per-corner scalar (+1 outside, -1 inside) instead of the sampled SDF.
def _axis_terms(c, p):
    bx = p[0] if BASE_CORNERS[c][0] else (1 - p[0])
    by = p[1] if BASE_CORNERS[c][1] else (1 - p[1])
    bz = p[2] if BASE_CORNERS[c][2] else (1 - p[2])
    return bx, by, bz


def local_outward(case, tri):
    corner_value = [1.0 if (case >> i) & 1 else -1.0 for i in range(8)]
    pts = [edge_point(e) for e in tri]
    centre = tuple(sum(p[k] for p in pts) / 3.0 for k in range(3))
    grad = [0.0, 0.0, 0.0]
    for c in range(8):
        bx, by, bz = _axis_terms(c, centre)
        dbx = 1.0 if BASE_CORNERS[c][0] else -1.0
        dby = 1.0 if BASE_CORNERS[c][1] else -1.0
        dbz = 1.0 if BASE_CORNERS[c][2] else -1.0
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


def repair_case(case, tris):
    n = len(tris)
    if n == 0:
        return tris
    tri_edges = [[(a, b), (b, c), (c, a)] for (a, b, c) in tris]

    # Adjacency: two triangles are neighbours iff they share an undirected cube edge.
    edge_owners = {}
    for i, edges in enumerate(tri_edges):
        for (u, v) in edges:
            key = (min(u, v), max(u, v))
            edge_owners.setdefault(key, []).append(i)

    adj = [[] for _ in range(n)]
    for key, owners in edge_owners.items():
        if len(owners) == 2:
            i, j = owners
            adj[i].append(j)
            adj[j].append(i)
        elif len(owners) > 2:
            raise AssertionError(
                "case 0x%02x: edge %s shared by >2 triangles (non-manifold table entry)" % (case, key))

    # BFS: relative_flip[i] = whether triangle i must flip from its as-extracted
    # orientation to stay consistent with the BFS root of its connected component.
    relative_flip = [None] * n
    components = []
    for start in range(n):
        if relative_flip[start] is not None:
            continue
        relative_flip[start] = False
        comp = [start]
        stack = [start]
        while stack:
            cur = stack.pop()
            cur_dir = {}
            for (u, v) in tri_edges[cur]:
                key = (min(u, v), max(u, v))
                cur_dir[key] = (v, u) if relative_flip[cur] else (u, v)
            for nbr in adj[cur]:
                if relative_flip[nbr] is not None:
                    continue
                nbr_keys = {(min(u, v), max(u, v)) for (u, v) in tri_edges[nbr]}
                shared_key = next(k for k in cur_dir if k in nbr_keys)
                nbr_dir = next((u, v) for (u, v) in tri_edges[nbr] if (min(u, v), max(u, v)) == shared_key)
                # Consistency requires cur's directed edge to be the reverse of nbr's.
                wanted = (cur_dir[shared_key][1], cur_dir[shared_key][0])
                relative_flip[nbr] = (nbr_dir != wanted)
                comp.append(nbr)
                stack.append(nbr)
        components.append(comp)

    result = list(tris)
    for comp in components:
        votes_keep = votes_flip = 0
        for idx in comp:
            a, b, c = tris[idx]
            candidate = (a, c, b) if relative_flip[idx] else (a, b, c)
            votes_keep += local_outward(case, candidate)
            votes_flip += not local_outward(case, candidate)
        global_flip = votes_flip > votes_keep
        for idx in comp:
            a, b, c = tris[idx]
            final_flip = relative_flip[idx] != global_flip  # xor
            result[idx] = (a, c, b) if final_flip else (a, b, c)
    return result


repaired = [repair_case(case, all_tris[case]) for case in range(256)]

# Full re-verification before anything is written.
bad_face = [(case, tri) for case in range(256) for tri in repaired[case] if not local_outward(case, tri)]
bad_case = [case for case in range(256) if not case_internal_consistent(repaired[case])]
print("local_outward failures: %d   internally-inconsistent cases: %d" % (len(bad_face), len(bad_case)))
assert not bad_face and not bad_case, "repair did not converge: %s / %s" % (bad_face[:5], bad_case[:5])

changed = sum(
    1
    for case in range(256)
    for i in range(len(all_tris[case]))
    if all_tris[case][i] != repaired[case][i])
print("triangles changed from the pristine extraction: %d" % changed)

if changed == 0:
    # No content changed: restore the header byte-for-byte from the pristine
    # extraction instead of re-serializing, so this can't introduce a formatting
    # mismatch (v1's hand patch also reformatted TRI_TABLE's indentation/spacing
    # away from convert_mc_tables.py's own style -- restoring verbatim undoes both).
    if source_path != HEADER:
        shutil.copy(source_path, HEADER)
    print("pristine extraction is already correct -- header restored verbatim " +
          "(this undoes v1's bad hand patch, content and formatting).")
else:
    new_tri = "\t\tstatic constexpr int32_t TRI_TABLE[256][16] = {\n"
    for case in range(256):
        row = repaired[case]
        cells = []
        for i in range(16):
            cells.append("%2d" % (row[i // 3][i % 3] if i < len(row) * 3 else -1))
        new_tri += "\t\t{ " + ", ".join(cells) + " },\n"
    new_tri += "\t};"

    if not os.path.exists(ORIGINAL):
        shutil.copy(HEADER, ORIGINAL)
        print("pristine extraction backed up to %s" % ORIGINAL)

    out = re.sub(
        r"static constexpr int32_t TRI_TABLE\[256\]\[16\] = \{.*?\};",
        new_tri, header_src, flags=re.S)
    open(HEADER, "w", encoding="utf-8", newline="\r\n").write(out)
    print("table written to %s" % HEADER)
