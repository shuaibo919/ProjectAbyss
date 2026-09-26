# 单体 Mesh 质量：柱身与柱础增量

日期：2026-09-26。范围：v2 P2 的柱面法线、柱础轮廓和柱构件稳定色差；不代表 P2 全部完成。

## 实际改动

- 柱侧面使用圆台解析法线，角向平滑；两端封盖使用独立硬法线。环向接缝位置、法线一致，UV 单独断开。圆周和高度 UV 使用世界尺寸。
- 新增回转剖面柱础，包含足部倒角、鼓身、折肩和承柱颈部；环向平滑，剖面折角保留硬边。属于通用工程样板，没有宣称复原某个朝代的特定柱础制度。
- 柱础替换柱身底部，不抬高柱顶；接合处柱径沿用原收分曲线。高度限制为柱高的 15%，负值按关闭处理。
- 矩形开间和多边形主体共用生成入口。柱身及柱础使用独立构件 ID 控制颜色；细分数、柱础开关和无关围栏不再改变这些构件的色差。此保证仅覆盖本次柱构件，不覆盖全建筑。
- `smooth_columns` 默认开启；`column_base_height_scale` 默认 0，避免自动扩大既有建筑柱脚占地。`column_sides` 仍独立控制轮廓精度。

## 使用

在 AncientBuilding 的 parameters 资源中，近景样板设置：

```gdscript
parameters.smooth_columns = true
parameters.column_sides = 24
parameters.column_base_height_scale = 0.65
```

柱础高度以现有模数 D 为单位，不是米。关闭柱础使用 0；关闭平滑法线使用 `smooth_columns = false`。旧模式对照用于观察棱面受光，不承诺与旧版本整个网格及配色逐顶点一致。

固定侧光对照场景：

```powershell
./Engine/bin/godot.windows.editor.x86_64.console.exe --path Game --log-file D:/VibeSpace/ProjectAbyss/Reference/Shots/ancient_building_quality.log --script res://Develop/AncientBuildingQuality.gd --resolution 1280x720 -- --capture
```

移除 `-- --capture` 可保留对照窗口。截图位于 `Reference/Shots/AncientBuildingQuality/`；柱脚近景逐栋独立显示，避免邻栋投影干扰。整体图左侧为 10 边平面法线、无柱础，右侧为 24 边解析法线、有柱础。

## 验证与成本

```powershell
./.venv/Scripts/python.exe -m SCons platform=windows target=template_debug arch=x86_64 -j8
./Engine/bin/godot.windows.editor.x86_64.console.exe --headless --path Game --log-file D:/VibeSpace/ProjectAbyss/Reference/Shots/ancient_building_columns_test.log --script res://tests/ancient_building_columns_test.gd
```

回归覆盖柱顶与整栋包围盒、圆台解析法线、三角形绕序与法线一致、无退化柱三角形、柱构件色差稳定、极端柱础高度、九种屋顶及六边形主体、网格索引合法性。

歇山无墙无围栏样板实测：

| 设置 | 顶点 | 三角形 |
|---|---:|---:|
| 10 边、平面法线、无柱础 | 44,704 | 22,590 |
| 24 边、解析法线、柱础 0.65D | 48,604 | 26,710 |

上述增量同时包含提高柱边数、封盖和新增柱础，共增加 4,120 个三角形（18.2%）。D3D12 / RTX 4060 Laptop / 1280×720 完成截图检查；没有据此宣称 FPS 或城镇性能达标。

## 后续工作

屋面连续采样、檐下椽飞结构、瓦片搭接、材质槽、切线和其他构件的稳定 ID 尚未在此增量实现。下一轮应先锁定歇山的共同屋面与檐口断面，再补檐下结构，避免新增构件与现有折线屋面脱节。
