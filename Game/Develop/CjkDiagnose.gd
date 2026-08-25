extends Node2D

# Diagnoses Chinese text coming out of C++ through Godot's String, separating the two failure
# modes that look similar but have different fixes:
#
#   mojibake      -> the compiler encoded the literal in the wrong charset; character COUNT is wrong
#   tofu (boxes)  -> encoding is fine but the font has no CJK glyphs; count is right, render is not
#
# Run: godot --path Game/ res://Develop/CjkDiagnose.tscn

func _ready() -> void:
	var expected := {
		1: "Gable and Hip (歇山)",
		2: "Hip (庑殿)",
		8: "Helmet (盔顶)",
	}

	var encoding_ok := true
	for roof_type in expected:
		var got: String = AncientBuildingParameters.get_roof_type_name_localized(roof_type)
		var want: String = expected[roof_type]
		var same: bool = got == want
		if not same:
			encoding_ok = false
		print("VERIFY roof %d: got=%s len=%d  want=%s len=%d  %s" % [
			roof_type, got, got.length(), want, want.length(), "PASS" if same else "FAIL"])

	print("ENCODING: %s" % ("PASS - C++ literals survive as correct Unicode"
		if encoding_ok else "FAIL - charset mismatch between compiler and Godot"))

	# Now the glyph question. Render the text with the default font and count non-background
	# pixels: tofu boxes still draw pixels, so instead compare against a known-good ASCII
	# baseline and against an intentionally unrenderable private-use codepoint.
	var probe := Label.new()
	probe.text = AncientBuildingParameters.get_roof_type_name_localized(1)
	probe.position = Vector2(20, 20)
	add_child(probe)

	var ascii_probe := Label.new()
	ascii_probe.text = "Gable and Hip"
	ascii_probe.position = Vector2(20, 60)
	add_child(ascii_probe)

	# U+E000 is private use: no real font has it, so whatever this draws is what "missing" looks like.
	var missing_probe := Label.new()
	missing_probe.text = ""
	missing_probe.position = Vector2(20, 100)
	add_child(missing_probe)

	for i in 6:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw

	var image := get_viewport().get_texture().get_image()
	image.save_png(ProjectSettings.globalize_path("res://Develop/CjkDiagnose.png"))

	# Width of the rendered CJK run tells us a lot: a font with real glyphs lays CJK out about
	# twice as wide per character as it lays out a tofu box for a missing one.
	print("CJK label size:   %s" % probe.get_minimum_size())
	print("ASCII label size: %s" % ascii_probe.get_minimum_size())
	print("Missing-glyph size: %s" % missing_probe.get_minimum_size())

	# Ask the text server directly, which is the authoritative answer on glyph coverage.
	var font: Font = probe.get_theme_font("font")
	print("Default font: %s" % font.get_font_name())
	# Write findings to a file: the console mangles UTF-8 on a CP936 terminal.
	var report := "Default font: %s
" % font.get_font_name()
	for ch in ["歇", "山", "殿", "A"]:
		report += "  has_char U+%04X (%s): %s
" % [ch.unicode_at(0), ch, font.has_char(ch.unicode_at(0))]
	report += "fallbacks: %d
" % font.get_fallbacks().size()
	report += "CJK size %s / ASCII size %s
" % [probe.get_minimum_size(), ascii_probe.get_minimum_size()]
	report += "ENCODING: %s
" % ("PASS" if encoding_ok else "FAIL")
	# Godot's official per-control answer: SystemFont resolves an OS-installed font by name,
	# with allow_system_fallback picking one that covers the script. No bundled asset needed.
	var sysfont := SystemFont.new()
	sysfont.font_names = PackedStringArray(["Microsoft YaHei", "Noto Sans CJK SC", "SimHei", "sans-serif"])
	sysfont.allow_system_fallback = true
	report += "SystemFont resolved: %s
" % sysfont.get_font_name()
	for ch in ["歇", "山", "殿"]:
		report += "  SystemFont has_char U+%04X: %s
" % [ch.unicode_at(0), sysfont.has_char(ch.unicode_at(0))]

	# And what the plain default does with allow_system_fallback, for comparison.
	var probe2 := Label.new()
	probe2.text = "歇山 庑殿 攒尖"
	probe2.add_theme_font_override("font", sysfont)
	probe2.position = Vector2(20, 140)
	add_child(probe2)
	for i in 4:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(
		ProjectSettings.globalize_path("res://Develop/CjkDiagnose.png"))
	report += "SystemFont label size: %s
" % probe2.get_minimum_size()

	FileAccess.open("res://Develop/CjkReport.txt", FileAccess.WRITE).store_string(report)

	get_tree().quit()
