#include "TreeGen/ProceduralTreeEditorPlugin.h"

#include "TreeGen/ProceduralTree.h"
#include "TreeGen/SlowTree/SlowTreeCompute.h"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_selection.hpp>
#include <godot_cpp/classes/editor_undo_redo_manager.hpp>
#include <godot_cpp/classes/h_separator.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace
{
	/** Dropdown order: the paper's four species first, then the Chinese ones, Default last. */
	const ProceduralTreeParameters::EPreset PRESET_ORDER[] = {
		ProceduralTreeParameters::PRESET_GINKGO,
		ProceduralTreeParameters::PRESET_PEACH,
		ProceduralTreeParameters::PRESET_CAMPHOR,
		ProceduralTreeParameters::PRESET_PINE,
		ProceduralTreeParameters::PRESET_CHINESE_FIR,
		ProceduralTreeParameters::PRESET_WILLOW,
		ProceduralTreeParameters::PRESET_APPLE,
		ProceduralTreeParameters::PRESET_SASSAFRAS,
		ProceduralTreeParameters::PRESET_PALM,
		ProceduralTreeParameters::PRESET_TAMARACK,
		ProceduralTreeParameters::PRESET_DEFAULT,
	};

	const int32_t PRESET_ORDER_COUNT = int32_t(sizeof(PRESET_ORDER) / sizeof(PRESET_ORDER[0]));
} // namespace

void ProceduralTreeEditorPlugin::_bind_methods()
{
}

// ==================== Plugin lifecycle ====================

void ProceduralTreeEditorPlugin::_enter_tree()
{
	BuildPanel();
	add_control_to_container(CONTAINER_SPATIAL_EDITOR_SIDE_RIGHT, Panel);
	// Hidden until the editor tells us one of our nodes is being edited.
	Panel->set_visible(false);

	add_tool_menu_item("SlowTree: Hello-Compute Probe", callable_mp(this, &ProceduralTreeEditorPlugin::OnHelloComputeProbePressed));
}

void ProceduralTreeEditorPlugin::_exit_tree()
{
	remove_tool_menu_item("SlowTree: Hello-Compute Probe");

	if (Panel != nullptr)
	{
		remove_control_from_container(CONTAINER_SPATIAL_EDITOR_SIDE_RIGHT, Panel);
		Panel->queue_free();
		Panel = nullptr;
	}
}

/**
 * The panel stays parented to the 3D editor's right-hand container for the plugin's lifetime and
 * is only shown or hidden.
 *
 * It deliberately does not live in a dock slot: DOCK_SLOT_RIGHT_BL shares its slot with the
 * Inspector, so adding and removing a dock per selection would steal the Inspector's tab at
 * exactly the moment the user wants it. A side container is a plain VBoxContainer, so toggling
 * visibility is safe, and nothing touches the dock layout.
 */
void ProceduralTreeEditorPlugin::SetPanelVisible(bool bVisible)
{
	if (Panel == nullptr || bVisible == bPanelVisible)
	{
		return;
	}

	Panel->set_visible(bVisible);
	bPanelVisible = bVisible;
}

void ProceduralTreeEditorPlugin::_make_visible(bool bVisible)
{
	SetPanelVisible(bVisible);
}

String ProceduralTreeEditorPlugin::_get_plugin_name() const
{
	return "Procedural Tree";
}

bool ProceduralTreeEditorPlugin::_handles(Object* Target) const
{
	return Object::cast_to<ProceduralTree>(Target) != nullptr;
}

void ProceduralTreeEditorPlugin::_edit(Object* Target)
{
	ProceduralTree* Tree = Object::cast_to<ProceduralTree>(Target);
	EditedTreeId = (Tree != nullptr) ? Tree->get_instance_id() : ObjectID();

	SyncFromTree();
}

ProceduralTree* ProceduralTreeEditorPlugin::ResolveTree() const
{
	if (!EditedTreeId.is_valid())
	{
		return nullptr;
	}

	// The node may have been deleted since it was selected, so always re-resolve.
	return Object::cast_to<ProceduralTree>(ObjectDB::get_instance(EditedTreeId));
}

void ProceduralTreeEditorPlugin::OnHelloComputeProbePressed()
{
	// Stage 0 gate: runs on the main thread against a fresh local RD, prints the
	// full report into the Output panel.
	SlowTreeCompute::hello_compute_probe(1 << 20, true);
}

// ==================== Panel construction ====================

HSlider* ProceduralTreeEditorPlugin::AddSliderRow(
	VBoxContainer* Parent,
	const String& Text,
	double Min,
	double Max,
	double Step)
{
	Label* Caption = memnew(Label);
	Caption->set_text(Text);
	Parent->add_child(Caption);

	HSlider* Slider = memnew(HSlider);
	Slider->set_min(Min);
	Slider->set_max(Max);
	Slider->set_step(Step);
	Parent->add_child(Slider);

	return Slider;
}

void ProceduralTreeEditorPlugin::BuildPanel()
{
	Panel = memnew(VBoxContainer);
	Panel->set_name("Trees");

	Label* Title = memnew(Label);
	// Creation lives in the Add Node dialog; this panel only edits what is already selected.
	Title->set_text("Species (applies to selection)");
	Panel->add_child(Title);

	// Weber 物种选择 + 应用按钮(仅 Weber 后端可见)。
	WeberSpeciesBox = memnew(VBoxContainer);
	Panel->add_child(WeberSpeciesBox);

	PresetPicker = memnew(OptionButton);
	for (int32_t Index = 0; Index < PRESET_ORDER_COUNT; ++Index)
	{
		PresetPicker->add_item(ProceduralTreeParameters::GetPresetName(PRESET_ORDER[Index]), Index);
	}
	PresetPicker->select(0);
	WeberSpeciesBox->add_child(PresetPicker);

	Button* Apply = memnew(Button);
	Apply->set_text("Apply to Selected");
	Apply->connect("pressed", callable_mp(this, &ProceduralTreeEditorPlugin::OnApplyToSelectedPressed));
	WeberSpeciesBox->add_child(Apply);

	Panel->add_child(memnew(HSeparator));

	// Everything below only applies to the selected tree.
	SelectionBox = memnew(VBoxContainer);
	Panel->add_child(SelectionBox);

	SelectionLabel = memnew(Label);
	SelectionLabel->set_text("Select a ProceduralTree to edit.");
	SelectionBox->add_child(SelectionLabel);

	StatsLabel = memnew(Label);
	StatsLabel->set_text("");
	SelectionBox->add_child(StatsLabel);

	Label* BackendCaption = memnew(Label);
	BackendCaption->set_text("Backend");
	SelectionBox->add_child(BackendCaption);

	BackendPicker = memnew(OptionButton);
	BackendPicker->add_item("Weber-Penn (HPG 2025)", ProceduralTree::BACKEND_WEBER_PENN);
	BackendPicker->add_item("SlowTree", ProceduralTree::BACKEND_SLOWTREE);
	BackendPicker->connect("item_selected", callable_mp(this, &ProceduralTreeEditorPlugin::OnBackendChanged));
	SelectionBox->add_child(BackendPicker);

	// Weber 专属微调控件(后端为 SlowTree 时整体隐藏)。
	WeberTuningBox = memnew(VBoxContainer);
	SelectionBox->add_child(WeberTuningBox);

	SeasonSlider = AddSliderRow(WeberTuningBox, "Season (0 winter - 2 summer - 4 winter)", 0.0, 4.0, 0.01);
	SeasonSlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnSeasonChanged));

	WindSlider = AddSliderRow(WeberTuningBox, "Wind bend", 0.0, 20.0, 0.1);
	WindSlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnWindChanged));

	DensitySlider = AddSliderRow(WeberTuningBox, "Leaf density", 0.01, 1.0, 0.01);
	DensitySlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnDensityChanged));

	Label* RadialCaption = memnew(Label);
	RadialCaption->set_text("Radial segments");
	WeberTuningBox->add_child(RadialCaption);

	RadialSpin = memnew(SpinBox);
	RadialSpin->set_min(3);
	RadialSpin->set_max(32);
	RadialSpin->set_step(1);
	RadialSpin->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnRadialChanged));
	WeberTuningBox->add_child(RadialSpin);

	BarkDetailCheck = memnew(CheckBox);
	BarkDetailCheck->set_text("Bark relief");
	BarkDetailCheck->connect("toggled", callable_mp(this, &ProceduralTreeEditorPlugin::OnBarkDetailToggled));
	WeberTuningBox->add_child(BarkDetailCheck);

	// SlowTree 预设下拉(仅 SlowTree 后端可见; 列表来自预设表, 与 inspector 枚举提示一致)。
	SlowTreePresetCaption = memnew(Label);
	SlowTreePresetCaption->set_text("SlowTree preset");
	SelectionBox->add_child(SlowTreePresetCaption);

	SlowTreePresetPicker = memnew(OptionButton);
	for (int32_t Index = 0; Index < SlowTreeGenerator::GetPresetCount(); ++Index)
	{
		SlowTreePresetPicker->add_item(SlowTreeGenerator::GetPresetName(Index), Index);
	}
	SlowTreePresetPicker->connect("item_selected", callable_mp(this, &ProceduralTreeEditorPlugin::OnSlowTreePresetChanged));
	SelectionBox->add_child(SlowTreePresetPicker);

	// Stage 2: GPU 细分开关(仅 SlowTree 后端可见; 与 inspector 的 use_gpu_tessellation 同源)。
	GpuTessellationCheck = memnew(CheckBox);
	GpuTessellationCheck->set_text("GPU tessellation (compute)");
	GpuTessellationCheck->connect("toggled", callable_mp(this, &ProceduralTreeEditorPlugin::OnGpuTessellationToggled));
	SelectionBox->add_child(GpuTessellationCheck);

	Label* SeedCaption = memnew(Label);
	SeedCaption->set_text("Seed");
	SelectionBox->add_child(SeedCaption);

	HBoxContainer* SeedRow = memnew(HBoxContainer);
	SelectionBox->add_child(SeedRow);

	SeedSpin = memnew(SpinBox);
	SeedSpin->set_min(0);
	SeedSpin->set_max(1000000);
	SeedSpin->set_step(1);
	SeedSpin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	SeedSpin->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnSeedChanged));
	SeedRow->add_child(SeedSpin);

	Button* Randomize = memnew(Button);
	Randomize->set_text("Randomize");
	Randomize->connect("pressed", callable_mp(this, &ProceduralTreeEditorPlugin::OnRandomizeSeedPressed));
	SeedRow->add_child(Randomize);

	SelectionBox->add_child(memnew(HSeparator));

	Button* Regenerate = memnew(Button);
	Regenerate->set_text("Regenerate");
	Regenerate->connect("pressed", callable_mp(this, &ProceduralTreeEditorPlugin::OnRegeneratePressed));
	SelectionBox->add_child(Regenerate);

	SyncFromTree();
}

void ProceduralTreeEditorPlugin::SyncFromTree()
{
	if (Panel == nullptr)
	{
		return;
	}

	ProceduralTree* Tree = ResolveTree();

	if (Tree == nullptr)
	{
		SelectionLabel->set_text("Select a ProceduralTree to edit.");
		StatsLabel->set_text("");
		SelectionBox->set_visible(false);
		SelectionLabel->set_visible(true);
		return;
	}

	SelectionBox->set_visible(true);
	SelectionLabel->set_text(Tree->get_name());

	const bool bSlowTree = Tree->GetBackend() == ProceduralTree::BACKEND_SLOWTREE;
	if (bSlowTree)
	{
		// SlowTree 没有段/叶独立计数, surface 数最有意义。
		StatsLabel->set_text(vformat(
			"%d verts / %d tris\n%d surfaces%s",
			Tree->GetVertexCount(),
			Tree->GetTriangleCount(),
			Tree->GetSegmentCount(),
			Tree->WasTruncated() ? String("\n(capped - vertex budget)") : String()));
	}
	else
	{
		StatsLabel->set_text(vformat(
			"%d verts / %d tris\n%d segments, %d leaves%s",
			Tree->GetVertexCount(),
			Tree->GetTriangleCount(),
			Tree->GetSegmentCount(),
			Tree->GetLeafCount(),
			Tree->WasTruncated() ? String("\n(capped - lower leaf density)") : String()));
	}

	bSyncingWidgets = true;
	BackendPicker->select(Tree->GetBackend());
	SlowTreePresetPicker->select(Tree->GetSlowTreePreset());
	WeberSpeciesBox->set_visible(!bSlowTree);
	WeberTuningBox->set_visible(!bSlowTree);
	SlowTreePresetCaption->set_visible(bSlowTree);
	SlowTreePresetPicker->set_visible(bSlowTree);
	GpuTessellationCheck->set_visible(bSlowTree);
	GpuTessellationCheck->set_pressed(Tree->ShouldUseGpuTessellation());
	SeedSpin->set_value(Tree->GetSeed());
	SeasonSlider->set_value(Tree->GetSeason());
	WindSlider->set_value(Tree->GetWindStrength());
	DensitySlider->set_value(Tree->GetLeafDensity());
	RadialSpin->set_value(Tree->GetRadialSegments());
	BarkDetailCheck->set_pressed(Tree->HasBarkDetail());
	bSyncingWidgets = false;
}

// ==================== Panel callbacks ====================

int32_t ProceduralTreeEditorPlugin::GetSelectedPreset() const
{
	if (PresetPicker == nullptr)
	{
		return ProceduralTreeParameters::PRESET_APPLE;
	}

	const int32_t Index = PresetPicker->get_selected();
	if (Index < 0 || Index >= PRESET_ORDER_COUNT)
	{
		return ProceduralTreeParameters::PRESET_APPLE;
	}

	return int32_t(PRESET_ORDER[Index]);
}

void ProceduralTreeEditorPlugin::OnApplyToSelectedPressed()
{
	ProceduralTree* Tree = ResolveTree();
	if (Tree == nullptr)
	{
		return;
	}

	Tree->ApplyPreset(GetSelectedPreset());
	SyncFromTree();
}

void ProceduralTreeEditorPlugin::OnRegeneratePressed()
{
	ProceduralTree* Tree = ResolveTree();
	if (Tree == nullptr)
	{
		return;
	}

	Tree->Generate();
	SyncFromTree();
}

void ProceduralTreeEditorPlugin::OnRandomizeSeedPressed()
{
	ProceduralTree* Tree = ResolveTree();
	if (Tree == nullptr)
	{
		return;
	}

	Tree->SetSeed(int32_t(UtilityFunctions::randi() % 1000000));
	SyncFromTree();
}

void ProceduralTreeEditorPlugin::OnBackendChanged(int64_t Index)
{
	if (bSyncingWidgets)
	{
		return;
	}

	ProceduralTree* Tree = ResolveTree();
	if (Tree == nullptr)
	{
		return;
	}

	Tree->SetBackend(int32_t(Index));
	SyncFromTree();
}

void ProceduralTreeEditorPlugin::OnSlowTreePresetChanged(int64_t Index)
{
	if (bSyncingWidgets)
	{
		return;
	}

	ProceduralTree* Tree = ResolveTree();
	if (Tree == nullptr)
	{
		return;
	}

	Tree->SetSlowTreePreset(int32_t(Index));
	SyncFromTree();
}

void ProceduralTreeEditorPlugin::OnGpuTessellationToggled(bool bPressed)
{
	if (bSyncingWidgets)
	{
		return;
	}

	ProceduralTree* Tree = ResolveTree();
	if (Tree == nullptr)
	{
		return;
	}

	Tree->SetUseGpuTessellation(bPressed);
	SyncFromTree();
}

#define TREE_PANEL_CALLBACK(Name, Setter, Cast)          \
	void ProceduralTreeEditorPlugin::Name(double Value)  \
	{                                                    \
		if (bSyncingWidgets)                             \
		{                                                \
			return;                                      \
		}                                                \
		ProceduralTree* Tree = ResolveTree();            \
		if (Tree == nullptr)                             \
		{                                                \
			return;                                      \
		}                                                \
		Tree->Setter(Cast(Value));                       \
		SyncFromTree();                                  \
	}

TREE_PANEL_CALLBACK(OnSeedChanged, SetSeed, int32_t)
TREE_PANEL_CALLBACK(OnSeasonChanged, SetSeason, float)
TREE_PANEL_CALLBACK(OnWindChanged, SetWindStrength, float)
TREE_PANEL_CALLBACK(OnDensityChanged, SetLeafDensity, float)
TREE_PANEL_CALLBACK(OnRadialChanged, SetRadialSegments, int32_t)

#undef TREE_PANEL_CALLBACK

void ProceduralTreeEditorPlugin::OnBarkDetailToggled(bool bPressed)
{
	if (bSyncingWidgets)
	{
		return;
	}

	ProceduralTree* Tree = ResolveTree();
	if (Tree == nullptr)
	{
		return;
	}

	Tree->SetBarkDetail(bPressed);
	SyncFromTree();
}
