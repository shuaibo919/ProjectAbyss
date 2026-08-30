#pragma once

// Editor dock for ProceduralRock, following the same shape as AncientBuildingEditorPlugin:
// registered from C++ at MODULE_INITIALIZATION_LEVEL_EDITOR, so there is no plugin.cfg and
// nothing under addons/ to enable.
//
// The inspector already exposes every parameter. This dock exists for the handful of knobs
// worth scrubbing while looking at the result — form, resolution, roughness — plus the
// vertex/triangle readout, because the one number users must feel is how much the
// resolution costs.

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
	class ProceduralRock;

	class ProceduralRockEditorPlugin : public EditorPlugin
	{
		GDCLASS(ProceduralRockEditorPlugin, EditorPlugin)

	private:
		VBoxContainer* Panel = nullptr;
		Label* SelectionLabel = nullptr;
		Label* StatsLabel = nullptr;

		OptionButton* EditFormPicker = nullptr;
		SpinBox* SeedSpin = nullptr;
		SpinBox* ResolutionSpin = nullptr;
		SpinBox* ScaleSpin = nullptr;
		SpinBox* StepsSpin = nullptr;
		HSlider* SmoothnessSlider = nullptr;
		HSlider* DisplacementSlider = nullptr;
		HSlider* SpreadSlider = nullptr;
		HSlider* FlatnessSlider = nullptr;
		HSlider* RoundnessSlider = nullptr;
		CheckBox* CutGroundCheck = nullptr;
		HSlider* GroundCutSlider = nullptr;
		ColorPickerButton* BaseColorPicker = nullptr;
		ColorPickerButton* CreviceColorPicker = nullptr;

		/** The rock being edited, or null. Only touched through ResolveRock(). */
		ObjectID EditedRockId;

		/** True while the panel pushes values into widgets, to suppress feedback loops. */
		bool bSyncingWidgets = false;

		/** Whether the panel is currently shown. */
		bool bPanelVisible = false;

		void SetPanelVisible(bool bVisible);

		ProceduralRock* ResolveRock() const;

		void BuildPanel();
		void SyncFromRock();

		Label* AddCaption(const String& Text) const;
		SpinBox* AddSpinRow(const String& Text, double Min, double Max, double Step);
		HSlider* AddSliderRow(const String& Text, double Min, double Max, double Step);

		void OnFormChanged(int32_t Index);
		void OnSeedChanged(double Value);
		void OnResolutionChanged(double Value);
		void OnScaleChanged(double Value);
		void OnStepsChanged(double Value);
		void OnSmoothnessChanged(double Value);
		void OnDisplacementChanged(double Value);
		void OnSpreadChanged(double Value);
		void OnFlatnessChanged(double Value);
		void OnRoundnessChanged(double Value);
		void OnCutGroundToggled(bool bPressed);
		void OnGroundCutChanged(double Value);
		void OnBaseColorChanged(const Color& Value);
		void OnCreviceColorChanged(const Color& Value);
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
