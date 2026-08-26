#pragma once

// Editor dock for ProceduralTree: spawn one of the paper's species into the open scene,
// then tune the few parameters worth scrubbing interactively. Everything else lives in
// the inspector, where the full Weber-Penn parameter set already is.
//
// Registered from C++ via EditorPlugins::add_by_type(), so it needs no plugin.cfg and no
// files under addons/ — consistent with the generator itself being asset-free.

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
	class ProceduralTree;

	class ProceduralTreeEditorPlugin : public EditorPlugin
	{
		GDCLASS(ProceduralTreeEditorPlugin, EditorPlugin)

	private:
		VBoxContainer* Panel = nullptr;
		OptionButton* PresetPicker = nullptr;
		VBoxContainer* SelectionBox = nullptr;
		Label* SelectionLabel = nullptr;
		Label* StatsLabel = nullptr;
		SpinBox* SeedSpin = nullptr;
		HSlider* SeasonSlider = nullptr;
		HSlider* WindSlider = nullptr;
		HSlider* DensitySlider = nullptr;
		SpinBox* RadialSpin = nullptr;
		CheckBox* BarkDetailCheck = nullptr;

		// 双后端控件: Backend 下拉 + SlowTree 预设下拉; Weber 专属控件分组按后端显隐。
		OptionButton* BackendPicker = nullptr;
		OptionButton* SlowTreePresetPicker = nullptr;
		Label* SlowTreePresetCaption = nullptr;
		CheckBox* GpuTessellationCheck = nullptr; // Stage 2: SlowTree 细分走 GPU compute(仅 SlowTree 后端可见)
		VBoxContainer* WeberSpeciesBox = nullptr;
		VBoxContainer* WeberTuningBox = nullptr;

		/** The tree currently being edited, or null. Only touched through ResolveTree(). */
		ObjectID EditedTreeId;

		/** True while the panel is pushing values into widgets, to suppress feedback loops. */
		bool bSyncingWidgets = false;

		/** Whether the panel is currently shown. */
		bool bPanelVisible = false;

		void SetPanelVisible(bool bVisible);

		ProceduralTree* ResolveTree() const;

		void BuildPanel();
		void SyncFromTree();

		/** Preset currently chosen in the dropdown. */
		int32_t GetSelectedPreset() const;

		void OnApplyToSelectedPressed();
		void OnRegeneratePressed();
		void OnRandomizeSeedPressed();
		void OnBackendChanged(int64_t Index);
		void OnSlowTreePresetChanged(int64_t Index);
		void OnGpuTessellationToggled(bool bPressed);
		void OnSeedChanged(double Value);
		void OnSeasonChanged(double Value);
		void OnWindChanged(double Value);
		void OnDensityChanged(double Value);
		void OnRadialChanged(double Value);
		void OnBarkDetailToggled(bool bPressed);

		/** Tool menu: runs the SlowTree hello-compute probe and prints the report. */
		void OnHelloComputeProbePressed();

		/** Adds a labelled row so the sliders read as more than bare tracks. */
		HSlider* AddSliderRow(VBoxContainer* Parent, const String& Text, double Min, double Max, double Step);

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
