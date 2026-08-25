# AncientBuilding — verified spec from Hu & Qin 2020

Working spec for a procedural ancient-Chinese-architecture generator, distilled from:

> Zhongtian Hu, Xujia Qin. *Extended interactive and procedural modeling method for
> ancient Chinese architecture.* Multimedia Tools and Applications, 2020.
> doi:10.1007/s11042-020-09744-2

The PDF is at `Reference/hu2020.pdf` (gitignored). Page renders for reading figures and
formulas are at `Reference/hu2020_figures/page_NN.png` (also gitignored — regenerate with
the snippet at the bottom of this file).

Everything below has been **read off the rendered pages**, not from text extraction — the
maths does not survive text extraction intact.

---

## 1. Figure / equation index

| Page | Contents |
|---|---|
| 02 | Fig 1 — the nine roof types |
| 03 | Fig 2 — eight body structures; **Fig 3 — component vocabulary** |
| 07 | Fig 4 — their framework; Fig 5 — spline distortion problem + vertex calc diagram |
| 08 | **Eq 1-5** — vertex position, displacement curve, direction constraints |
| 09 | Fig 6 — components built from spline meshes; **Eq 6-7** — instantiation |
| 10 | Fig 7 — instance transform; Fig 8 — cutting out the excess |
| 11 | Fig 9 — radial culling areas; Fig 10 — fence/wall results |
| 12 | **Fig 11 — hierarchical tree of the built-in frame**; Eq 8 — sides/ratio constraint |
| 13 | Fig 12 — frame at sides=4 vs 8; Fig 13 — three-part hierarchy |
| 14 | **Fig 14 — roof frame control points**; **Fig 15 — roof segmentation + subsplines**; Eq 9-11 |
| 15 | **Table 1 — default frame properties**; Eq 12-13; Fig 16 |
| 16-21 | Frame extraction from existing meshes (Eq 14-19) — **out of scope** |
| 21-22 | Eq 20 + Fig 25-26 — their LOD scheme — **out of scope** |
| 23-33 | Results, user study, timing/size tables |

## 2. Roof taxonomy (Fig 1) — and why it is two generators, not nine

| Fig 1 label | Chinese | Family |
|---|---|---|
| Pyramidal | 攒尖顶 | centralised |
| Round | 圆攒尖 | centralised |
| Helmet | 盔顶 | centralised |
| Hollow | 盝顶 | centralised |
| Flush Gable | 硬山 | ridged |
| Overhanging gable | 悬山 | ridged |
| Hip | 庑殿 | ridged |
| Gable and hip | 歇山 | ridged |
| Round Ridge | 卷棚 | ridged |

Eq 8 makes the split structural rather than cosmetic:

```
sides == 4  ⟺  ratio != 1
sides != 4  ⟺  ratio == 1        where ratio = Building.width / Building.depth
```

So a rectangular plan is always 4-sided (→ ridged roofs), and a polygonal plan is forced
square/regular (→ centralised roofs). **Two roof generators cover all nine types**; the
differences within a family are ridge-curve shape and eave treatment.

## 3. Component vocabulary

From Fig 3 (labelled photo of an exploded building) and Fig 11 (layer 0 leaves):

- **Roof**: round tile, flat tile, drop tile, rafter, ridges
- **Body**: ZuoDou (座斗), Gong (拱) — i.e. bracket sets; column, door, window, wall
- **Base**: baluster, balustrade panel, balustrade post, steps' body, vertical band, platform

Fig 3 also shows **top / middle / bottom roof** — multi-tier roofs are a stack of the same
roof generator, not a special case.

## 4. The hierarchical tree (Fig 11) — maps onto a PCG graph

Five layers, root at layer 4:

```
Layer 4  Building
Layer 3  Roof                    Body                      Base
Layer 2  Roof boarding           Brackets   Room           Fence
Layer 1  Roof tiles              Bucket arch  Room comps    Fence comps   Steps
Layer 0  round/flat/drop tile    ZuoDou Gong  Column Door   Baluster      Steps' body
         rafter, ridges                       Wall          Balustrade    Vertical band
                                                                          Platform
```

Two edge kinds:
- **green** = the child was produced by *component instantiation* along the parent's spline
  (`id_high = id_low + 1`)
- **grey** = the child was produced by *direct assembly* of several nodes
  (`id_high = max(child ids) + 1`)

Editing a node dirties everything from it up to the root.

**This is a DAG with layer-indexed dirty propagation — i.e. exactly what PCGODOT's
evaluator already is.** We do not need to build this; we need to express the building
schema *as* a graph.

## 5. Surface operation A — spline mesh generation (Eq 1-5)

### 5.1 Vertex position (Eq 1) — a miter join, not a swept contour

Given spline knots `p_i` with tangent/normal/bitangent `T_i, N_i, B_i` (i ∈ [1,n]) and a
user contour of `m` knots placed on the plane of `(T_1, p_1)`:

```
d          = (p_{i+1} - p_i) / ||p_{i+1} - p_i||
c_j^{i+1}  = c_j^i + [ (-T_{i+1} · (p_{i+1} - c_j^i)) / (-T_{i+1} · d) ] · d
```

The double negation cancels, so in implementation terms this is a **ray/plane intersection**:

```
t = dot(T_{i+1}, p_{i+1} - c_j^i) / dot(T_{i+1}, d)
c_j^{i+1} = c_j^i + t * d
```

Read carefully, because this is the paper's actual contribution and it is easy to
mis-implement: **the contour is never re-oriented.** Each contour vertex travels in a
straight line along the *segment* direction `d` and is stopped by the plane perpendicular
to the *next knot's tangent*. That is a miter joint, the same construction as offsetting a
polyline.

Consequences worth knowing before writing it:
- Adjacent segments share their boundary ring exactly, so side faces stay planar and
  connected — this is why acute corners no longer pinch (Fig 5a/5b vs 5c).
- Cross-section area is **not** preserved through a bend: the outside of the corner widens.
  That is correct for tiled ridges and is what the old sweep got wrong.
- **Degeneracy**: `dot(T_{i+1}, d) → 0` when the turn approaches 180°, and `t` explodes.
  Needs a guard (clamp `t`, or split the knot). The paper does not mention this.

### 5.2 Displacement curve (Eq 2)

```
c_disp[i][j] = c_j^i + φ · y(i/n) · normalize(c_j^i - p_i)
```

A user curve `y: [0,1] → R` scaled by `φ`, displacing each contour vertex **radially** away
from its spine point. Note the parameter is `i/n`, i.e. indexed by *knot number*, not by
arc length. Godot's native `Curve` resource is a drop-in for `y`.

### 5.3 Direction constraints (Eq 3-5)

```
c_constrain[i][j] = c_j^i + φ · y(i/n) · W · D

D = normalize(c_j^i - p_i)   if δ >  π/2
  = B_i                      otherwise

W = 1   if α ≤ δ
  = 0   otherwise            where α = angle(B_i, c_j^i - p_i)
```

Semantics: with `δ ≤ π/2` only the contour vertices facing "up" (within `δ` of the
bitangent) are selected, and they move along the fixed bitangent instead of radially. That
is the mechanism for one-sided profiles — steps, ridge tails (Fig 6).

## 6. Surface operation B — component instantiation (Eq 6-7)

Per spline segment, with `d` = the width of the source component along its own axis:

```
k       = ceil( ||segment|| / d )                      (Eq 6)
s_l^i   = p_i + (l-1) · d · normalize(p_{i+1} - p_i)   (Eq 7),  l ∈ [1,k]
```

Instances keep the source scale; orientation comes from `p_i → p_{i+1}`.

- **Cutting out the excess** (§3.2.2): the last instance overhangs; clip it against the
  plane at `p_{i+1}` with normal `p_i → p_{i+1}`.
- **Radial culling** (§3.2.3, Fig 9): two params — `ω` = culling-area width, `λ` = number of
  dichotomy iterations, giving `2^λ` culling directions (`λ=0 → {0}`, `λ=1 → {0, π}`,
  `λ=2 → {0, π/2, π, 3π/2}`). An instance whose bounding box lies wholly inside a culling
  area is dropped; partially inside, it is clipped. This is how door and step openings are
  punched out of a fence or wall ring.

## 7. Roof construction (Fig 14, Fig 15)

Fig 14c gives the improved roof frame as a small set of named control points:

- `p^BD` building datum, `p^MRg` main ridge, `p^Rg` ridge, `p^RgT` ridge tail, `p^Ev` eave,
  `p^EvC` eave corner
- bending handles `b^RfS` (roof surface), `b^Rg`, `b^RgT`, `b^Ev`

Ridge shape is a **cubic Bezier**, which is what lets the frame express the helmet and
round-ridge roofs that the previous method could not.

The tile mechanism (Fig 15) is the part to copy:

1. The roof surface is bounded by **ridge curves** (red in Fig 15).
2. Segment that surface and emit **sub-splines** across it (blue) — these are the rafter /
   tile-course lines. Their knots are the cyan dots.
3. Instantiate a tile component along each sub-spline (operation B).
4. Generate ridges and rafters by running operation A on the ridge curves and sub-splines.
5. `Cr ∈ [0,1]` controls tile coverage independently of ridge shape: `Cr=1` covers the whole
   roof, `Cr=0.4` covers only the upper 40% and leaves the rest exposed (Fig 15a vs 15b).

So the roof is a **lofted surface between ridge curves, resampled into sub-splines**, not a
swept tube. Both operations A and B then apply to it.

### Roof pitch (Eq 9-10)

Stated for frame extraction, but it is a usable default for the built-in frame too:

```
Roof.height = 1.3 × Building.depth / ((n_R - 1) / 2)
```

`n_R` is set by roof type, minimum 5. This is effectively 举架 — `n_R` is the number of
rafter courses (步架). Ties roof height to plan depth instead of exposing it raw.

## 8. Frame derivation (Table 1)

One module drives everything:

```
D = Building.width × 0.8 × (1/11)
```

| Part | Component | Property | Default |
|---|---|---|---|
| Roof | | position | `(0, 11D + bracket.height, 0)` |
| Body | Brackets | position | `(0, 11D, 0)` |
| | Wall | position | `(0, Base.Platform.height, 0)` |
| Base | Fence | position | `(0, Base.Platform.height, 0)` |
| | | ω | `fence.width × 2` |
| | | λ | `1` |
| | Platform | position | `(0, 0, 0)` |
| | | height | `2D` |
| | Steps | position | `(0, 0, 0)` |
| | | width | `Fence.ω` |
| | | depth | `Steps.width × 1.1` |
| | | number | `2^Fence.λ` |
| | | direction | from `Fence.λ` |

`fence.width` and `bracket.height` come from whichever components the user supplied.

Since `11D = Building.width × 0.8`, the whole table reduces to one readable rule:

> **Eave height = 0.8 × building width.** The platform takes 2/11 of it, the wall the
> remaining 9/11, and the roof sits on top of the brackets.

This is a simplification of the 材份/斗口 module system of the *Yingzao Fashi*. Good enough
for games; say so in the code so nobody mistakes it for authoritative.

## 9. Deliberately out of scope

| Section | Why |
|---|---|
| §4.3 frame extraction (Eq 12-19, Fig 16-24) | Voxelisation → water-fill → split-line search → ray sampling → clustering. Its purpose is letting a user start from a downloaded mesh; we have no model library and no such need. ~6 pages of machinery for zero benefit here. |
| §5 LOD (Eq 20, Fig 25-26) | Re-bakes geometry against camera distance and projects the building to a view-dependent billboard with angle/NDC tolerances. Fights Godot, which already has `visibility_range_*` with hysteresis on `GeometryInstance3D`, plus MultiMesh. Better: bake 2-3 static LODs from the same frame at different segment resolutions — the approach already proven by `ProceduralTree.radial_segments`. |

## 10. Implementation status

### Phase 1 — spline mesh generation (§3.1, Eq 1-5) — **done, verified**

- `Source/AncientBuilding/SplineSweep.h/.cpp` — the kernel. Plain C++ in namespace
  `AncientBuilding`, no Godot classes beyond `Vector2`/`Vector3`.
- `Source/AncientBuilding/AncientSplineSweep.h/.cpp` — `MeshInstance3D` tool node reading a
  `Path3D`. Contour presets (square / round / ridge tile / step) or a custom
  `PackedVector2Array`; `Curve` for y(x); φ, δ; and a `mode` toggle between the paper's
  miter and the prior art.
- `Game/Develop/SweepValidate.tscn` — the harness. Renders to `Game/Develop/SweepShots/`
  (gitignored) and prints numeric PASS/FAIL for the direction constraints.

Decisions taken while implementing, that the paper does not cover:

1. **Interior tangents are the segment bisector.** The paper says "the tangent of each spline
   knot" without defining it for a corner curve. It must be `normalize(d[i-1] + d[i])` — if
   it were the incoming segment direction the plane would give a butt joint, not a miter, and
   the whole point is lost.
2. **Degeneracy guard.** `dot(T[i+1], d) → 0` as a turn approaches 180° and `t` diverges.
   Travel is clamped to 8× the segment length and the joint is counted; the node exposes
   `get_degenerate_joint_count()` so a caller knows to subdivide. Not mentioned in the paper.
3. **The bitangent is parallel-transported**, not re-derived from a fixed world up, or a
   curving ridge twists.
4. **`y` is sampled at `i/(n-1)`, not the paper's `i/n`**, so the curve's far end lands on the
   last knot. Visible only at the ends.
5. **Vertices are duplicated per contour edge.** Profile corners stay hard, the sweep
   direction stays smooth — correct shading for an extrusion, and it costs nothing since the
   strips are generated independently anyway.
6. Equations 2 and 3-5 are one code path: at the default δ = 180 the constraint is inert and
   the result is plain equation 2.

Verification (`SweepValidate`):

- **Acute corner.** With the round profile on a ~40° corner, `mode = Frame` collapses the
  tube to a knife edge (reproducing Fig 5b) while `mode = Miter` carries the full section
  through the bend. Same vertex and triangle counts in both modes — only positions differ.
- **Direction constraints, numerically.** Unit square profile, φ = 0.5, up = +Y:
  δ = 180 gives y ∈ [-0.854, 0.854], matching the predicted `0.5 + 0.5/√2` for corners at
  45°; δ = 60 gives y ∈ [-0.5, 1.0] with z untouched, i.e. only the two top corners moved and
  by exactly φ along the bitangent. Both assertions PASS.
- `MaxMiterStretch` reports travel relative to segment length (1.16 on the test corner), so
  it is a "should I subdivide" signal rather than a distortion measure.

### Phase 2 — frame, base, body and the flush-gable roof — **done, verified. 歇山 outstanding.**

- `AncientBuildingParameters.h/.cpp` — plan/base/body/roof knobs plus the Table 1 derivation as
  read-only getters, all bound so tests can assert them.
- `BuildingBuilder.h/.cpp` — `MeshAccumulator` (quads, boxes, polygons, tapered columns,
  sweeps) and the part builders. One vertex-coloured surface, matching the
  `PcgVillageMeshes` contract.
- `AncientBuilding.h/.cpp` — the node. `mesh` is excluded from serialisation, same as
  `ProceduralTree`.
- `Game/Develop/BuildingValidate.tscn` — renders to `Game/Develop/BuildingShots/`
  (gitignored) and asserts Table 1 numerically.

Built: platform with 阶条石 cap; stair runs; balustrade with posts, panels and swept rail;
columns with 收分 taper on the bay grid; walls per perimeter bay; bracket band with a 斗 block
per column; 硬山 roof with the 举架 curve, boarding, 瓦垄 tile courses, 正脊, 垂脊 and the
滴水 eave course.

Where the sweep is used versus boxes: ridges, eave courses, tile courses, the balustrade rail
and the stair blocks all go through `BuildSweep`. Platform, columns and walls are boxes,
because dressing a prism up as a sweep buys nothing.

**The stair is the payoff for porting equations 3-5.** A stair run is one swept block whose
*top* is stepped by the direction constraint: a closed rectangular contour, two knots per
tread (offset by 4% of the tread so the riser is near-vertical rather than a ramp), and one
displacement sample per knot. Because `SampleDisplacement` is evaluated at exactly `i/(n-1)`,
passing one sample per knot gives exact per-knot control — which makes the mechanism fully
general, not just a smooth curve. δ is *derived* from the block's own proportions
(`atan2(width/2, height/2) + 6°`, capped at 89°) rather than hardcoded; the paper leaves δ to
the user, which is fragile for a block this flat.

Verification: all seven Table 1 assertions pass — `D = width·0.8/11`, eave height `= 11D
= 0.8·width`, platform `= 2D`, column `= 9D`, roof base `= 11D + bracket`, roof height from
equation 10, and step runs `= 2^λ`. A 3-bay hall is ~4.6k triangles.

Two bugs the renders caught:

1. **ω was derived from the bay width.** On a one-bay pavilion that meant 75% of the whole
   frontage, and since `Steps.depth = ω × 1.1` the footprint tripled — a 5 m pavilion measured
   17 m across. Table 1's ω is twice one *balustrade component*, which scales with the module,
   not the bay: now `3.2D`, clamped to 90% of the platform half-width.
2. **Roof boarding was inside-out.** Going up-slope means z decreases, so the naive corner
   order produced a normal pointing into the roof. Invisible at `Cr = 1` because the tiles hid
   it; only `Cr = 0.4` exposed it. Worth remembering that full tile coverage masks boarding
   errors.

Also note `RafterCourses` defaults to 5, not 7: equation 10 is inverse in `n_R`, and the paper
takes 5 as its minimum. At 7 the roof was only a quarter of the total height, which does not
read as Chinese.

### Phase 3a — 歇山 and 翼角起翘 — **done, verified**

**A correction to the earlier plan:** doing 翼角起翘 before 歇山 to "improve 硬山" was wrong.
硬山 has no 翼角 — the corner upturn needs a hipped corner with a diagonal 角梁, and 硬山's
gables are flush walls. There is no corner to lift. The two features therefore had to be built
together, which is also how they relate structurally.

**歇山** is built as two tiers:

1. A hipped skirt **lofted** between the eave rectangle and the 收山 break rectangle. Insetting
   the break by the same distance in X and Z puts the hip ridges at 45° in plan, which is
   exactly what makes all four slopes share one pitch. Lofting rings also produces the four
   faces *and* the four corner wedges in one pass, with no corner special-casing.
2. A gabled tier above it, with a vertical 山花 tympanum at each end.

Ridges: 正脊 along the apex, four 垂脊 down the tier edges, four 戗脊 running diagonally from
the break corners out to the flipped eave corners, and a 滴水 drip course that closes the loop
around the whole eave — closing it matters, because that is what makes the last corner miter
against the first side instead of leaving a butt end.

**翼角起翘 is a deformation, not geometry.** Structurally the corner rafter is longer and tilts
up, dragging the eave with it, so `CornerFlip::Apply` is run over *every* roof vertex —
boarding, tiles, ridges and the drip course all pass through it and stay mutually consistent.
The weight is `(1 - d/Span)²` where `d = max(inset from ±X edge, inset from ±Z edge)`, which is
zero only at the plan corner and equals the along-eave distance on either edge. The squared
falloff keeps the eave flat along most of its length and turns it up sharply near the corner.
起翘 lifts in Y, 出翘 pushes out diagonally in X and Z.

Verified: X extent 13.1 with the flip off, 14.3 at default, 15.5 at 3× — with **identical
vertex and triangle counts**, confirming it is a pure deformation. All Table 1 assertions still
pass. A 歇山 hall is ~11k triangles.

One bug the renders caught: **tile courses were coplanar with the boarding and z-fought into a
mottled mess.** Present with the flip both on and off, so not a flip problem. `MakeTileContour`
now takes a `Lift` that raises the chord off the surface by `0.07D` — which is also physically
right, since tiles sit on battens. Worth remembering as a general rule for this module: any
sweep laid onto a generated surface needs an explicit lift.

### Phase 3b — 庑殿 — **done, verified**

庑殿 and 歇山 are one function, `BuildHippedRoof(Spec, bFullHip, Mesh)`. 庑殿 is the same hipped
shell with the inset taken all the way to the half-depth: the inner rectangle collapses to a
line, and that line **is** the main ridge. The diagonal ridges 歇山 calls 戗脊 become 庑殿's 垂脊
with no change. So the whole roof type cost about fifteen lines of gating.

A pleasant consequence: a **square** plan on full hip collapses the ridge to a point, which is a
攒尖 pyramid — verified rendering correctly, so the centralised family is partly reachable
already. The main ridge is skipped when it has no length.

Tile courses were also improved while here: they are now spaced over the **eave** extent and
each course is **clipped** where it runs off the face. That tiles the 歇山 corner wedges and the
庑殿 triangular hip ends, both of which previously showed bare boarding. It leaves a serrated
edge at the hip rather than the fan of cut tiles a real 翼角 has — visible up close, reads as
texture at distance.

### Phase 4 — plugin and PCG integration — **done, verified**

`Game/addons/ancient_building/` — `plugin.cfg`, `plugin.gd`, and one Flow node.

- `plugin.gd` registers the node directory through `FlowNodeRegistry.register_node_directory()`.
  **This is the first consumer of that API** — it existed in the addon but nothing called it, so
  suspect it first if node lookup ever misbehaves. It also warns clearly if the `abyss`
  GDExtension is not loaded rather than failing obscurely.
- `nodes/ancient_building.gd` — takes a point set, writes generated meshes into a Resource
  stream (default attribute `mesh`), which the existing `spawn_meshes` node instances into
  MultiMeshInstance3D. No new spawning code needed.
- Meshes are generated **per variant, not per point**. Forty houses cost four meshes; that is
  the difference between usable in a graph and not. Per-point variant choice is seeded, so a
  village is reproducible.

Verified by `Game/Develop/BuildingPcgValidate.tscn`, which builds a graph in code
(grid → transform → ancient_building → spawn_meshes) and asserts three things: the node
resolves through the registry, `bake_mesh()` works on a node that never enters the tree, and the
evaluated graph really produces MultiMesh instances. All PASS — 9 buildings of mixed roof type
from 4 meshes.

### Phase 5 — all nine roof types, plus 斗拱 and 门窗 — **done, verified**

**Three generators cover the nine types**, as predicted from Eq 8:

| Generator | Types | How they differ |
|---|---|---|
| `BuildGabledRoof(bOverhang, bRolled)` | 硬山, 悬山, 卷棚 | gable overhang; rolled ridge |
| `BuildHippedRoof(EHipTop)` | 庑殿, 歇山, 盝顶 | shell termination: ridge / gabled tier / flat cap |
| `BuildCentralisedRoof(ECentralProfile)` | 攒尖, 圆攒尖, 盔顶 | profile shape and facet count |

Things worth knowing about each:

- **卷棚** is built from a *single unbroken profile* running from one eave over the roll to the
  other, not two slopes plus a cap. A tile course therefore spans the whole roof in one sweep,
  which is what a real 卷棚 does.
- **盝顶** is just a third termination of the hipped shell: stop early, cap with a flat polygon,
  ring it with a 围脊.
- **The centralised family needs a polygonal plan** (Eq 8), so it lives in its own file,
  `PolygonalBuilding.cpp`, with its own platform/columns/walls/balustrade. `PlanPolygon` puts
  vertices at `2πk/N + π/N` so edges face the axes — otherwise N=4 gives a diamond, not a square.
- **圆攒尖** is 攒尖 forced to at least 24 facets with the corner flip and ridges switched off.
  A cone is a pyramid with the corners taken away, not a separate construction.
- **盔顶** is a bulged profile: `(1-T) + bulge·sin(πT)·(1-T)` pushes the lower slope *outside*
  the straight line to give the helmet its swollen shoulder. This is the case the paper singles
  out as impossible for the method it replaces.

Two bugs the polygonal renders caught:

1. **The corner flip span was derived from depth**, which on an octagon facet covered the entire
   facet and turned the roof into a fluted umbrella. Polygonal plans now scale the span to the
   facet chord length instead.
2. **Ridges converging on one apex overlapped into a spiky crown.** They now stop at 90% height
   and the 宝顶 finial covers the junction — which is what a finial is actually for.

Also delivered: **翼角切瓦** (clipped tile courses now land exactly on the hip line, killing the
serrated edge), **斗拱** (座斗 block carrying tiers of crossing arms with 升 blocks — boxes only,
but the stepped corbelling is the silhouette that reads), and **门窗** (板门 with two leaves in a
frame on the approached bays, 槛墙 dado plus 棂条 lattice windows elsewhere; the lattice is a bar
grid, since a texture would break the asset-free contract).

Tuning note: the first lattice attempt used a 0.33 m grid of two crossing bar sets and the whole
upper wall read as red timber rather than plaster with windows. Coarser spacing, thinner bars and
a taller dado fixed it. The 补间铺作 intermediate bracket sets were also dropped — at this scale
they crowded the band into one mass.

Costs: 硬山 hall ~9.6k tris, 歇山 ~18k, 庑殿 ~17k, octagonal 攒尖 ~18.5k.

### Phase 6 — editor dock — **done**

`AncientBuildingEditorPlugin` (`Source/AncientBuilding/`), registered from C++ at
`MODULE_INITIALIZATION_LEVEL_EDITOR` via `EditorPlugins::add_by_type` — same as the tree panel, so
there is no `plugin.cfg` and nothing under `addons/` to enable.

**It is contextual**: `_handles` / `_edit` / `_make_visible` show it only while an
`AncientBuilding` is selected. The panel is parented once to `CONTAINER_SPATIAL_EDITOR_SIDE_RIGHT`
and merely shown or hidden.

That location is deliberate. A dock slot was the first attempt and is wrong for this: adding and
removing a dock per selection would steal the Inspector's tab (`DOCK_SLOT_RIGHT_BL` shares its
slot with it) at exactly the moment the user wants the Inspector, and a dock slot is a
`TabContainer` that owns its children's visibility, so plain `set_visible()` cannot be used there
either. A spatial-editor side container has neither problem.

Because the panel is contextual, it does **not** create nodes — the *Add to Scene* button would be
unreachable, and it was redundant anyway since `AncientBuilding` is a registered class that Godot's
Add Node dialog already offers. Dropping it also removed a duplicate roof picker; one now serves.

The inspector already exposes all ~50 parameters, so the dock deliberately does not mirror them.
It carries:

- A **roof picker listing all nine types grouped by generator** (gabled, hipped, centralised),
  labelled in both English and Chinese, plus *Add to Scene* with UndoRedo.
- A **live readout of the Table 1 derivation** — module D, eave height 11D, platform 2D, total
  height. This is the dock's real justification: the entire design is "one number derives the
  rest", and a designer dragging Width should see that happen.
- The knobs worth scrubbing against a viewport: width, depth, bays, sides, rafter courses, roof
  height, corner upturn, tile coverage, and the fence/steps/walls toggles.

**It enforces the Eq 8 coupling**, which is the one place a dock can prevent a confusing result:
choosing a centralised roof snaps a 4-sided plan to an octagon and locks depth to width, and
disables depth and the bay counts, because a polygonal plan must be regular. Switching back to a
ridged roof restores 4 sides.

Note `sides = 4` with a centralised roof is *legal*, not an error — it gives a square-plan 攒尖
built as a rectangular base and body under a converging roof. The snap to 8 is a nicer pavilion
default, so it is applied on entry and not enforced.

Verified: the build is clean and the editor loads with zero errors or warnings, and
`Game/Develop/BuildingMatrixValidate.tscn` walks all nine roof types applying the dock's own
plan-legality rule, asserting each produces real geometry with finite bounds above the ground and
that Table 1 still holds — **9/9 PASS**. That covers the dock's contract; the widget layout itself
has not been eyeballed, since a headless editor cannot show a dock.

### Not yet done

- **多重檐** (Fig 3 shows top/middle/bottom roofs). This is the one remaining item and it is not
  just another roof type: it needs the *body* to become multi-storey — a wall band and bracket
  band per tier, with a roof generated at each level on a shrinking footprint. That is a change
  to `BuildBody`, so it was scoped out rather than rushed.
- 门窗 on the polygonal path; those walls are still plain slabs.
- Interior framing (梁架), which nothing currently sees.

## 11. Regenerating the page renders

```python
import fitz
d = fitz.open('Reference/hu2020.pdf')
mat = fitz.Matrix(2.2, 2.2)   # ~158 dpi, enough to read subscripts
for pno, page in enumerate(d, start=1):
    page.get_pixmap(matrix=mat, colorspace=fitz.csRGB).save(
        f'Reference/hu2020_figures/page_{pno:02d}.png')
```

Extracting the embedded images instead of rendering pages is not worth it: many are CMYK or
indexed and need colourspace conversion, and page renders keep the captions attached.
