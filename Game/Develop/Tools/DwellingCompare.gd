extends SceneTree

# =============================================================================
# 民居样板「参考图 ⟷ 渲染图」并排合成工具（05 契约 §4.6 收件模板的配图）
#
# 左边 = 图鉴参考图（Reference/MD/tujian/...），右边 = 本项目渲染图
# （Reference/Shots/AncientBuildingQuality/dwelling/*.png），**按等高缩放**后拼成一张，
# 写到 Reference/Shots/AncientBuildingQuality/dwelling/compare_<name>.png。
#
# 纯 GDScript，只用 Image.load_from_file / Image.resize / Image.blit_rect / Image.save_png，
# 不依赖任何外部库；可以重复执行，同一输入必得同一输出。合成本身不需要 GPU，
# 加不加 --headless 都能跑（这条和拍图脚本相反 —— 那个才必须窗口模式）。
#
# -----------------------------------------------------------------------------
# 命令行（`--` 之后是 user args）：
#
#   1) 批量：把 PAIRS 表里所有条目按当前 tag 合一遍（最常用）
#   ./Engine/bin/godot.windows.editor.x86_64.console.exe --path Game --headless ^
#     --script res://Develop/Tools/DwellingCompare.gd -- --all --tag=after
#
#   2) 单个具名条目（名字取 PAIRS 的键）
#   ./Engine/bin/godot.windows.editor.x86_64.console.exe --path Game --headless ^
#     --script res://Develop/Tools/DwellingCompare.gd -- --name=dwelling_flush --tag=after
#
#   3) 任意两张图（绝对路径；--out 是相对 dwelling 目录的文件名，给绝对路径也行）
#   ./Engine/bin/godot.windows.editor.x86_64.console.exe --path Game --headless ^
#     --script res://Develop/Tools/DwellingCompare.gd -- --ref=D:/ref.jpeg --shot=D:/shot.png --out=custom.png
#
# 其他可选参数：
#   --tag=NAME       渲染图文件名里 before/after 那一段（默认 after）
#   --height=N       统一缩放到的高度（默认 = 渲染图自身高度，参考图跟着缩到同高）
#   --gutter=N       左/右之间分隔条宽度，像素（默认 8）
#   --show-missing   把缺图的条目也打印出来（默认也打印，只是标 MISSING 且不中断）
#
# 退出码：全部成功 0；有任一条目失败 1（批量模式下也会把其余条目跑完）。
# =============================================================================

const SHOT_DIR := "D:/VibeSpace/ProjectAbyss/Reference/Shots/AncientBuildingQuality/dwelling"
const REF_DIR := "D:/VibeSpace/ProjectAbyss/Reference/MD/tujian/dokumen.pub_9787121450198"
const REF_DIR_FALLBACK := "D:/VibeSpace/ProjectAbyss/Reference/MD/tujian/dokumen.pub_7121450198"

# name -> [左=参考图文件名, 右=渲染图文件名（不含 _<tag>.png）]
# 参考图与判据出处：05_建筑描述契约 §4.7、20_墙体 §1、40_山花 §1、60_台基地面 §1。
const PAIRS := {
	# 民居全景（硬山）：墙上抹灰+下碱带、台基顶面接缝、沿口、踏步、铺地、方形柱础
	"dwelling_flush": ["_page_245_Picture_5.jpeg", "M4_flush"],
	# 民居卷棚全景：同一套墙体/台基/踏步/柱础，屋顶为滚脊（无正脊）
	"dwelling_rolled": ["_page_246_Picture_3.jpeg", "M4_rolled"],
	# 悬山山面：悬鱼（窄长竖板 + 侧缺口 + 下端尖 V）第二安装位
	"xuanyu": ["_page_210_Picture_2.jpeg", "M3_ridge_overhang"],
	# 惹草（卷草浮雕件 + 中央圆形开光）—— 用 M4c 微距，M4b 的 9.7 m 下它只有 ~25 px
	"recao": ["_page_216_Picture_3.jpeg", "M4c_overhang"],
	# 博风板构造（四步工序图：下缘线脚 + 交汇处榫接 + 板料厚度）—— 交汇处在脊心，用整块山面
	"bofeng_joinery": ["_page_203_Picture_1.jpeg", "M4b_overhang"],
	# 博风板末端卷草浅浮雕 + 下缘线脚 —— 用 M4c 微距看板缘
	"bofeng_tip": ["_page_207_Picture_2.jpeg", "M4c_overhang"],
}

const DEFAULT_GUTTER := 8
const GUTTER_COLOR := Color(0.06, 0.07, 0.09, 1.0)

var args := {}
var failures := 0


func _initialize() -> void:
	args = _parse_args()
	var tag := _arg_str("tag", "after")
	var gutter := int(_arg_float("gutter", float(DEFAULT_GUTTER)))
	var height := int(_arg_float("height", 0.0))
	var out_dir := _out_dir()

	if "--help" in OS.get_cmdline_user_args() or args.has("help"):
		_print_help()
		quit(0)
		return

	if args.has("all"):
		for name in PAIRS:
			var entry: Array = PAIRS[name]
			_compose(_ref_path(entry[0]),
				SHOT_DIR.path_join("%s_%s.png" % [entry[1], tag]),
				out_dir.path_join("compare_%s.png" % name), gutter, height)
	elif args.has("name"):
		var name := _arg_str("name", "")
		if not PAIRS.has(name):
			push_error("DwellingCompare: 未知条目 '%s'；可用：%s" % [name, ", ".join(PAIRS.keys())])
			quit(1)
			return
		var entry: Array = PAIRS[name]
		_compose(_ref_path(entry[0]),
			SHOT_DIR.path_join("%s_%s.png" % [entry[1], tag]),
			out_dir.path_join("compare_%s.png" % name), gutter, height)
	elif args.has("ref") and args.has("shot"):
		var out_name := _arg_str("out", "compare_custom.png")
		if not out_name.ends_with(".png"):
			out_name += ".png"
		var out_path := out_name if out_name.is_absolute_path() else out_dir.path_join(out_name)
		_compose(_arg_str("ref", ""), _arg_str("shot", ""), out_path, gutter, height)
	else:
		_print_help()
		quit(1)
		return

	if failures > 0:
		print("DwellingCompare: %d 个条目失败" % failures)
	quit(1 if failures > 0 else 0)


func _out_dir() -> String:
	# 合成图与渲染图同目录：compare_<name>.png 直接落在 dwelling\ 下（任务约定）。
	return SHOT_DIR


func _print_help() -> void:
	print("DwellingCompare —— 参考图 ⟷ 渲染图 并排合成")
	print("  --all                 合成 PAIRS 全部条目 -> compare_<name>.png")
	print("  --name=<条目>         合成单个具名条目（%s）" % ", ".join(PAIRS.keys()))
	print("  --ref=<绝对路径> --shot=<绝对路径> [--out=NAME.png]   任意两张")
	print("  --tag=NAME            渲染图后缀（默认 after）")
	print("  --height=N            统一高度（默认跟随渲染图）")
	print("  --gutter=N            分隔条宽度像素（默认 %d）" % DEFAULT_GUTTER)


func _parse_args() -> Dictionary:
	var out := {}
	for arg in OS.get_cmdline_user_args():
		if not arg.begins_with("--"):
			continue
		var body := arg.substr(2)
		var eq := body.find("=")
		if eq == -1:
			out[body] = "1"
		else:
			out[body.substr(0, eq)] = body.substr(eq + 1)
	return out


func _arg_str(key: String, fallback: String) -> String:
	return str(args[key]) if args.has(key) else fallback


func _arg_float(key: String, fallback: float) -> float:
	return float(str(args[key])) if args.has(key) else fallback


func _ref_path(filename: String) -> String:
	# 图鉴目录在契约里登记为 dokumen.pub_9787121450198；早先的脚本用过不带 978 的前缀，
	# 两个都试一下，谁存在用谁。
	var primary := REF_DIR.path_join(filename)
	if FileAccess.file_exists(primary):
		return primary
	return REF_DIR_FALLBACK.path_join(filename)


func _compose(ref_path: String, shot_path: String, out_path: String, gutter: int, forced_h: int) -> bool:
	var ref := Image.load_from_file(ref_path)
	var shot := Image.load_from_file(shot_path)
	if ref == null or ref.is_empty():
		print("MISSING ref  %s" % ref_path)
		failures += 1
		return false
	if shot == null or shot.is_empty():
		print("MISSING shot %s" % shot_path)
		failures += 1
		return false
	ref.convert(Image.FORMAT_RGBA8)
	shot.convert(Image.FORMAT_RGBA8)

	var height := forced_h if forced_h > 0 else shot.get_height()
	var ref_w := maxi(1, int(round(float(ref.get_width()) * float(height) / float(ref.get_height()))))
	var shot_w := maxi(1, int(round(float(shot.get_width()) * float(height) / float(shot.get_height()))))
	if ref.get_height() != height or ref.get_width() != ref_w:
		ref.resize(ref_w, height, Image.INTERPOLATE_LANCZOS)
	if shot.get_height() != height or shot.get_width() != shot_w:
		shot.resize(shot_w, height, Image.INTERPOLATE_LANCZOS)

	var canvas := Image.create_empty(ref_w + gutter + shot_w, height, false, Image.FORMAT_RGBA8)
	canvas.fill(GUTTER_COLOR)
	canvas.blit_rect(ref, Rect2i(0, 0, ref_w, height), Vector2i(0, 0))
	canvas.blit_rect(shot, Rect2i(0, 0, shot_w, height), Vector2i(ref_w + gutter, 0))

	DirAccess.make_dir_recursive_absolute(out_path.get_base_dir())
	var err := canvas.save_png(out_path)
	print("compare=%s  [left ref %dx%d | right shot %dx%d] -> %dx%d  result=%d" %
		[out_path, ref_w, height, shot_w, height, canvas.get_width(), height, err])
	if err != OK:
		failures += 1
		return false
	return true
