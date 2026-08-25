#include "AncientBuilding/AncientBuildingEditorPlugin.h"

#include "AncientBuilding/AncientBuilding.h"
#include "AncientBuilding/AncientBuildingParameters.h"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_selection.hpp>
#include <godot_cpp/classes/editor_undo_redo_manager.hpp>
#include <godot_cpp/classes/h_separator.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <algorithm>

using namespace godot;

namespace
{
	/**
	 * Dock order groups the nine types by generator, which is also how they group visually:
	 * gabled, then hipped, then centralised. See Docs/AncientBuilding_Spec.md section 2.
	 */
	/**
	 * Picker order groups the nine types by generator, which is also how they group visually:
	 * gabled, then hipped, then centralised. See Docs/AncientBuilding_Spec.md section 2.
	 *
	 * The labels themselves come from AncientBuildingParameters, so the Chinese terms live in one
	 * bound place instead of being duplicated as literals here.
	 */
	const int32_t ROOF_ORDER[] = {
		AncientBuildingParameters::ROOF_FLUSH_GABLE,
		AncientBuildingParameters::ROOF_OVERHANGING_GABLE,
		AncientBuildingParameters::ROOF_ROUND_RIDGE,
		AncientBuildingParameters::ROOF_GABLE_AND_HIP,
		AncientBuildingParameters::ROOF_HIP,
		AncientBuildingParameters::ROOF_HOLLOW,
		AncientBuildingParameters::ROOF_PYRAMIDAL,
		AncientBuildingParameters::ROOF_ROUND,
		AncientBuildingParameters::ROOF_HELMET,
	};

	const int32_t ROOF_ENTRY_COUNT = int32_t(sizeof(ROOF_ORDER) / sizeof(ROOF_ORDER[0]));

	bool IsPolygonalRoof(int32_t RoofType)
	{
		return AncientBuildingParameters::IsCentralisedRoof(RoofType);
	}
} // namespace

void AncientBuildingEditorPlugin::_bind_methods()
{
}

// ==================== Lifecycle ====================

void AncientBuildingEditorPlugin::_enter_tree()
{
	BuildPanel();
	add_control_to_container(CONTAINER_SPATIAL_EDITOR_SIDE_RIGHT, Panel);
	// Hidden until the editor tells us one of our nodes is being edited.
	Panel->set_visible(false);
}

void AncientBuildingEditorPlugin::_exit_tree()
{
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
void AncientBuildingEditorPlugin::SetPanelVisible(bool bVisible)
{
	if (Panel == nullptr || bVisible == bPanelVisible)
	{
		return;
	}

	Panel->set_visible(bVisible);
	bPanelVisible = bVisible;
}

void AncientBuildingEditorPlugin::_make_visible(bool bVisible)
{
	SetPanelVisible(bVisible);
}

String AncientBuildingEditorPlugin::_get_plugin_name() const
{
	return "Ancient Building";
}

bool AncientBuildingEditorPlugin::_handles(Object* Target) const
{
	return Object::cast_to<AncientBuilding>(Target) != nullptr;
}

void AncientBuildingEditorPlugin::_edit(Object* Target)
{
	AncientBuilding* Building = Object::cast_to<AncientBuilding>(Target);
	EditedBuildingId = (Building != nullptr) ? Building->get_instance_id() : ObjectID();

	SyncFromBuilding();
}

AncientBuilding* AncientBuildingEditorPlugin::ResolveBuilding() const
{
	if (!EditedBuildingId.is_valid())
	{
		return nullptr;
	}

	// The node may have been deleted since selection, so always re-resolve.
	return Object::cast_to<AncientBuilding>(ObjectDB::get_instance(EditedBuildingId));
}

// ==================== Panel ====================

void AncientBuildingEditorPlugin::FillRoofPicker(OptionButton* Picker) const
{
	for (int32_t Index = 0; Index < ROOF_ENTRY_COUNT; ++Index)
	{
		Picker->add_item(AncientBuildingParameters::GetRoofTypeNameLocalized(ROOF_ORDER[Index]), Index);
	}
	Picker->select(3);
}

int32_t AncientBuildingEditorPlugin::RoofTypeAt(const OptionButton* Picker) const
{
	if (Picker == nullptr)
	{
		return AncientBuildingParameters::ROOF_GABLE_AND_HIP;
	}

	const int32_t Index = Picker->get_selected();
	if (Index < 0 || Index >= ROOF_ENTRY_COUNT)
	{
		return AncientBuildingParameters::ROOF_GABLE_AND_HIP;
	}

	return ROOF_ORDER[Index];
}

Label* AncientBuildingEditorPlugin::AddCaption(VBoxContainer* Parent, const String& Text) const
{
	Label* Caption = memnew(Label);
	Caption->set_text(Text);
	Parent->add_child(Caption);

	return Caption;
}

SpinBox* AncientBuildingEditorPlugin::AddSpinRow(
	VBoxContainer* Parent, const String& Text, double Min, double Max, double Step)
{
	HBoxContainer* Row = memnew(HBoxContainer);
	Parent->add_child(Row);

	Label* Caption = memnew(Label);
	Caption->set_text(Text);
	Caption->set_custom_minimum_size(Vector2(112, 0));
	Row->add_child(Caption);

	SpinBox* Spin = memnew(SpinBox);
	Spin->set_min(Min);
	Spin->set_max(Max);
	Spin->set_step(Step);
	Spin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	Row->add_child(Spin);

	return Spin;
}

HSlider* AncientBuildingEditorPlugin::AddSliderRow(
	VBoxContainer* Parent, const String& Text, double Min, double Max, double Step)
{
	AddCaption(Parent, Text);

	HSlider* Slider = memnew(HSlider);
	Slider->set_min(Min);
	Slider->set_max(Max);
	Slider->set_step(Step);
	Parent->add_child(Slider);

	return Slider;
}

void AncientBuildingEditorPlugin::BuildPanel()
{
	Panel = memnew(VBoxContainer);
	Panel->set_name("Buildings");

	SelectionLabel = memnew(Label);
	Panel->add_child(SelectionLabel);

	DerivedLabel = memnew(Label);
	// The module system is the whole point, so show what one number derived.
	DerivedLabel->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	Panel->add_child(DerivedLabel);

	StatsLabel = memnew(Label);
	Panel->add_child(StatsLabel);

	Panel->add_child(memnew(HSeparator));

	EditRoofPicker = memnew(OptionButton);
	FillRoofPicker(EditRoofPicker);
	EditRoofPicker->connect("item_selected", callable_mp(this, &AncientBuildingEditorPlugin::OnEditRoofChanged));
	Panel->add_child(EditRoofPicker);

	WidthSpin = AddSpinRow(Panel, "Width", 2.0, 60.0, 0.1);
	WidthSpin->connect("value_changed", callable_mp(this, &AncientBuildingEditorPlugin::OnWidthChanged));

	DepthSpin = AddSpinRow(Panel, "Depth", 2.0, 60.0, 0.1);
	DepthSpin->connect("value_changed", callable_mp(this, &AncientBuildingEditorPlugin::OnDepthChanged));

	EditSidesSpin = AddSpinRow(Panel, "Plan sides", 3, 24, 1);
	EditSidesSpin->connect("value_changed", callable_mp(this, &AncientBuildingEditorPlugin::OnSidesChanged));

	BaysXSpin = AddSpinRow(Panel, "Bays across", 1, 9, 1);
	BaysXSpin->connect("value_changed", callable_mp(this, &AncientBuildingEditorPlugin::OnBaysXChanged));

	BaysZSpin = AddSpinRow(Panel, "Bays deep", 1, 9, 1);
	BaysZSpin->connect("value_changed", callable_mp(this, &AncientBuildingEditorPlugin::OnBaysZChanged));

	RafterSpin = AddSpinRow(Panel, "Rafter courses", 3, 13, 1);
	RafterSpin->connect("value_changed", callable_mp(this, &AncientBuildingEditorPlugin::OnRafterChanged));

	RoofHeightSlider = AddSliderRow(Panel, "Roof height", 0.3, 2.5, 0.01);
	RoofHeightSlider->connect("value_changed", callable_mp(this, &AncientBuildingEditorPlugin::OnRoofHeightChanged));

	CornerRiseSlider = AddSliderRow(Panel, String::utf8("Corner upturn (翼角起翘)"), 0.0, 4.0, 0.01);
	CornerRiseSlider->connect("value_changed", callable_mp(this, &AncientBuildingEditorPlugin::OnCornerRiseChanged));

	TileCoverageSlider = AddSliderRow(Panel, "Tile coverage (Cr)", 0.0, 1.0, 0.01);
	TileCoverageSlider->connect("value_changed", callable_mp(this, &AncientBuildingEditorPlugin::OnTileCoverageChanged));

	HBoxContainer* Toggles = memnew(HBoxContainer);
	Panel->add_child(Toggles);

	FenceCheck = memnew(CheckBox);
	FenceCheck->set_text("Fence");
	FenceCheck->connect("toggled", callable_mp(this, &AncientBuildingEditorPlugin::OnFenceToggled));
	Toggles->add_child(FenceCheck);

	StepsCheck = memnew(CheckBox);
	StepsCheck->set_text("Steps");
	StepsCheck->connect("toggled", callable_mp(this, &AncientBuildingEditorPlugin::OnStepsToggled));
	Toggles->add_child(StepsCheck);

	WallsCheck = memnew(CheckBox);
	WallsCheck->set_text("Walls");
	WallsCheck->connect("toggled", callable_mp(this, &AncientBuildingEditorPlugin::OnWallsToggled));
	Toggles->add_child(WallsCheck);

	Button* Regenerate = memnew(Button);
	Regenerate->set_text("Regenerate");
	Regenerate->connect("pressed", callable_mp(this, &AncientBuildingEditorPlugin::OnRegeneratePressed));
	Panel->add_child(Regenerate);

	SyncFromBuilding();
}

void AncientBuildingEditorPlugin::SyncFromBuilding()
{
	if (Panel == nullptr)
	{
		return;
	}

	AncientBuilding* Building = ResolveBuilding();
	if (Building == nullptr || Building->GetParameters().is_null())
	{
		// Reachable briefly between deselect and the dock being pulled, so keep it quiet.
		SelectionLabel->set_text("No building selected.");
		DerivedLabel->set_text("");
		StatsLabel->set_text("");
		return;
	}

	const Ref<AncientBuildingParameters> Parameters = Building->GetParameters();

	SelectionLabel->set_text(Building->get_name());

	DerivedLabel->set_text(vformat(
		"Module D = %.2f m\nEave height 11D = %.2f m\nPlatform 2D = %.2f m\nTotal %.2f m",
		Parameters->GetModule(),
		Parameters->GetEaveHeight(),
		Parameters->GetPlatformHeight(),
		Parameters->GetTotalHeight()));

	StatsLabel->set_text(vformat(
		"%d verts / %d tris", Building->GetVertexCount(), Building->GetTriangleCount()));

	bSyncingWidgets = true;

	for (int32_t Index = 0; Index < ROOF_ENTRY_COUNT; ++Index)
	{
		if (ROOF_ORDER[Index] == Parameters->GetRoofType())
		{
			EditRoofPicker->select(Index);
			break;
		}
	}

	const bool bPolygonal = Parameters->IsPolygonal();

	WidthSpin->set_value(Parameters->GetWidth());
	DepthSpin->set_value(Parameters->GetDepth());
	EditSidesSpin->set_value(Parameters->GetSides());
	BaysXSpin->set_value(Parameters->GetBaysX());
	BaysZSpin->set_value(Parameters->GetBaysZ());
	RafterSpin->set_value(Parameters->GetRafterCourses());
	RoofHeightSlider->set_value(Parameters->GetRoofHeightScale());
	CornerRiseSlider->set_value(Parameters->GetCornerRiseScale());
	TileCoverageSlider->set_value(Parameters->GetTileCoverage());
	FenceCheck->set_pressed(Parameters->ShouldGenerateFence());
	StepsCheck->set_pressed(Parameters->ShouldGenerateSteps());
	WallsCheck->set_pressed(Parameters->ShouldGenerateWalls());

	// Equation 8 again: a polygonal plan is regular, so depth and the two bay counts have no
	// meaning there. Disabling rather than hiding keeps the layout from jumping around.
	DepthSpin->set_editable(!bPolygonal);
	BaysXSpin->set_editable(!bPolygonal);
	BaysZSpin->set_editable(!bPolygonal);
	EditSidesSpin->set_editable(IsPolygonalRoof(Parameters->GetRoofType()));

	bSyncingWidgets = false;
}

// ==================== Callbacks ====================

void AncientBuildingEditorPlugin::OnEditRoofChanged(int32_t)
{
	if (bSyncingWidgets)
	{
		return;
	}

	AncientBuilding* Building = ResolveBuilding();
	if (Building == nullptr || Building->GetParameters().is_null())
	{
		return;
	}

	const Ref<AncientBuildingParameters> Parameters = Building->GetParameters();
	const int32_t RoofType = RoofTypeAt(EditRoofPicker);
	Parameters->SetRoofType(RoofType);

	// Keep the plan legal for the chosen roof, or the switch appears to do nothing.
	if (IsPolygonalRoof(RoofType))
	{
		if (Parameters->GetSides() == 4)
		{
			Parameters->SetSides(8);
		}
		Parameters->SetDepth(Parameters->GetWidth());
	}
	else
	{
		Parameters->SetSides(4);
	}

	SyncFromBuilding();
}

/** Applies a parameter change from a widget, then refreshes the derived readout. */
#define BUILDING_PANEL_CALLBACK(Name, Setter, Cast)                          \
	void AncientBuildingEditorPlugin::Name(double Value)                     \
	{                                                                        \
		if (bSyncingWidgets)                                                 \
		{                                                                    \
			return;                                                          \
		}                                                                    \
		AncientBuilding* Building = ResolveBuilding();                       \
		if (Building == nullptr || Building->GetParameters().is_null())      \
		{                                                                    \
			return;                                                          \
		}                                                                    \
		Building->GetParameters()->Setter(Cast(Value));                      \
		SyncFromBuilding();                                                  \
	}

BUILDING_PANEL_CALLBACK(OnDepthChanged, SetDepth, float)
BUILDING_PANEL_CALLBACK(OnBaysXChanged, SetBaysX, int32_t)
BUILDING_PANEL_CALLBACK(OnBaysZChanged, SetBaysZ, int32_t)
BUILDING_PANEL_CALLBACK(OnCornerRiseChanged, SetCornerRiseScale, float)
BUILDING_PANEL_CALLBACK(OnTileCoverageChanged, SetTileCoverage, float)
BUILDING_PANEL_CALLBACK(OnRoofHeightChanged, SetRoofHeightScale, float)
BUILDING_PANEL_CALLBACK(OnRafterChanged, SetRafterCourses, int32_t)

#undef BUILDING_PANEL_CALLBACK

void AncientBuildingEditorPlugin::OnWidthChanged(double Value)
{
	if (bSyncingWidgets)
	{
		return;
	}

	AncientBuilding* Building = ResolveBuilding();
	if (Building == nullptr || Building->GetParameters().is_null())
	{
		return;
	}

	const Ref<AncientBuildingParameters> Parameters = Building->GetParameters();
	Parameters->SetWidth(float(Value));
	// A polygonal plan is regular, so width and depth move together.
	if (Parameters->IsPolygonal())
	{
		Parameters->SetDepth(float(Value));
	}

	SyncFromBuilding();
}

void AncientBuildingEditorPlugin::OnSidesChanged(double Value)
{
	if (bSyncingWidgets)
	{
		return;
	}

	AncientBuilding* Building = ResolveBuilding();
	if (Building == nullptr || Building->GetParameters().is_null())
	{
		return;
	}

	const Ref<AncientBuildingParameters> Parameters = Building->GetParameters();
	Parameters->SetSides(std::max(int32_t(Value), 3));
	if (Parameters->IsPolygonal())
	{
		Parameters->SetDepth(Parameters->GetWidth());
	}

	SyncFromBuilding();
}

#define BUILDING_PANEL_TOGGLE(Name, Setter)                                  \
	void AncientBuildingEditorPlugin::Name(bool bPressed)                    \
	{                                                                        \
		if (bSyncingWidgets)                                                 \
		{                                                                    \
			return;                                                          \
		}                                                                    \
		AncientBuilding* Building = ResolveBuilding();                       \
		if (Building == nullptr || Building->GetParameters().is_null())      \
		{                                                                    \
			return;                                                          \
		}                                                                    \
		Building->GetParameters()->Setter(bPressed);                         \
		SyncFromBuilding();                                                  \
	}

BUILDING_PANEL_TOGGLE(OnFenceToggled, SetGenerateFence)
BUILDING_PANEL_TOGGLE(OnStepsToggled, SetGenerateSteps)
BUILDING_PANEL_TOGGLE(OnWallsToggled, SetGenerateWalls)

#undef BUILDING_PANEL_TOGGLE

void AncientBuildingEditorPlugin::OnRegeneratePressed()
{
	AncientBuilding* Building = ResolveBuilding();
	if (Building == nullptr)
	{
		return;
	}

	Building->Generate();
	SyncFromBuilding();
}
