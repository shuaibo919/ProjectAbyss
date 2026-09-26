# 单体 Mesh 质量：屋面连续曲线增量

日期：2026-09-26。范围：v2 P1.2/P1.3 的屋面连续采样、望板平滑受光与按曲线参数的瓦片覆盖率；不代表 P1 全部完成。

## 实际改动

- 屋面剖面改为举架坡率的解析积分曲线：`x(t) = HalfSpan·(1-t)`，`y(t) = Rise·[a·t + (b-a)·t²/2] / [(a+b)/2]`，其中 `a = eave_rise_ratio`、`b = ridge_rise_ratio`。曲线精确经过旧折线全部节点（中点法则恒等式），端点与旧剖面逐点相同；`a+b = 0` 时退化回平面剖面，与旧规整退化一致。
- 新增自适应采样（`Source/AncientBuilding/RoofCurve.cpp`）：弦误差按 1/4、1/2、3/4 三点检查，`roof_chord_error`（默认 0.005m）与 `roof_max_segment`（默认 0.5m）双阈值，递归深度上限 8，超限触发一次性警告。采样密度不再随分瓦数跳变。
- 望板平滑受光：连续模式改走逐顶点解析法线（`AddQuadSmooth`），四个角各自取所在跨距的法线再按方位倾转；相邻板带在共用剖面节点处取样到同一个法线值，整面望板连续过渡。旧模式保持逐板带面法线（三角面片法线），是此前棱带感的来源。歇山檐坡、歇山收山层、庑殿坡、卷棚坡面/卷棚段、多边形主体全部接入。
- `tile_coverage` 语义升级：连续模式下按曲线跨距定义（从正脊向下保留到 `HalfSpan·coverage`），并在覆盖边界插入曲线上的精确插值点；采样密度不再移动瓦片/裸望板的边界。旧模式保留节点计数语义，旧资源打开即走旧语义（`roof_curve_mode` 缺省为 Legacy）。
- 新增参数：`roof_curve_mode`（Legacy/Continuous 枚举）、`roof_chord_error`、`roof_max_segment`，以及静态方法 `sample_roof_curve` 供测试与工具直接取样。

### 已知模式差异（有意为之，非回归）

- 盝顶平顶放在收山处的曲线上；旧模式沿弦插值，顶面最高约高 8mm。
- 庑殿/歇山戗脊扫掠的平面极值点随采样节点略有位移（实测角部 0.77–2.76mm）。
- 旧模式是逐三角面片法线，且柱体、望板等构件的法线参数仅用于绕序——此前"解析法线"并未真正进入网格；本增量是第一次让望板法线真正进入网格。因此不能宣称与旧网格像素级一致。

## 使用

```gdscript
parameters.roof_curve_mode = 1        # Continuous；0/缺省为 Legacy
parameters.roof_chord_error = 0.005   # 弦误差（米）
parameters.roof_max_segment = 0.5     # 最大板带（米）
```

旧资源不需要迁移：`roof_curve_mode` 缺省为 Legacy，输出与旧版本逐字节一致（回归锚点 44,704 顶点 / 22,590 三角形）。

固定侧光对照场景：

```powershell
./Engine/bin/godot.windows.editor.x86_64.console.exe --path Game --log-file D:/VibeSpace/ProjectAbyss/Reference/Shots/roof_capture.log --script res://Develop/AncientBuildingRoofQuality.gd --resolution 1280x720 -- --capture
```

截图位于 `Reference/Shots/AncientBuildingQuality/`：`roof_curve_comparison.png`（左旧右新）、`roof_curve_legacy/continuous.png`（檐口近景）、`roof_curve_tier_legacy/continuous.png`（收山层正面特写）。北向掠射主光 + 无墙无柱 + `tile_coverage = 0`，让裸望板的板带受光差异直接可见。

## 验证与成本

```powershell
./.venv/Scripts/python.exe -m SCons platform=windows target=template_debug arch=x86_64 -j8
./Engine/bin/godot.windows.editor.x86_64.console.exe --headless --path Game --log-file D:/VibeSpace/ProjectAbyss/Reference/Shots/roof_curve_test.log --script res://tests/ancient_building_roof_curve_test.gd
```

回归覆盖：采样器保真（端点 ≤1e-4、弦误差、单调性、解析曲线贴合、旧节点恒等、退化坡）、旧模式逐字节锚点、九种屋顶两种模式的包围盒/索引/单位法线/确定性、覆盖率极值，以及**望板平滑法线契约**：收山层板带边界处，旧模式每个剖面节点携带两个法线（相邻板带面法线），连续模式携带一个（解析法线）。

歇山无墙无围栏样板实测（`tile_coverage = 0`）：

| 设置 | 顶点 | 三角形 |
|---|---:|---:|
| Legacy | 21,752 | 10,230 |
| Continuous | 36,188 | 17,798 |

连续模式平滑法线走 4 顶点/板带发射（旧面片发射为 6 顶点/板带），故顶点数不可直接比较；三角形增长 74% 全部来自采样密度。九种屋顶均无退化、无非法索引。D3D12 / RTX 4060 Laptop / 1280×720 完成截图检查；没有据此宣称 FPS 或城镇性能达标。

渲染对照的定量观察：收山层特写中，旧模式出现整条板带的等亮度平台（例如行 404–464 约 152.6 恒定，即一个板带一个面法线），连续模式同一区域为连续渐变（150.8→149.5）；檐口近景在掠射光下两模式接近，因为旧模式逐板带法线本就粗略跟随曲线，差异集中在上层收山层与板带数量。

## 后续工作

檐下椽飞结构（椽头→飞椽→连檐板→柱头斗拱剪影）、瓦片搭接、四类材质槽与稳定构件 ID 尚未在此增量实现。屋面曲线与采样是这些增量的共同基础：瓦片搭接可沿曲线参数定位，檐口断面可挂在曲线的檐口端点与切线上。下一轮应先锁定歇山的共同屋面与檐口断面，再补檐下结构。
