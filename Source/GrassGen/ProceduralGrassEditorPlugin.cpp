#include "GrassGen/ProceduralGrassEditorPlugin.h"

#include "GrassGen/ProceduralGrass.h"
#include "GrassGen/ProceduralGrassParameters.h"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/h_separator.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

using namespace godot;

namespace
{
	const int32_t SPECIES_ORDER[] = {
		ProceduralGrassParameters::SPECIES_THATCH,
		ProceduralGrassParameters::SPECIES_FOXTAIL,
		ProceduralGrassParameters::SPECIES_SHORT,
		ProceduralGrassParameters::SPECIES_WEED,
	};

	const int32_t SPECIES_ENTRY_COUNT = int32_t(sizeof(SPECIES_ORDER) / sizeof(SPECIES_ORDER[0]));
} // namespace

void ProceduralGrassEditorPlugin::_bind_methods()
{
}

// ==================== Lifecycle ====================

void ProceduralGrassEditorPlugin::_enter_tree()
{
	BuildPanel();
	add_control_to_container(CONTAINER_SPATIAL_EDITOR_SIDE_RIGHT, Panel);
	// Hidden until the editor tells us one of our nodes is being edited.
	Panel->set_visible(false);
}

void ProceduralGrassEditorPlugin::_exit_tree()
{
	if (Panel != nullptr)
	{
		remove_control_from_container(CONTAINER_SPATIAL_EDITOR_SIDE_RIGHT, Panel);
		Panel->queue_free();
		Panel = nullptr;
	}
}

void ProceduralGrassEditorPlugin::SetPanelVisible(bool bVisible)
{
	if (Panel == nullptr || bVisible == bPanelVisible)
	{
		return;
	}

	Panel->set_visible(bVisible);
	bPanelVisible = bVisible;
}

void ProceduralGrassEditorPlugin::_make_visible(bool bVisible)
{
	SetPanelVisible(bVisible);
}

String ProceduralGrassEditorPlugin::_get_plugin_name() const
{
	return "Procedural Grass";
}

bool ProceduralGrassEditorPlugin::_handles(Object* Target) const
{
	return Object::cast_to<ProceduralGrass>(Target) != nullptr;
}

void ProceduralGrassEditorPlugin::_edit(Object* Target)
{
	ProceduralGrass* Grass = Object::cast_to<ProceduralGrass>(Target);
	EditedGrassId = (Grass != nullptr) ? Grass->get_instance_id() : ObjectID();

	SyncFromGrass();
}

ProceduralGrass* ProceduralGrassEditorPlugin::ResolveGrass() const
{
	if (!EditedGrassId.is_valid())
	{
		return nullptr;
	}

	// The node may have been deleted since selection, so always re-resolve.
	return Object::cast_to<ProceduralGrass>(ObjectDB::get_instance(EditedGrassId));
}

// ==================== Panel ====================

Label* ProceduralGrassEditorPlugin::AddCaption(const String& Text) const
{
	Label* Caption = memnew(Label);
	Caption->set_text(Text);
	Panel->add_child(Caption);

	return Caption;
}

SpinBox* ProceduralGrassEditorPlugin::AddSpinRow(const String& Text, double Min, double Max, double Step)
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

HSlider* ProceduralGrassEditorPlugin::AddSliderRow(const String& Text, double Min, double Max, double Step)
{
	AddCaption(Text);

	HSlider* Slider = memnew(HSlider);
	Slider->set_min(Min);
	Slider->set_max(Max);
	Slider->set_step(Step);
	Panel->add_child(Slider);

	return Slider;
}

void ProceduralGrassEditorPlugin::BuildPanel()
{
	Panel = memnew(VBoxContainer);
	Panel->set_name("Grass");

	SelectionLabel = memnew(Label);
	Panel->add_child(SelectionLabel);

	StatsLabel = memnew(Label);
	Panel->add_child(StatsLabel);

	Panel->add_child(memnew(HSeparator));

	EditSpeciesPicker = memnew(OptionButton);
	for (int32_t Index = 0; Index < SPECIES_ENTRY_COUNT; ++Index)
	{
		EditSpeciesPicker->add_item(ProceduralGrassParameters::GetSpeciesNameLocalized(SPECIES_ORDER[Index]), Index);
	}
	EditSpeciesPicker->connect("item_selected", callable_mp(this, &ProceduralGrassEditorPlugin::OnSpeciesChanged));
	Panel->add_child(EditSpeciesPicker);

	SeedSpin = AddSpinRow("Seed", 0.0, 1000000.0, 1.0);
	SeedSpin->connect("value_changed", callable_mp(this, &ProceduralGrassEditorPlugin::OnSeedChanged));

	ScaleSpin = AddSpinRow("Scale", 0.1, 5.0, 0.05);
	ScaleSpin->connect("value_changed", callable_mp(this, &ProceduralGrassEditorPlugin::OnScaleChanged));

	ClumpRadiusSpin = AddSpinRow("Clump radius", 0.02, 1.0, 0.01);
	ClumpRadiusSpin->connect("value_changed", callable_mp(this, &ProceduralGrassEditorPlugin::OnClumpRadiusChanged));

	BladeCountSpin = AddSpinRow("Blade count", 0.0, 60.0, 1.0);
	BladeCountSpin->connect("value_changed", callable_mp(this, &ProceduralGrassEditorPlugin::OnBladeCountChanged));

	CurvatureSlider = AddSliderRow("Curvature", 0.0, 3.0, 0.01);
	CurvatureSlider->connect("value_changed", callable_mp(this, &ProceduralGrassEditorPlugin::OnCurvatureChanged));

	LeanAngleSlider = AddSliderRow("Lean angle", 0.0, 60.0, 0.5);
	LeanAngleSlider->connect("value_changed", callable_mp(this, &ProceduralGrassEditorPlugin::OnLeanAngleChanged));

	LeanAzimuthSlider = AddSliderRow("Lean azimuth", 0.0, 360.0, 1.0);
	LeanAzimuthSlider->connect("value_changed", callable_mp(this, &ProceduralGrassEditorPlugin::OnLeanAzimuthChanged));

	Panel->add_child(memnew(HSeparator));

	ColorVarianceSlider = AddSliderRow("Color variance", 0.0, 1.0, 0.01);
	ColorVarianceSlider->connect("value_changed", callable_mp(this, &ProceduralGrassEditorPlugin::OnColorVarianceChanged));

	UseSpeciesColorsCheck = memnew(CheckBox);
	UseSpeciesColorsCheck->set_text("Use species colors");
	UseSpeciesColorsCheck->connect("toggled", callable_mp(this, &ProceduralGrassEditorPlugin::OnUseSpeciesColorsToggled));
	Panel->add_child(UseSpeciesColorsCheck);

	BaseColorPicker = memnew(ColorPickerButton);
	BaseColorPicker->connect("color_changed", callable_mp(this, &ProceduralGrassEditorPlugin::OnBaseColorChanged));
	Panel->add_child(BaseColorPicker);

	TipColorPicker = memnew(ColorPickerButton);
	TipColorPicker->connect("color_changed", callable_mp(this, &ProceduralGrassEditorPlugin::OnTipColorChanged));
	Panel->add_child(TipColorPicker);

	Button* Regenerate = memnew(Button);
	Regenerate->set_text("Regenerate");
	Regenerate->connect("pressed", callable_mp(this, &ProceduralGrassEditorPlugin::OnRegeneratePressed));
	Panel->add_child(Regenerate);

	SyncFromGrass();
}

void ProceduralGrassEditorPlugin::SyncFromGrass()
{
	if (Panel == nullptr)
	{
		return;
	}

	ProceduralGrass* Grass = ResolveGrass();
	if (Grass == nullptr || Grass->GetParameters().is_null())
	{
		// Reachable briefly between deselect and the dock being pulled, so keep it quiet.
		SelectionLabel->set_text("No grass clump selected.");
		StatsLabel->set_text("");
		return;
	}

	const Ref<ProceduralGrassParameters> Parameters = Grass->GetParameters();

	SelectionLabel->set_text(Grass->get_name());
	StatsLabel->set_text(vformat(
		"%d verts / %d tris", Grass->GetVertexCount(), Grass->GetTriangleCount()));

	bSyncingWidgets = true;

	for (int32_t Index = 0; Index < SPECIES_ENTRY_COUNT; ++Index)
	{
		if (SPECIES_ORDER[Index] == Parameters->GetSpecies())
		{
			EditSpeciesPicker->select(Index);
			break;
		}
	}

	SeedSpin->set_value(Parameters->GetSeed());
	ScaleSpin->set_value(Parameters->GetScale());
	ClumpRadiusSpin->set_value(Parameters->GetClumpRadius());
	BladeCountSpin->set_value(Parameters->GetBladeCount());
	CurvatureSlider->set_value(Parameters->GetCurvature());
	LeanAngleSlider->set_value(Parameters->GetLeanAngle());
	LeanAzimuthSlider->set_value(Parameters->GetLeanAzimuth());
	ColorVarianceSlider->set_value(Parameters->GetColorVariance());
	UseSpeciesColorsCheck->set_pressed(Parameters->ShouldUseSpeciesColors());
	BaseColorPicker->set_pick_color(Parameters->GetBaseColor());
	TipColorPicker->set_pick_color(Parameters->GetTipColor());

	// Disabling rather than hiding keeps the layout from jumping around.
	const bool bManualColors = !Parameters->ShouldUseSpeciesColors();
	BaseColorPicker->set_disabled(!bManualColors);
	TipColorPicker->set_disabled(!bManualColors);
	LeanAzimuthSlider->set_editable(Parameters->GetLeanAngle() > 0.01f);

	bSyncingWidgets = false;
}

// ==================== Callbacks ====================

/** Applies a parameter change from a widget, then refreshes the readout. */
#define GRASS_PANEL_CALLBACK(Name, Setter, Cast)                             \
	void ProceduralGrassEditorPlugin::Name(double Value)                     \
	{                                                                        \
		if (bSyncingWidgets)                                                  \
		{                                                                    \
			return;                                                          \
		}                                                                    \
		ProceduralGrass* Grass = ResolveGrass();                             \
		if (Grass == nullptr || Grass->GetParameters().is_null())            \
		{                                                                    \
			return;                                                          \
		}                                                                    \
		Grass->GetParameters()->Setter(Cast(Value));                         \
		SyncFromGrass();                                                     \
	}

GRASS_PANEL_CALLBACK(OnSeedChanged, SetSeed, float)
GRASS_PANEL_CALLBACK(OnScaleChanged, SetScale, float)
GRASS_PANEL_CALLBACK(OnClumpRadiusChanged, SetClumpRadius, float)
GRASS_PANEL_CALLBACK(OnBladeCountChanged, SetBladeCount, int32_t)
GRASS_PANEL_CALLBACK(OnCurvatureChanged, SetCurvature, float)
GRASS_PANEL_CALLBACK(OnLeanAngleChanged, SetLeanAngle, float)
GRASS_PANEL_CALLBACK(OnLeanAzimuthChanged, SetLeanAzimuth, float)
GRASS_PANEL_CALLBACK(OnColorVarianceChanged, SetColorVariance, float)

#undef GRASS_PANEL_CALLBACK

void ProceduralGrassEditorPlugin::OnSpeciesChanged(int32_t)
{
	if (bSyncingWidgets)
	{
		return;
	}

	ProceduralGrass* Grass = ResolveGrass();
	if (Grass == nullptr || Grass->GetParameters().is_null())
	{
		return;
	}

	const Ref<ProceduralGrassParameters> Parameters = Grass->GetParameters();
	Parameters->SetSpecies(SPECIES_ORDER[EditSpeciesPicker->get_selected()]);

	SyncFromGrass();
}

void ProceduralGrassEditorPlugin::OnUseSpeciesColorsToggled(bool bPressed)
{
	if (bSyncingWidgets)
	{
		return;
	}

	ProceduralGrass* Grass = ResolveGrass();
	if (Grass == nullptr || Grass->GetParameters().is_null())
	{
		return;
	}

	Grass->GetParameters()->SetUseSpeciesColors(bPressed);
	SyncFromGrass();
}

void ProceduralGrassEditorPlugin::OnBaseColorChanged(const Color& Value)
{
	if (bSyncingWidgets)
	{
		return;
	}

	ProceduralGrass* Grass = ResolveGrass();
	if (Grass == nullptr || Grass->GetParameters().is_null())
	{
		return;
	}

	Grass->GetParameters()->SetBaseColor(Value);
	SyncFromGrass();
}

void ProceduralGrassEditorPlugin::OnTipColorChanged(const Color& Value)
{
	if (bSyncingWidgets)
	{
		return;
	}

	ProceduralGrass* Grass = ResolveGrass();
	if (Grass == nullptr || Grass->GetParameters().is_null())
	{
		return;
	}

	Grass->GetParameters()->SetTipColor(Value);
	SyncFromGrass();
}

void ProceduralGrassEditorPlugin::OnRegeneratePressed()
{
	ProceduralGrass* Grass = ResolveGrass();
	if (Grass == nullptr)
	{
		return;
	}

	Grass->Generate();
	SyncFromGrass();
}
