# Real-Time GPU Tree Generation

`Source/TreeGen/` is a CPU port of the HLSL sample accompanying:

> **Real-Time GPU Tree Generation**
> Bastian Kuth, Max Oberberger, Carsten Faber, Pirmin Pfeifer, Seyedmasih Tabaei,
> Dominik Baumeister and Quirin Meyer.
> *High Performance Graphics — Symposium Papers*, ACM, Copenhagen, June 2025.
>
> Paper: <https://diglib.eg.org/bitstream/handle/10.2312/hpg20251168/hpg20251168.pdf>
> Sample: <https://github.com/bkuth/procedural-tree-generation>

```bibtex
@inproceedings{Kuth25RTT,
  title     = {Real-Time GPU Tree Generation},
  author    = {Bastian Kuth and Max Oberberger and Carsten Faber and Pirmin Pfeifer and
               Seyedmasih Tabaei and Dominik Baumeister and Quirin Meyer},
  booktitle = {High Performance Graphics - Symposium Papers},
  year      = {2025},
  publisher = {ACM},
  address   = {Copenhagen, Denmark},
  month     = jun
}
```

The `random` / noise utilities in `TreeMath.h` are ported from AMD's
[Work Graph Playground](https://github.com/GPUOpen-LibrariesAndSDKs/WorkGraphPlayground)
`tutorials/Common.h`, also MIT licensed.

The underlying tree model is Weber & Penn, *Creation and Rendering of Realistic Trees*
(SIGGRAPH 1995); the sample's section references point at that paper.

## License

MIT License

Copyright (c) 2025 Bastian Kuth

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Deviations from the original

The original renders trees every frame through D3D12 Work Graphs 1.1 mesh nodes, which
Godot has no equivalent for. This port evaluates the same model on the CPU and bakes an
`ArrayMesh`, so the view-dependent parts had to change:

| Original | This port |
|---|---|
| Camera-facing single-sided stem strips, opening angle per ring | Closed tubes; radial count halves per stem level; the trunk base ring is capped |
| Screen-space adaptive tessellation | Explicit `radial_segments` / `rings_per_segment` |
| Leaf silhouette via pixel-shader discard of quadratic Bezier | The same four Bezier arcs, tessellated into a triangle strip |
| Per-pixel leaf vein normal bump (`Strand*` parameters) | A per-vertex blade cup, `leaf.curl`. The `Strand*` parameters have no baked equivalent and are not exposed |
| Needle fascicle = 4 blades × ~20 shader-carved needles | `leaf.needle_blades` tapered spikes (default 4) |
| Baked 130-vertex fruit sphere | Lat-long sphere at `fruit_longitudes` × `fruit_bands` |
| Bark cracks / lichen / snow in the pixel shader | Evaluated per vertex, baked into vertex colours and radius |
| Leaf density from camera distance | `leaf_density` knob, auto-clamped to `max_leaves` |
| Wind animated from frame time | `wind_strength` / `wind_time` as a static pose |

### Parameters added beyond the paper

`leaf.curl`, `leaf.color_jitter`, `leaf.scale_jitter`, `leaf.needle_blades` and
`leaf.evergreen`. The first four compensate for detail the original produced per-pixel and a
baked mesh cannot; `evergreen` lets a broadleaf keep its foliage through winter, which the
sample had no species for.

### Remaining fidelity gap

Foliage density. A mesh leaf costs ~16 triangles, so a 12 000-leaf budget is roughly 200 000
triangles and species that should read as dense — camphor, willow — still look airy. Closing
that gap means rendering leaves as alpha cards against a procedurally rasterised leaf
texture: ~2 triangles per leaf, which buys 8× the leaf count at the same cost and restores
the `Strand*` vein pattern the original had.

Seeded output matches the original's random stream: `TreeMath.h` reproduces the
playground's integer hash and Perlin gradient bit-for-bit, and z is quantised to the same
15 bits, so a given seed places branches where the paper's sample would.
