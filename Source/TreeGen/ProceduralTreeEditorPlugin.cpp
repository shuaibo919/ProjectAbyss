#include "TreeGen/ProceduralTreeEditorPlugin.h"

#include "TreeGen/ProceduralTree.h"
#include "TreeGen/ProceduralTreeGrowthParameters.h"
#include "TreeGen/SlowTree/SlowTreeCompute.h"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_selection.hpp>
#include <godot_cpp/classes/editor_undo_redo_manager.hpp>
#include <godot_cpp/classes/foldable_container.hpp>
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
	const Callable OnCompleted = callable_mp(this, &ProceduralTreeEditorPlugin::SyncFromTree);
	ProceduralTree* Previous = ResolveTree();
	if (Previous && Previous->is_connected("generation_completed", OnCompleted))
	{
		Previous->disconnect("generation_completed", OnCompleted);
	}
	ProceduralTree* Tree = Object::cast_to<ProceduralTree>(Target);
	EditedTreeId = (Tree != nullptr) ? Tree->get_instance_id() : ObjectID();
	if (Tree)
	{
		Tree->connect("generation_completed", OnCompleted);
	}

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

void ProceduralTreeEditorPlugin::OnCrossedFoliageToggled(bool bPressed)
{
	if (bSyncingWidgets)
	{
		return;
	}
	ProceduralTree* Tree = ResolveTree();
	if (Tree)
	{
		Tree->SetFoliageMode(bPressed ? 1 : 0);
		SyncFromTree();
	}
}

void ProceduralTreeEditorPlugin::OnSpeciesRulesToggled(bool bPressed)
{
	if (bSyncingWidgets)
	{
		return;
	}
	ProceduralTree* Tree = ResolveTree();
	if (Tree)
	{
		Tree->SetSpeciesRules(bPressed);
		SyncFromTree();
	}
}

void ProceduralTreeEditorPlugin::OnStructuralBranchesToggled(bool bPressed)
{
	ProceduralTree* Tree = ResolveTree();
	if (Tree && !bSyncingWidgets)
	{
		Tree->SetStructuralBranches(bPressed);
		SyncFromTree();
	}
}

void ProceduralTreeEditorPlugin::OnShowLeavesToggled(bool bPressed)
{
	ProceduralTree* Tree = ResolveTree();
	if (Tree && !bSyncingWidgets)
	{
		Tree->SetGenerateLeaves(bPressed);
	}
}

void ProceduralTreeEditorPlugin::OnGrowthValueChanged(double Value, const StringName& Property)
{
	ProceduralTree* Tree = ResolveTree();
	if (!Tree || bSyncingWidgets)
	{
		return;
	}
	Ref<ProceduralTreeGrowthParameters> Parameters = Tree->GetGrowthParameters();
	if (Parameters.is_null())
	{
		Parameters.instantiate();
		Tree->SetGrowthParameters(Parameters);
	}
	Parameters->set(Property, Value);
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
	GpuTessellationCheck->set_tooltip_text(
		"Used by Regenerate / bake. Automatic preview uses the background CPU generator.");
	GpuTessellationCheck->connect("toggled", callable_mp(this, &ProceduralTreeEditorPlugin::OnGpuTessellationToggled));
	SelectionBox->add_child(GpuTessellationCheck);

	// Stage 2.5: SlowTree 形变旋钮(乘法乘数, 1.0 = 预设原样; 仅 SlowTree 后端可见)。
	// 只暴露用户点名的 4 个(粗细/密度), 其余参数留在预设里。
	SlowTreeTuningBox = memnew(VBoxContainer);
	SelectionBox->add_child(SlowTreeTuningBox);
	CrossedFoliageCheck = memnew(CheckBox);
	CrossedFoliageCheck->set_text("Crossed leaf clusters (alpha mask)");
	CrossedFoliageCheck->connect("toggled", callable_mp(this, &ProceduralTreeEditorPlugin::OnCrossedFoliageToggled));
	SlowTreeTuningBox->add_child(CrossedFoliageCheck);
	SpeciesRulesCheck = memnew(CheckBox);
	SpeciesRulesCheck->set_text("Species branching rules");
	SpeciesRulesCheck->connect("toggled", callable_mp(this, &ProceduralTreeEditorPlugin::OnSpeciesRulesToggled));
	SlowTreeTuningBox->add_child(SpeciesRulesCheck);
	ShowLeavesCheck = memnew(CheckBox);
	ShowLeavesCheck->set_text("Show foliage");
	ShowLeavesCheck->connect("toggled", callable_mp(this, &ProceduralTreeEditorPlugin::OnShowLeavesToggled));
	SlowTreeTuningBox->add_child(ShowLeavesCheck);
	GrowthFoldout = memnew(FoldableContainer);
	GrowthFoldout->set_title("Branch growth");
	GrowthFoldout->set_folded(true);
	SlowTreeTuningBox->add_child(GrowthFoldout);
	GrowthTuningBox = memnew(VBoxContainer);
	GrowthFoldout->add_child(GrowthTuningBox);
	StructuralBranchesCheck = memnew(CheckBox);
	StructuralBranchesCheck->set_text("Connected branch structure");
	StructuralBranchesCheck->connect("toggled", callable_mp(this, &ProceduralTreeEditorPlugin::OnStructuralBranchesToggled));
	GrowthTuningBox->add_child(StructuralBranchesCheck);
	TrunkBendSlider = AddSliderRow(GrowthTuningBox, "Trunk bend", 0.0, 3.0, 0.01);
	TrunkBendSlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnGrowthValueChanged).bind(StringName("trunk_bend")));
	BranchBendSlider = AddSliderRow(GrowthTuningBox, "Branch bend", 0.0, 3.0, 0.01);
	BranchBendSlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnGrowthValueChanged).bind(StringName("branch_bend")));
	ForkingSlider = AddSliderRow(GrowthTuningBox, "Unequal forks", 0.0, 2.0, 0.01);
	ForkingSlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnGrowthValueChanged).bind(StringName("forking")));
	BambooTuningBox = memnew(VBoxContainer);
	GrowthTuningBox->add_child(BambooTuningBox);
	BambooInternodeSlider = AddSliderRow(BambooTuningBox, "Bamboo internode length (m)", 0.15, 0.60, 0.01);
	BambooInternodeSlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnGrowthValueChanged).bind(StringName("bamboo_internode_length")));
	BambooNodeSlider = AddSliderRow(BambooTuningBox, "Bamboo node definition", 0.0, 2.0, 0.01);
	BambooNodeSlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnGrowthValueChanged).bind(StringName("bamboo_node_definition")));
	BambooLeafSlider = AddSliderRow(BambooTuningBox, "Bamboo leaf scale", 0.5, 2.0, 0.01);
	BambooLeafSlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnGrowthValueChanged).bind(StringName("bamboo_leaf_scale")));
	SlowLeafDensitySlider = AddSliderRow(SlowTreeTuningBox, "Leaf cluster density", 0.01, 1.0, 0.01);
	SlowLeafDensitySlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnDensityChanged));
	SlowSeasonSlider = AddSliderRow(SlowTreeTuningBox, "Season (0 winter - 2 summer - 4 winter)", 0.0, 4.0, 0.01);
	SlowSeasonSlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnSeasonChanged));

	TrunkThicknessSlider = AddSliderRow(SlowTreeTuningBox, "Trunk thickness", 0.1, 5.0, 0.01);
	TrunkThicknessSlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnTrunkThicknessChanged));

	RootThicknessSlider = AddSliderRow(SlowTreeTuningBox, "Root thickness", 0.1, 5.0, 0.01);
	RootThicknessSlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnRootThicknessChanged));

	BranchThicknessSlider = AddSliderRow(SlowTreeTuningBox, "Branch thickness", 0.1, 5.0, 0.01);
	BranchThicknessSlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnBranchThicknessChanged));

	BranchDensitySlider = AddSliderRow(SlowTreeTuningBox, "Branch density", 0.1, 5.0, 0.01);
	BranchDensitySlider->connect("value_changed", callable_mp(this, &ProceduralTreeEditorPlugin::OnBranchDensityChanged));

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
		StatsLabel->set_text(vformat("%d verts / %d tris\n%d surfaces / %d clusters\n%.1f ms build / %.1f ms submit%s",
									 Tree->GetVertexCount(), Tree->GetTriangleCount(), Tree->get_mesh().is_valid() ? Tree->get_mesh()->get_surface_count() : 0,
									 Tree->GetLeafCount(), Tree->GetGenerationMs(), Tree->GetCommitMs(),
									 Tree->IsPreviewPending()
										 ? String("\nPreview updating...")
										 : (Tree->WasTruncated() ? String("\nSegment budget reached") : String())));
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
	SlowTreeTuningBox->set_visible(bSlowTree);
	CrossedFoliageCheck->set_pressed(Tree->GetFoliageMode() == 1);
	SpeciesRulesCheck->set_pressed(Tree->HasSpeciesRules());
	ShowLeavesCheck->set_pressed(Tree->ShouldGenerateLeaves());
	const bool bSupportsStructure = bSlowTree && Tree->HasSpeciesRules() && Tree->GetFoliageMode() == 1 &&
		Tree->GetSlowTreePreset() >= 0 && Tree->GetSlowTreePreset() < SlowTreeGenerator::GetPresetCount();
	GrowthFoldout->set_visible(bSupportsStructure);
	StructuralBranchesCheck->set_pressed(Tree->GetStructuralBranches());
	const Ref<ProceduralTreeGrowthParameters> Growth = Tree->GetGrowthParameters();
	TrunkBendSlider->set_value(Growth.is_valid() ? Growth->GetTrunkBend() : 1.0);
	BranchBendSlider->set_value(Growth.is_valid() ? Growth->GetBranchBend() : 1.0);
	ForkingSlider->set_value(Growth.is_valid() ? Growth->GetForking() : 1.0);
	TrunkBendSlider->set_editable(Tree->GetStructuralBranches());
	BranchBendSlider->set_editable(Tree->GetStructuralBranches());
	const int32_t SlowPreset = Tree->GetSlowTreePreset();
	BambooTuningBox->set_visible(SlowPreset == 4);
	BambooInternodeSlider->set_value(Growth.is_valid() ? Growth->GetBambooInternodeLength() : 0.28);
	BambooNodeSlider->set_value(Growth.is_valid() ? Growth->GetBambooNodeDefinition() : 1.0);
	BambooLeafSlider->set_value(Growth.is_valid() ? Growth->GetBambooLeafScale() : 1.0);
	BambooInternodeSlider->set_editable(Tree->GetStructuralBranches());
	BambooNodeSlider->set_editable(Tree->GetStructuralBranches());
	BambooLeafSlider->set_editable(Tree->GetStructuralBranches());
	const bool bHasForks = SlowPreset == 0 || SlowPreset == 1 || SlowPreset == 3 || SlowPreset == 6;
	ForkingSlider->set_editable(Tree->GetStructuralBranches() && bHasForks);
	ForkingSlider->set_tooltip_text(bHasForks ? "Unequal scaffold forks."
		: "This species keeps its main axes; additional scaffold forks are disabled.");
	RootThicknessSlider->set_editable(!(bSupportsStructure && Tree->GetStructuralBranches() && SlowPreset == 4));
	GpuTessellationCheck->set_disabled(bSupportsStructure && Tree->GetStructuralBranches());
	GpuTessellationCheck->set_tooltip_text(bSupportsStructure && Tree->GetStructuralBranches()
		? "Connected branches use the CPU worker. GPU tessellation is available for the other branch paths."
		: "GPU tessellation for explicit generation; live preview uses the CPU worker.");
	SlowLeafDensitySlider->set_value(Tree->GetLeafDensity());
	SlowSeasonSlider->set_value(Tree->GetSeason());
	TrunkThicknessSlider->set_value(Tree->GetTrunkThickness());
	RootThicknessSlider->set_value(Tree->GetRootThickness());
	BranchThicknessSlider->set_value(Tree->GetBranchThickness());
	BranchDensitySlider->set_value(Tree->GetBranchDensity());
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
TREE_PANEL_CALLBACK(OnTrunkThicknessChanged, SetTrunkThickness, float)
TREE_PANEL_CALLBACK(OnRootThicknessChanged, SetRootThickness, float)
TREE_PANEL_CALLBACK(OnBranchThicknessChanged, SetBranchThickness, float)
TREE_PANEL_CALLBACK(OnBranchDensityChanged, SetBranchDensity, float)

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
