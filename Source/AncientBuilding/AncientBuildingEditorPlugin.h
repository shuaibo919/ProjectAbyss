#pragma once

// Editor dock for AncientBuilding, following the same shape as ProceduralTreeEditorPlugin:
// registered from C++ at MODULE_INITIALIZATION_LEVEL_EDITOR, so there is no plugin.cfg and
// nothing under addons/ to enable.
//
// The inspector already exposes every parameter. This dock exists for the handful of knobs
// worth scrubbing while looking at the result — roof type, plan, and the proportions that
// Table 1 derives everything else from — plus a live readout of those derived values, since
// the whole point of the module system is that one number drives the rest.

#include <godot_cpp/classes/box_container.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/h_slider.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/spin_box.hpp>
#include <godot_cpp/classes/v_box_container.hpp>

namespace godot
{
	class AncientBuilding;

	class AncientBuildingEditorPlugin : public EditorPlugin
	{
		GDCLASS(AncientBuildingEditorPlugin, EditorPlugin)

	private:
		VBoxContainer* Panel = nullptr;
		Label* SelectionLabel = nullptr;
		Label* DerivedLabel = nullptr;
		Label* StatsLabel = nullptr;

		OptionButton* EditRoofPicker = nullptr;
		SpinBox* WidthSpin = nullptr;
		SpinBox* DepthSpin = nullptr;
		SpinBox* BaysXSpin = nullptr;
		SpinBox* BaysZSpin = nullptr;
		SpinBox* EditSidesSpin = nullptr;
		HSlider* CornerRiseSlider = nullptr;
		HSlider* TileCoverageSlider = nullptr;
		HSlider* RoofHeightSlider = nullptr;
		SpinBox* RafterSpin = nullptr;
		CheckBox* FenceCheck = nullptr;
		CheckBox* StepsCheck = nullptr;
		CheckBox* WallsCheck = nullptr;

		/** The building being edited, or null. Only touched through ResolveBuilding(). */
		ObjectID EditedBuildingId;

		/** True while the panel pushes values into widgets, to suppress feedback loops. */
		bool bSyncingWidgets = false;

		/** Whether the panel is currently shown. */
		bool bPanelVisible = false;

		void SetPanelVisible(bool bVisible);

		AncientBuilding* ResolveBuilding() const;

		void BuildPanel();
		void SyncFromBuilding();

		/** Populates a picker with the nine roof types in taxonomy order. */
		void FillRoofPicker(OptionButton* Picker) const;

		/** Roof type behind the selected index of a picker. */
		int32_t RoofTypeAt(const OptionButton* Picker) const;

		Label* AddCaption(VBoxContainer* Parent, const String& Text) const;
		SpinBox* AddSpinRow(VBoxContainer* Parent, const String& Text, double Min, double Max, double Step);
		HSlider* AddSliderRow(VBoxContainer* Parent, const String& Text, double Min, double Max, double Step);

		void OnEditRoofChanged(int32_t Index);
		void OnWidthChanged(double Value);
		void OnDepthChanged(double Value);
		void OnBaysXChanged(double Value);
		void OnBaysZChanged(double Value);
		void OnSidesChanged(double Value);
		void OnCornerRiseChanged(double Value);
		void OnTileCoverageChanged(double Value);
		void OnRoofHeightChanged(double Value);
		void OnRafterChanged(double Value);
		void OnFenceToggled(bool bPressed);
		void OnStepsToggled(bool bPressed);
		void OnWallsToggled(bool bPressed);
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
