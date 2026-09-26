# 单体 Mesh 质量：檐下椽飞结构增量

日期：2026-09-26。范围：v2 P4 的檐下椽飞（椽头→飞椽→连檐板），以一份**共同檐口断面**挂在所有带檐口的屋顶上；柱头斗拱剪影同属 P4 编码单元，留作下一轮。

## 实际改动

- 新增参数 `eave_rafter_style`（枚举 None / Rafter Heads / Rafters + Flying Rafters，默认 2），经 `CollectSpec` 进入 `BuildingSpec.EaveRafterStyle`。
- 新增 `BuildingGen::AddEaveRafterHeads`（`BuildingBuilder.cpp`）：所有檐口共用一份断面——每根椽头一个两节点扫掠，8 点台阶闭合轮廓：下宽台 = 檐椽头（宽 0.30D、深 0.26D），上窄台 = 飞椽头（宽 0.21D、高 0.18D）；style 1 退化为 4 点方断面。长度 1.8D、间距 0.7D（与多边形檐环采样同距），颜色 `TimberColor × 1.28`。
- 断面**垂直于椽轴**（真实檐椽头端面即平切于椽身），故轮廓上沿随檐口坡度外倾 H·sinθ；节点 Y 取 `SoffitY − H·cosθ`，使断面顶缘在所有坡度下精确贴合望板底板（SoffitY = RoofBase − 板厚·cosθ），整排藏在连檐之后（连檐内收 0.26D、下沉 0.20D，读作椽头前面的连檐板）。
- 每节点各自过 `CornerFlip::Apply`（外节点权重高于内节点），翼角起翘把角部椽头连同檐口一起抬起。硬山/悬山、卷棚双檐、庑殿/歇山/盝顶檐环（环采样中点，角部自然呈扇形发散，接缝由戗脊覆盖）、攒尖/圆攒尖/盔顶多边形檐全部接入同一断面。歇山收山层下缘落在檐环上、没有敞开的檐口，正确地不生椽头。
- style 0 什么都不发（早退），且屋顶在整个建筑的生成序里最后构建——因此旧资源输出逐字节不变，P2 回归锚点继续成立。

### 已知模式差异（有意为之，非回归）

- 椽头端面平切于椽轴，其上外侧角越过檐口线 H·sinθ（常规屋顶约 0.13m，陡峭的多边形檐口最大约 0.25m），故 style 2 相对 style 0 的包围盒 X/Z 可能各向外增大最多 0.30m；Y 完全不动。这不是锚点漂移，是平切端面的几何后果。

## 使用

```gdscript
parameters.eave_rafter_style = 2   # 2/缺省：檐椽头 + 飞椽；1：仅檐椽头；0：关闭
```

旧资源不需要迁移：`eave_rafter_style = 0` 时输出与旧版本逐字节一致（回归锚点 44,704 顶点 / 22,590 三角形）。

固定侧光对照场景：

```powershell
./Engine/bin/godot.windows.editor.x86_64.console.exe --path Game --script res://Develop/AncientBuildingEaveRafters.gd --resolution 1280x720 -- --capture
```

截图位于 `Reference/Shots/AncientBuildingQuality/`：`eave_rafter_underside_style0/style2.png`（檐下仰视，左关右开）、`eave_rafter_closeup_style0/style2.png`（连檐居中特写）、`eave_rafter_corner_style0/style2.png`（翼角，起翘的角檐带着椽头）。注意：本场景**不要加 `--headless`**——headless 下 `frame_post_draw` 不触发，截图流程会挂起。

## 验证与成本

```powershell
uv run --python 3.11 --with scons python run_scons.py platform=windows target=template_debug arch=x86_64 -j8
./Engine/bin/godot.windows.editor.x86_64.console.exe --headless --path Game --script res://tests/ancient_building_eave_rafter_test.gd
./Engine/bin/godot.windows.editor.x86_64.console.exe --headless --path Game --script res://tests/ancient_building_roof_curve_test.gd
```

回归覆盖（`ancient_building_eave_rafter_test.gd`）：九种屋顶 × style 0/1/2 的顶点/三角形严格递增（2>1>0）、包围盒 Y 不变且 X/Z 增长受平切端面外倾上限约束、style 2 有限值/单位法线/存在低于底板高度的椽头顶点、歇山 style 2 计数锚点（53,586 顶点 / 27,230 三角形）与双重烘焙确定性。原屋顶曲线套件（含旧模式逐字节锚点）继续全绿。

歇山无围栏样板（`generate_fence = false`，Legacy 曲线模式）：

| 檐下椽飞 | 顶点 | 三角形 |
|---|---:|---:|
| None (0) | 48,936 | 24,254 |
| Rafter Heads (1) | 51,354 | 25,742 |
| Rafters + Flying Rafters (2) | 53,586 | 27,230 |

每根椽头 = 一个两节点扫掠（8 点断面），增量为 4,650 顶点 / 2,976 三角形（约 9.5%）。像素差验证：style 0/2 同机位截图在檐口轮廓带（上沿锯齿 = 椽头与连檐探出檐口线）、檐下椽头行（沿檐方向 ~80px 周期的高低交替 = 0.7D 间距的椽头序列）、翼角区域集中出现差异，其余区域逐像素相同。D3D12 / RTX 4060 Laptop / 1280×720 完成截图检查；没有据此宣称 FPS 或城镇性能达标。

## 后续工作

- **柱头斗拱剪影**（P4 编码单元剩余项，下一轮）：本增量锁定了共同檐口断面，斗拱柱头可以挂在同一檐口基准上，不再担心与折线屋面脱节。
- 瓦片搭接（沿曲线参数）、四类材质槽与稳定构件 ID。
- 城镇 PCG 评审项（共享网格键、lot_type 使用、布局方向等）排在其后。
