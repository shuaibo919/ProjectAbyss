"""Rewrites the five SlowTree species presets onto the HelloTree design skeleton.

See Source/TreeGen/SlowTree/AUTHORING.md for where every rule comes from. Done as one script
rather than five hand edits so the shared skeleton stays literally shared — the earlier hand pass
drifted and got taperPow, spreadAngle and gravity backwards on different presets.

Budget: the scatter tier, ~40-60k triangles. Twig tubes dominate cost (count x sides x lengthSegs),
not leaves, so the skeleton runs two Branch levels rather than HelloTree's three and keeps twig
tessellation low.
"""
import io
import os

# 目标文件定位到脚本旁(本脚本从任意 cwd 运行均可)。
PATH = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'SlowTreePresets.cpp'))


def trunk(length, start_r, end_r, seed, albedo, extra=''):
    return f"""NODE 1 0 100 200
length {length}
startRadius {start_r}
endRadius {end_r}
baseFlare 1.6
noiseAmount 85
noiseFreq 3.1
gnarl 14
taperPow 1.6
sides 8
lengthSegs 16
seed {seed}
uvTiling 3
{extra}mat.albedo {albedo}
mat.roughness 0.85
ENDNODE"""


def roots(length, seed, albedo):
    # radiusScale 0.34 is the load-bearing number: thicker reads as splayed blades, not roots.
    return f"""NODE 7 1 282 119
rootCount 6
length {length}
radiusScale 0.34
endRatio 0.08
taperPow 1.8
baseFlare 2.5
spreadAngle 90
droop 1
rotateOffset 140.9
noiseAmount 90
noiseFreq 1.7
gnarl 17
sides 6
lengthSegs 10
seed {seed}
mat.albedo {albedo}
mat.roughness 0.9
ENDNODE"""


def branch_l1(count, seed, albedo, spread=36, gravity=0.95, length_ratio=0.7, mode_lines='',
              taper=0.75, noise=90, noise_freq=1.5, gnarl=10, region_start=0.4,
              size_falloff=0.0, region_end=0.92, rotate=137.5):
    # taperPow < 1 = slender limb; gravity ~1 on this level only is what makes the crown arch.
    # Those are BROADLEAF values. Conifers and bamboo want straight, sturdy whorls instead:
    # low noise, low gnarl, taperPow > 1. Overriding here rather than forking the skeleton.
    #
    # sizeFalloff likewise: HelloTree uses none, and on a broadleaf it just shrinks the upper
    # crown. A conifer *needs* it — tiers that do not shorten upward give a ball, not a cone.
    falloff_line = 'sizeFalloff %s\n' % size_falloff if size_falloff else ''
    return f"""NODE 2 2 282 200
{mode_lines}lengthRatio {length_ratio}
radiusScale 0.68
endRatio 0.25
baseFlare 2.1
taperPow {taper}
spreadAngle {spread}
rotateOffset {rotate}
gravity {gravity}
regionStart {region_start}
regionEnd {region_end}
noiseAmount {noise}
noiseFreq {noise_freq}
gnarl {gnarl}
{falloff_line}branchCount {count}
sides 6
lengthSegs 6
seed {seed}
uvTiling 2
mat.albedo {albedo}
mat.roughness 0.87
ENDNODE"""


def spine(count, seed, albedo, node_id, x, length_ratio=0.6, radius_scale=0.4, spread=76,
          gravity=0.9, region_start=0.15, region_end=0.95, sides=4, segs=8, noise=10,
          rotate=137.5):
    return f"""NODE {node_id} 5 {x} 200
spineCount {count}
lengthRatio {length_ratio}
radiusScale {radius_scale}
endRatio 0.15
taperPow 1.2
spreadAngle {spread}
rotateOffset {rotate}
gravity {gravity}
regionStart {region_start}
regionEnd {region_end}
noiseAmount {noise}
noiseFreq 1.8
gnarl 3
sides {sides}
lengthSegs {segs}
seed {seed}
mat.albedo {albedo}
mat.roughness 0.7
ENDNODE"""


def frond(seed, albedo, node_id, x, width=0.115, width_base=0.3, width_tip=0.0,
          profile_pow=0.5, segs_per_side=1, serrate=1, serrate_depth=0.45):
    return f"""NODE {node_id} 6 {x} 200
width {width}
widthBase {width_base}
widthTip {width_tip}
profilePow {profile_pow}
curl 0
segsPerSide {segs_per_side}
serrate {serrate}
serrateDepth {serrate_depth}
seed {seed}
mat.albedo {albedo}
mat.roughness 0.75
mat.sssStrength 0.6
ENDNODE"""


def branch_l2(count, seed, albedo, spread=52, gravity=0.2, length_ratio=0.42,
              taper=1.5, noise=88, gnarl=10, size_falloff=0.0, radius_scale=0.42,
              sides=5, segs=6, rotate=137.5):
    falloff_line = 'sizeFalloff %s\n' % size_falloff if size_falloff else ''
    return f"""NODE 3 2 417 200
lengthRatio {length_ratio}
radiusScale {radius_scale}
endRatio 0.25
baseFlare 2.2
taperPow {taper}
spreadAngle {spread}
rotateOffset {rotate}
gravity {gravity}
regionStart 0.4
regionEnd 0.95
noiseAmount {noise}
noiseFreq 2.1
gnarl {gnarl}
{falloff_line}branchCount {count}
sides {sides}
lengthSegs {segs}
seed {seed}
uvTiling 2
mat.albedo {albedo}
mat.roughness 0.88
ENDNODE"""


def twig(count, seed, albedo, spread=66, gravity=0.26, length_ratio=0.58, node_id=4, x=554,
         noise=35, gnarl=8, taper=1.3, rotate=137.5):
    return f"""NODE {node_id} 3 {x} 200
twigCount {count}
lengthRatio {length_ratio}
radiusScale 1
endRatio 0.25
baseFlare 1.8
taperPow {taper}
spreadAngle {spread}
rotateOffset {rotate}
gravity {gravity}
regionStart 0.2
regionEnd 0.87
noiseAmount {noise}
noiseFreq 3.5
gnarl {gnarl}
sides 4
lengthSegs 4
alternating 1
seed {seed}
mat.albedo {albedo}
mat.roughness 0.9
ENDNODE"""


def leaves(count, size, aspect, seed, albedo, soften=0.4, planar=0, falloff=0.0, node_id=5, x=682):
    lines = [f'NODE {node_id} 4 {x} 200',
             f'leafCount {count}',
             'clusterRadius 0.05',
             f'leafSize {size}',
             f'leafAspect {aspect}',
             'normalJitter 0.26',
             f'normalSoften {soften}']
    if planar:
        lines.append('planar 1')
    if falloff:
        lines.append(f'sizeFalloff {falloff}')
    lines += [f'seed {seed}', f'mat.albedo {albedo}', 'mat.roughness 0.82',
              'mat.sssStrength 0.8', 'ENDNODE']
    return '\n'.join(lines)


BARK1, BARK2, BARK3 = '0.33 0.2 0.1', '0.32 0.19 0.1', '0.3 0.19 0.09'
ROOTC = '0.3 0.19 0.1'

# ---- 银杏: broad fan leaves, rounded crown, moderate arch ----
GINKGO = '\n'.join([
    'VEGTOOL 1',
    trunk(6.6, 0.3, 0.075, 31, '0.36 0.24 0.14'),
    branch_l1(11, 32, BARK1),
    branch_l2(8, 36, BARK2),
    twig(6, 33, BARK3),
    leaves(10, 0.20, 1.15, 34, '0.55 0.62 0.2', soften=0.65),
    roots(1.4, 35, ROOTC),
    'LINK 1 2', 'LINK 1 7', 'LINK 2 3', 'LINK 3 4', 'LINK 4 5',
])

# ---- 柳树: the weep lives on L1/L2 gravity, leaves are narrow and pinnate ----
WILLOW = '\n'.join([
    'VEGTOOL 1',
    trunk(6.2, 0.3, 0.07, 11, '0.3 0.22 0.14'),
    branch_l1(10, 12, BARK1, spread=52, gravity=1.9, length_ratio=0.78),
    branch_l2(7, 16, BARK2, spread=64, gravity=1.7, length_ratio=0.5),
    twig(6, 13, BARK3, spread=58, gravity=1.6, length_ratio=0.62),
    leaves(14, 0.22, 0.26, 14, '0.35 0.55 0.28', soften=0.55, planar=1, falloff=0.3),
    roots(1.5, 15, ROOTC),
    'LINK 1 2', 'LINK 1 7', 'LINK 2 3', 'LINK 3 4', 'LINK 4 5',
])

# ---- 松树 ----
# Structure follows the SpeedTree pine walkthrough archived at
# Reference/zhihu-scraper/知乎归档/SPEEDTREE学习笔记—松树简易制作流程（单轴分支）/:
#   §03 L1 branches near-horizontal ("更平"), only the tip bent down, and length long at the
#       bottom / short at the top via a curve — that curve is sizeFalloff, and it is what makes
#       the cone. Without it every tier is the same length and the crown is a ball.
#   §04 L2 = a copy of L1 aimed outward.
#   §05 needles are made from *branch* nodes, one branch per needle.
#   §06 but that blows up the count ("生成数量过度，会导致软件崩溃") so needles stop at L2.
#
# We keep §03/§04 and drop §05: a Spine+Frond spray costs ~60 tris and covers what ~15 needle
# tubes (~180 tris) would. Same silhouette, a third of the budget, and the serrate edge gives the
# needle comb. §06 is conceding the identical problem from the other side.
PINE = '\n'.join([
    'VEGTOOL 1',
    trunk(9.0, 0.24, 0.05, 21, '0.34 0.24 0.15'),
    # spreadAngle 62 不是 85: PlantBotany §2② 指出 85 是 Massart(云杉那种平展层次),
    # 真正的松(Rauh's model)枝是 orthotropic — 抬起、辐射对称。第一版把整枝拍平后
    # 轮生层像棍子插进树干, 现在抬起一些有松树的气韵。
    branch_l1(5, 22, BARK1, spread=62, gravity=0.30, length_ratio=0.34,
              mode_lines='mode 6\nintervalSpacing 0.1\nbranchesPerNode 5\n',
              taper=1.15, noise=20, noise_freq=1.2, gnarl=3, region_start=0.15,
              size_falloff=0.85, region_end=1.0),
    # L2 count 5: 第一版 4 根太稀, 轮生枝间褐色裸干刺眼(第二轮截图确认);
    # 叶簇跟随 L2 数量, 密了才盖得住。
    branch_l2(5, 23, BARK3, spread=45, gravity=0.25, length_ratio=0.45,
              taper=1.3, noise=18, gnarl=3, size_falloff=0.3, radius_scale=0.45,
              sides=4, segs=4),
    # spineCount 7 + lengthRatio 0.85: 第一版 5 根 x 1.1 倍长, 针叶带顺着整根枝拖成
    # 一条条稀疏长条(第二轮截图)。短叶轴 + 多叶簇 = 簇形聚焦在枝梢, 读作一簇簇松针。
    spine(7, 26, '0.13 0.3 0.12', node_id=4, x=554, length_ratio=0.85, radius_scale=0.12,
          spread=26, gravity=0.35, region_start=0.1, region_end=0.95, sides=3, segs=6, noise=14),
    # width 0.22 + profilePow 0.8: 叶带更宽更饱满(基部收窄、中部圆润),
    # 而不是一条等宽的细带 —— serrate 边缘给出针叶梳齿。
    frond(27, '0.11 0.3 0.13', node_id=5, x=682, width=0.22, width_base=0.85, width_tip=0.2,
          profile_pow=0.8, segs_per_side=2, serrate=1, serrate_depth=0.65),
    roots(1.6, 25, ROOTC),
    'LINK 1 2', 'LINK 1 7', 'LINK 2 3', 'LINK 3 4', 'LINK 4 5',
])

# ---- 竹子: McClure's model(PlantBotany §2③) - basitonic 分枝, 新竿从根状茎发出。
# 植物学定论: 竹**必须是一丛** —— 单竿是错的骨架, 实测读作"一把散草"。
# 三竿: 一主两副, posX/posZ 错开, 副竿更短更细更密。
# 每竿只有一层 Interval 轮生枝 + 一层 Twig + 叶; 竹竿直(noise 低), 枝位于竿上段(regionStart 0.45)。
def bamboo_culm(ids, length, start_r, end_r, n_seg, seed_trunk, seed_branch, seed_twig, seed_leaf,
                pos_lines=''):
    t, b, w, l = ids
    return '\n'.join([
        f'NODE {t} 0 100 200', f'length {length}', f'startRadius {start_r}',
        f'endRadius {end_r}', 'baseFlare 1.6', 'noiseAmount 14', 'noiseFreq 3.1', 'gnarl 3',
        'taperPow 1.6', 'sides 8', 'lengthSegs 16', f'seed {seed_trunk}', 'uvTiling 3',
        'jointCount 16', 'jointBulge 0.18', f'{pos_lines}mat.albedo 0.24 0.42 0.12',
        'mat.roughness 0.85', 'ENDNODE',
        f'NODE {b} 2 282 200', 'mode 6', f'intervalSpacing {n_seg}', 'branchesPerNode 2',
        'lengthRatio 0.35', 'radiusScale 0.68', 'endRatio 0.25', 'baseFlare 2.1',
        'taperPow 1.15', 'spreadAngle 58', 'rotateOffset 137.5', 'gravity 0.3',
        'regionStart 0.45', 'regionEnd 0.95', 'noiseAmount 14', 'noiseFreq 2.4', 'gnarl 3',
        'sides 6', 'lengthSegs 6', f'seed {seed_branch}', 'uvTiling 2',
        'mat.albedo 0.22 0.4 0.11', 'mat.roughness 0.87', 'ENDNODE',
        f'NODE {w} 3 417 200', 'twigCount 3', 'lengthRatio 0.45', 'radiusScale 1',
        'endRatio 0.25', 'baseFlare 1.8', 'taperPow 1.1', 'spreadAngle 38',
        'rotateOffset 137.5', 'gravity 0.4', 'regionStart 0.2', 'regionEnd 0.87',
        'noiseAmount 12', 'noiseFreq 3.5', 'gnarl 2', 'sides 4', 'lengthSegs 4',
        'alternating 1', f'seed {seed_twig}', 'mat.albedo 0.22 0.4 0.11',
        'mat.roughness 0.9', 'ENDNODE',
        f'NODE {l} 4 554 200', 'leafCount 9', 'clusterRadius 0.05', 'leafSize 0.17',
        'leafAspect 0.22', 'normalJitter 0.26', 'normalSoften 0.4', 'planar 1',
        'sizeFalloff 0.35', f'seed {seed_leaf}', 'mat.albedo 0.13 0.4 0.17',
        'mat.roughness 0.82', 'mat.sssStrength 0.8', 'ENDNODE',
    ])


BAMBOO = '\n'.join([
    'VEGTOOL 1',
    bamboo_culm((1, 2, 3, 4), 8.5, 0.15, 0.09, 0.1, 41, 42, 44, 43),
    bamboo_culm((5, 6, 8, 9), 7.4, 0.13, 0.08, 0.09, 51, 52, 54, 53,
                pos_lines='posX 0.45\nposZ 0.5\n'),
    bamboo_culm((10, 11, 12, 13), 6.6, 0.12, 0.07, 0.08, 61, 62, 64, 63,
                pos_lines='posX -0.5\nposZ 0.35\n'),
    roots(1.3, 45, '0.28 0.2 0.12'),
    'LINK 1 2', 'LINK 1 7', 'LINK 2 3', 'LINK 3 4',
    'LINK 5 6', 'LINK 6 8', 'LINK 8 9',
    'LINK 10 11', 'LINK 11 12', 'LINK 12 13',
])

# ---- 水杉: pinnate compound foliage via Spine -> Frond instead of leaf cards ----
METASEQUOIA = '\n'.join([
    'VEGTOOL 1',
    trunk(7.8, 0.32, 0.08, 51, '0.34 0.23 0.13'),
    branch_l1(10, 52, BARK1, spread=70, gravity=0.8, length_ratio=0.58, taper=1.1, noise=40, gnarl=6),
    branch_l2(6, 53, BARK2, spread=62, gravity=0.65, length_ratio=0.46),
    spine(8, 54, '0.22 0.42 0.1', node_id=4, x=554, length_ratio=0.6,
          radius_scale=0.4, spread=76, gravity=0.9, segs=8),
    frond(55, '0.15 0.4 0.18', node_id=5, x=682, width=0.115, width_base=0.3,
          profile_pow=0.5, serrate=1, serrate_depth=0.45),
    roots(1.6, 56, ROOTC),
    'LINK 1 2', 'LINK 1 7', 'LINK 2 3', 'LINK 3 4', 'LINK 4 5',
])

# ---- 桃 (Prunus persica) ----
# Wikipedia "Peach": typically 3-4 m tall with crown spread about equal to height (rarely 10 m);
# leaves oblong-lanceolate 7-15 x 2-4.5 cm; flowers 2-3.5 cm across, 4-5 petals, pink, borne
# SOLITARY OR IN PAIRS and opening BEFORE THE LEAVES; bark dark grey with horizontal lenticels,
# young twigs reddish; "branches tend to droop over time".
#
# Architecture: run Halle's key (Docs/PlantBotany_ForPCG.md section 2) - axes homogeneous and
# orthotropic, inflorescences LATERAL (axillary buds on one-year wood, so branches are monopodial),
# trunk growth rhythmic -> couplet 21a -> **Rauh's model**. The documented drooping is secondary
# gravity bending as in Champagnat's model, which is exactly SlowTree's `gravity`.
# Halle's book does not classify Prunus itself - it is a tropical-trees survey and mentions the
# genus only as a temperate bud-behaviour example.
#
# rotateOffset 144 = 2/5 phyllotaxis, the Prunus/apricot group (section 4 of the botany doc),
# not the 137.5 golden angle every other preset uses.
#
# Scale is the big departure: 4-ish metres against 9-11 for every other preset, so the trunk is
# short and thin and the branch lengthRatio is high to build the crown from the branches instead.
#
# Foliage is TWO LeafCluster nodes on the same twig, because SlowTree has no notion of a flower:
# a dense pink small-leafSize cluster stands in for blossom, plus a sparse green one for the young
# leaves just breaking. Botanically peach blossom opens on bare wood; a little leaf is the state
# right at the end of bloom, and it reads better than bare twigs. A true bare-blossom / full-leaf
# switch needs the season parameter SlowTree does not have.
PEACH = '\n'.join([
    'VEGTOOL 1',
    trunk(2.6, 0.13, 0.04, 61, '0.28 0.24 0.21'),
    branch_l1(7, 62, '0.30 0.25 0.21', spread=48, gravity=0.9, length_ratio=0.85,
              region_start=0.3, rotate=144),
    branch_l2(6, 63, '0.34 0.24 0.19', spread=58, gravity=0.55, length_ratio=0.5,
              radius_scale=0.45, rotate=144),
    twig(7, 64, '0.44 0.25 0.18', spread=66, gravity=0.4, length_ratio=0.6, rotate=144),
    # A single 2-3.5 cm flower is ~4 px on a 4.5 m tree and averages away to grey speckle, so each
    # card stands for a flower-plus-bud pair instead (AUTHORING section 17: wider and fewer beats
    # narrower and more). normalSoften high so the mass shades as a volume rather than half the
    # cards turning black when they face away.
    leaves(18, 0.09, 1.0, 65, '0.95 0.51 0.62', soften=0.85, node_id=5, x=682),
    leaves(2, 0.08, 0.32, 66, '0.45 0.58 0.26', soften=0.4, planar=1, node_id=6, x=810),
    roots(0.55, 67, '0.28 0.23 0.2'),
    'LINK 1 2', 'LINK 1 7', 'LINK 2 3', 'LINK 3 4', 'LINK 4 5', 'LINK 4 6',
])

PRESETS = {
    'kWillowVtree': WILLOW,
    'kPineVtree': PINE,
    'kGinkgoVtree': GINKGO,
    'kBambooVtree': BAMBOO,
    'kMetasequoiaVtree': METASEQUOIA,
    'kPeachVtree': PEACH,
}

src = io.open(PATH, encoding='utf-8').read()
for name, body in PRESETS.items():
    head = f'\tstatic const char* {name} = R"VT('
    start = src.index(head)
    end = src.index(')VT";', start) + len(')VT";')
    src = src[:start] + head + body + '\n)VT";' + src[end:]

io.open(PATH, 'w', encoding='utf-8', newline='').write(src)
print('rewrote', len(PRESETS), 'presets')
