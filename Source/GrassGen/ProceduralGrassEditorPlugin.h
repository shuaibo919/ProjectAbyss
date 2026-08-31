#pragma once

// Editor dock for ProceduralGrass, following the same shape as ProceduralRockEditorPlugin:
// registered from C++ at MODULE_INITIALIZATION_LEVEL_EDITOR, so there is no plugin.cfg and
// nothing under addons/ to enable.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/color_picker_button.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/h_slider.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/spin_box.hpp>
#include <godot_cpp/classes/v_box_container.hpp>

namespace godot
{
	class ProceduralGrass;

	class ProceduralGrassEditorPlugin : public EditorPlugin
	{
		GDCLASS(ProceduralGrassEditorPlugin, EditorPlugin)

	private:
		VBoxContainer* Panel = nullptr;
		Label* SelectionLabel = nullptr;
		Label* StatsLabel = nullptr;

		OptionButton* EditSpeciesPicker = nullptr;
		SpinBox* SeedSpin = nullptr;
		SpinBox* ScaleSpin = nullptr;
		SpinBox* ClumpRadiusSpin = nullptr;
		SpinBox* BladeCountSpin = nullptr;
		HSlider* CurvatureSlider = nullptr;
		HSlider* LeanAngleSlider = nullptr;
		HSlider* LeanAzimuthSlider = nullptr;
		HSlider* ColorVarianceSlider = nullptr;
		CheckBox* UseSpeciesColorsCheck = nullptr;
		ColorPickerButton* BaseColorPicker = nullptr;
		ColorPickerButton* TipColorPicker = nullptr;

		/** The grass clump being edited, or null. Only touched through ResolveGrass(). */
		ObjectID EditedGrassId;

		/** True while the panel pushes values into widgets, to suppress feedback loops. */
		bool bSyncingWidgets = false;

		/** Whether the panel is currently shown. */
		bool bPanelVisible = false;

		void SetPanelVisible(bool bVisible);

		ProceduralGrass* ResolveGrass() const;

		void BuildPanel();
		void SyncFromGrass();

		Label* AddCaption(const String& Text) const;
		SpinBox* AddSpinRow(const String& Text, double Min, double Max, double Step);
		HSlider* AddSliderRow(const String& Text, double Min, double Max, double Step);

		void OnSpeciesChanged(int32_t Index);
		void OnSeedChanged(double Value);
		void OnScaleChanged(double Value);
		void OnClumpRadiusChanged(double Value);
		void OnBladeCountChanged(double Value);
		void OnCurvatureChanged(double Value);
		void OnLeanAngleChanged(double Value);
		void OnLeanAzimuthChanged(double Value);
		void OnColorVarianceChanged(double Value);
		void OnUseSpeciesColorsToggled(bool bPressed);
		void OnBaseColorChanged(const Color& Value);
		void OnTipColorChanged(const Color& Value);
		void OnRegeneratePressed();

	protected:
		static void _bind_methods();

	public:
		void _enter_tree() override;
		void _exit_tree() override;

		String _get_plugin_name() const override;
		bool _handles(Object* Target) const override;
		void _edit(Object* Target) override;
		void _make_visible(bool bVisible) override;
	};
} // namespace godot
