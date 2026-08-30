#include "RockGen/ProceduralRockEditorPlugin.h"

#include "RockGen/ProceduralRock.h"
#include "RockGen/ProceduralRockParameters.h"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/h_separator.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

using namespace godot;

namespace
{
	const int32_t FORM_ORDER[] = {
		ProceduralRockParameters::FORM_SPHERE,
		ProceduralRockParameters::FORM_ELLIPSOID,
		ProceduralRockParameters::FORM_ROUNDED_BOX,
	};

	const int32_t FORM_ENTRY_COUNT = int32_t(sizeof(FORM_ORDER) / sizeof(FORM_ORDER[0]));
} // namespace

void ProceduralRockEditorPlugin::_bind_methods()
{
}

// ==================== Lifecycle ====================

void ProceduralRockEditorPlugin::_enter_tree()
{
	BuildPanel();
	add_control_to_container(CONTAINER_SPATIAL_EDITOR_SIDE_RIGHT, Panel);
	// Hidden until the editor tells us one of our nodes is being edited.
	Panel->set_visible(false);
}

void ProceduralRockEditorPlugin::_exit_tree()
{
	if (Panel != nullptr)
	{
		remove_control_from_container(CONTAINER_SPATIAL_EDITOR_SIDE_RIGHT, Panel);
		Panel->queue_free();
		Panel = nullptr;
	}
}

void ProceduralRockEditorPlugin::SetPanelVisible(bool bVisible)
{
	if (Panel == nullptr || bVisible == bPanelVisible)
	{
		return;
	}

	Panel->set_visible(bVisible);
	bPanelVisible = bVisible;
}

void ProceduralRockEditorPlugin::_make_visible(bool bVisible)
{
	SetPanelVisible(bVisible);
}

String ProceduralRockEditorPlugin::_get_plugin_name() const
{
	return "Procedural Rock";
}

bool ProceduralRockEditorPlugin::_handles(Object* Target) const
{
	return Object::cast_to<ProceduralRock>(Target) != nullptr;
}

void ProceduralRockEditorPlugin::_edit(Object* Target)
{
	ProceduralRock* Rock = Object::cast_to<ProceduralRock>(Target);
	EditedRockId = (Rock != nullptr) ? Rock->get_instance_id() : ObjectID();

	SyncFromRock();
}

ProceduralRock* ProceduralRockEditorPlugin::ResolveRock() const
{
	if (!EditedRockId.is_valid())
	{
		return nullptr;
	}

	// The node may have been deleted since selection, so always re-resolve.
	return Object::cast_to<ProceduralRock>(ObjectDB::get_instance(EditedRockId));
}

// ==================== Panel ====================

Label* ProceduralRockEditorPlugin::AddCaption(const String& Text) const
{
	Label* Caption = memnew(Label);
	Caption->set_text(Text);
	Panel->add_child(Caption);

	return Caption;
}

SpinBox* ProceduralRockEditorPlugin::AddSpinRow(const String& Text, double Min, double Max, double Step)
{
	HBoxContainer* Row = memnew(HBoxContainer);
	Panel->add_child(Row);

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

HSlider* ProceduralRockEditorPlugin::AddSliderRow(const String& Text, double Min, double Max, double Step)
{
	AddCaption(Text);

	HSlider* Slider = memnew(HSlider);
	Slider->set_min(Min);
	Slider->set_max(Max);
	Slider->set_step(Step);
	Panel->add_child(Slider);

	return Slider;
}

void ProceduralRockEditorPlugin::BuildPanel()
{
	Panel = memnew(VBoxContainer);
	Panel->set_name("Rocks");

	SelectionLabel = memnew(Label);
	Panel->add_child(SelectionLabel);

	StatsLabel = memnew(Label);
	Panel->add_child(StatsLabel);

	Panel->add_child(memnew(HSeparator));

	EditFormPicker = memnew(OptionButton);
	for (int32_t Index = 0; Index < FORM_ENTRY_COUNT; ++Index)
	{
		EditFormPicker->add_item(ProceduralRockParameters::GetFormNameLocalized(FORM_ORDER[Index]), Index);
	}
	EditFormPicker->connect("item_selected", callable_mp(this, &ProceduralRockEditorPlugin::OnFormChanged));
	Panel->add_child(EditFormPicker);

	SeedSpin = AddSpinRow("Seed", 0.0, 1000000.0, 1.0);
	SeedSpin->connect("value_changed", callable_mp(this, &ProceduralRockEditorPlugin::OnSeedChanged));

	ResolutionSpin = AddSpinRow("Resolution", 16.0, 128.0, 1.0);
	ResolutionSpin->connect("value_changed", callable_mp(this, &ProceduralRockEditorPlugin::OnResolutionChanged));

	ScaleSpin = AddSpinRow("Scale", 0.1, 20.0, 0.05);
	ScaleSpin->connect("value_changed", callable_mp(this, &ProceduralRockEditorPlugin::OnScaleChanged));

	StepsSpin = AddSpinRow("Bumps", 8.0, 72.0, 1.0);
	StepsSpin->connect("value_changed", callable_mp(this, &ProceduralRockEditorPlugin::OnStepsChanged));

	SmoothnessSlider = AddSliderRow("Smoothness", 0.01, 0.2, 0.001);
	SmoothnessSlider->connect("value_changed", callable_mp(this, &ProceduralRockEditorPlugin::OnSmoothnessChanged));

	DisplacementSlider = AddSliderRow("Roughness", 0.0, 1.0, 0.001);
	DisplacementSlider->connect("value_changed", callable_mp(this, &ProceduralRockEditorPlugin::OnDisplacementChanged));

	SpreadSlider = AddSliderRow("Roughness scale", 1.0, 10.0, 0.01);
	SpreadSlider->connect("value_changed", callable_mp(this, &ProceduralRockEditorPlugin::OnSpreadChanged));

	FlatnessSlider = AddSliderRow("Flatness", 0.3, 1.5, 0.01);
	FlatnessSlider->connect("value_changed", callable_mp(this, &ProceduralRockEditorPlugin::OnFlatnessChanged));

	RoundnessSlider = AddSliderRow("Roundness", 0.0, 1.0, 0.01);
	RoundnessSlider->connect("value_changed", callable_mp(this, &ProceduralRockEditorPlugin::OnRoundnessChanged));

	CutGroundCheck = memnew(CheckBox);
	CutGroundCheck->set_text("Cut ground");
	CutGroundCheck->connect("toggled", callable_mp(this, &ProceduralRockEditorPlugin::OnCutGroundToggled));
	Panel->add_child(CutGroundCheck);

	GroundCutSlider = AddSliderRow("Ground cut", -0.5, 0.5, 0.01);
	GroundCutSlider->connect("value_changed", callable_mp(this, &ProceduralRockEditorPlugin::OnGroundCutChanged));

	Panel->add_child(memnew(HSeparator));

	BaseColorPicker = memnew(ColorPickerButton);
	BaseColorPicker->connect("color_changed", callable_mp(this, &ProceduralRockEditorPlugin::OnBaseColorChanged));
	Panel->add_child(BaseColorPicker);

	CreviceColorPicker = memnew(ColorPickerButton);
	CreviceColorPicker->connect("color_changed", callable_mp(this, &ProceduralRockEditorPlugin::OnCreviceColorChanged));
	Panel->add_child(CreviceColorPicker);

	Button* Regenerate = memnew(Button);
	Regenerate->set_text("Regenerate");
	Regenerate->connect("pressed", callable_mp(this, &ProceduralRockEditorPlugin::OnRegeneratePressed));
	Panel->add_child(Regenerate);

	SyncFromRock();
}

void ProceduralRockEditorPlugin::SyncFromRock()
{
	if (Panel == nullptr)
	{
		return;
	}

	ProceduralRock* Rock = ResolveRock();
	if (Rock == nullptr || Rock->GetParameters().is_null())
	{
		// Reachable briefly between deselect and the dock being pulled, so keep it quiet.
		SelectionLabel->set_text("No rock selected.");
		StatsLabel->set_text("");
		return;
	}

	const Ref<ProceduralRockParameters> Parameters = Rock->GetParameters();

	SelectionLabel->set_text(Rock->get_name());
	StatsLabel->set_text(vformat(
		"%d verts / %d tris", Rock->GetVertexCount(), Rock->GetTriangleCount()));

	bSyncingWidgets = true;

	for (int32_t Index = 0; Index < FORM_ENTRY_COUNT; ++Index)
	{
		if (FORM_ORDER[Index] == Parameters->GetForm())
		{
			EditFormPicker->select(Index);
			break;
		}
	}

	SeedSpin->set_value(Parameters->GetSeed());
	ResolutionSpin->set_value(Parameters->GetResolution());
	ScaleSpin->set_value(Parameters->GetScale());
	StepsSpin->set_value(Parameters->GetSteps());
	SmoothnessSlider->set_value(Parameters->GetSmoothness());
	DisplacementSlider->set_value(Parameters->GetDisplacementScale());
	SpreadSlider->set_value(Parameters->GetDisplacementSpread());
	FlatnessSlider->set_value(Parameters->GetFlatness());
	RoundnessSlider->set_value(Parameters->GetRoundness());
	CutGroundCheck->set_pressed(Parameters->ShouldCutGround());
	GroundCutSlider->set_value(Parameters->GetGroundCut());
	BaseColorPicker->set_pick_color(Parameters->GetBaseColor());
	CreviceColorPicker->set_pick_color(Parameters->GetCreviceColor());

	// Disabling rather than hiding keeps the layout from jumping around.
	const bool bFlatForm = Parameters->GetForm() == ProceduralRockParameters::FORM_ELLIPSOID
		|| Parameters->GetForm() == ProceduralRockParameters::FORM_ROUNDED_BOX;
	FlatnessSlider->set_editable(bFlatForm);
	RoundnessSlider->set_editable(Parameters->GetForm() == ProceduralRockParameters::FORM_ROUNDED_BOX);

	bSyncingWidgets = false;
}

// ==================== Callbacks ====================

/** Applies a parameter change from a widget, then refreshes the readout. */
#define ROCK_PANEL_CALLBACK(Name, Setter, Cast)                              \
	void ProceduralRockEditorPlugin::Name(double Value)                      \
	{                                                                        \
		if (bSyncingWidgets)                                                  \
		{                                                                    \
			return;                                                          \
		}                                                                    \
		ProceduralRock* Rock = ResolveRock();                                \
		if (Rock == nullptr || Rock->GetParameters().is_null())              \
		{                                                                    \
			return;                                                          \
		}                                                                    \
		Rock->GetParameters()->Setter(Cast(Value));                          \
		SyncFromRock();                                                      \
	}

ROCK_PANEL_CALLBACK(OnSeedChanged, SetSeed, float)
ROCK_PANEL_CALLBACK(OnResolutionChanged, SetResolution, int32_t)
ROCK_PANEL_CALLBACK(OnScaleChanged, SetScale, float)
ROCK_PANEL_CALLBACK(OnStepsChanged, SetSteps, int32_t)
ROCK_PANEL_CALLBACK(OnSmoothnessChanged, SetSmoothness, float)
ROCK_PANEL_CALLBACK(OnDisplacementChanged, SetDisplacementScale, float)
ROCK_PANEL_CALLBACK(OnSpreadChanged, SetDisplacementSpread, float)
ROCK_PANEL_CALLBACK(OnFlatnessChanged, SetFlatness, float)
ROCK_PANEL_CALLBACK(OnRoundnessChanged, SetRoundness, float)
ROCK_PANEL_CALLBACK(OnGroundCutChanged, SetGroundCut, float)

#undef ROCK_PANEL_CALLBACK

void ProceduralRockEditorPlugin::OnFormChanged(int32_t)
{
	if (bSyncingWidgets)
	{
		return;
	}

	ProceduralRock* Rock = ResolveRock();
	if (Rock == nullptr || Rock->GetParameters().is_null())
	{
		return;
	}

	const Ref<ProceduralRockParameters> Parameters = Rock->GetParameters();
	Parameters->SetForm(FORM_ORDER[EditFormPicker->get_selected()]);

	SyncFromRock();
}

void ProceduralRockEditorPlugin::OnCutGroundToggled(bool bPressed)
{
	if (bSyncingWidgets)
	{
		return;
	}

	ProceduralRock* Rock = ResolveRock();
	if (Rock == nullptr || Rock->GetParameters().is_null())
	{
		return;
	}

	Rock->GetParameters()->SetCutGround(bPressed);
	SyncFromRock();
}

void ProceduralRockEditorPlugin::OnBaseColorChanged(const Color& Value)
{
	if (bSyncingWidgets)
	{
		return;
	}

	ProceduralRock* Rock = ResolveRock();
	if (Rock == nullptr || Rock->GetParameters().is_null())
	{
		return;
	}

	Rock->GetParameters()->SetBaseColor(Value);
	SyncFromRock();
}

void ProceduralRockEditorPlugin::OnCreviceColorChanged(const Color& Value)
{
	if (bSyncingWidgets)
	{
		return;
	}

	ProceduralRock* Rock = ResolveRock();
	if (Rock == nullptr || Rock->GetParameters().is_null())
	{
		return;
	}

	Rock->GetParameters()->SetCreviceColor(Value);
	SyncFromRock();
}

void ProceduralRockEditorPlugin::OnRegeneratePressed()
{
	ProceduralRock* Rock = ResolveRock();
	if (Rock == nullptr)
	{
		return;
	}

	Rock->Generate();
	SyncFromRock();
}
