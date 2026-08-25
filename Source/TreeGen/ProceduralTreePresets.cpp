#include "TreeGen/ProceduralTreeParameters.h"

// Species presets. Kept apart from ProceduralTreeParameters.cpp because this is data, not
// behaviour: the first five are transcribed from TreeParameters.h in the paper's sample, the
// rest are tuned for trees common to Chinese landscapes.

using namespace godot;

String ProceduralTreeParameters::GetPresetName(int32_t Preset)
{
	switch (Preset)
	{
		case PRESET_APPLE: return "Apple";
		case PRESET_SASSAFRAS: return "Sassafras";
		case PRESET_PALM: return "Palm";
		case PRESET_TAMARACK: return "Tamarack";
		// Chinese species, with the common name in the comment for searchability.
		case PRESET_GINKGO: return "Ginkgo";       // 银杏
		case PRESET_PEACH: return "Peach";         // 桃花树
		case PRESET_CAMPHOR: return "Camphor";     // 樟树
		case PRESET_PINE: return "Pine";           // 松树
		case PRESET_CHINESE_FIR: return "Chinese Fir"; // 杉树
		case PRESET_WILLOW: return "Willow";       // 柳树
		default: return "Default";
	}
}

void ProceduralTreeParameters::ApplyPreset(int32_t Preset)
{
	EnsureSubResources();

	// Shared baseline; each preset below only overrides what makes it that species.
	bStemBirchTexture = false;
	StemSmallColor = Color(0.175f, 0.25f, 0.15f, 1.0f);
	StemBigColor = Color(0.24f, 0.2f, 0.17f, 1.0f);
	StemBumpStrength = 1.0f;
	StemBumpGapSize = 0.14f;
	StemBumpVoronoiWeight = 0.5f;
	StemLichenFrequency = 8.0f;
	StemLichenSize = 0.7f;

	const Ref<ProceduralTreeLeafParameters> Foliage[2] = { Leaf, Blossom };
	for (const Ref<ProceduralTreeLeafParameters>& Target : Foliage)
	{
		Target->SetScaleShape(3);
		Target->SetStemLen(0.01f);
		Target->SetTranslucency(0.7f);
		Target->SetSeasonOffset(0.0f);
		Target->SetCurl(0.35f);
		Target->SetColorJitter(0.12f);
		Target->SetScaleJitter(0.2f);
		Target->SetNeedleBlades(4);
		Target->SetTopConvex(false);
		Target->SetIsNeedle(false);
		Target->SetEvergreen(false);
	}

	switch (Preset)
	{
		case PRESET_APPLE:
		{
			Levels = 3;
			BaseSize = Vector4(0.15f, 0.217f, 0.0f, 0.05f);
			AttractionUp = 2.0f;
			Flare = 0.9f;
			Lobes = 0;
			LobeDepth = 0.0f;
			Scale = 4.5f;
			ScaleV = 1.0f;
			Ratio = 0.02f;
			RatioPower = 1.5f;
			Shape = Vector4i(2, 2, 4, 0);
			BaseSplits = Vector4i(0, 0, 0, 0);
			SegSplits = Vector4(0.0f, 0.474f, 0.0f, 0.0f);
			SegSplitBaseOffset = Vector4(0, 0, 0, 0);
			SplitAngle = Vector4(0.0f, 20.0f, 0.0f, 0.0f);
			SplitAngleV = Vector4(0.0f, 10.0f, 0.0f, 0.0f);
			Branches = Vector4i(0, 28, 100, 10);
			Length = Vector4(1.0f, 0.5f, 0.4f, 0.0f);
			LengthV = Vector4(0.0f, 0.0f, 0.1f, 0.0f);
			Curve = Vector4(0.0f, -20.0f, 0.0f, 0.0f);
			CurveV = Vector4(30.0f, 140.0f, 100.0f, 0.0f);
			CurveBack = Vector4(0, 0, 0, 0);
			Rotate = Vector4(0.0f, 140.0f, 140.0f, 77.0f);
			RotateV = Vector4(0, 0, 0, 0);
			DownAngle = Vector4(0.0f, 60.0f, 60.0f, 45.0f);
			DownAngleV = Vector4(0.0f, -30.0f, 20.0f, 30.0f);
			CurveRes = Vector4i(5, 10, 5, 1);
			Taper = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

			Leaf->SetCount(13);
			Leaf->SetScale(0.085f);
			Leaf->SetScaleX(0.465f);
			Leaf->SetBotAngle(-85.0f);
			Leaf->SetMidAngle(0.0f);
			Leaf->SetTopAngle(45.0f);
			Leaf->SetSideOffset(0.45f);
			Leaf->SetLobes(1);
			Leaf->SetLobeAngle(0.0f);
			Leaf->SetLobeFalloff(0.0f);
			Leaf->SetLeafColor(Color(0.0f, 0.225f, 0.0f, 1.0f));
			Leaf->SetSeasonOffset(-0.186f);

			Blossom->SetCount(10);
			Blossom->SetScale(0.0495f);
			Blossom->SetScaleX(0.612f);
			Blossom->SetBotAngle(-85.0f);
			Blossom->SetMidAngle(0.0f);
			Blossom->SetTopAngle(45.0f);
			Blossom->SetSideOffset(0.45f);
			Blossom->SetLobes(4);
			Blossom->SetLobeAngle(39.556f);
			Blossom->SetLobeFalloff(0.02f);
			Blossom->SetLeafColor(Color(0.48f, 0.35f, 0.48f, 1.0f));

			Fruit->SetChance(0.03f);
			Fruit->SetDownForce(1.0f);
			Fruit->SetSize(0.06f);
			Fruit->SetShape(Vector4(0.685f, -0.4f, 0.74f, 1.1f));
			Fruit->SetFruitColor(Color(0.29302323f, 0.1042142f, 0.040886965f, 1.0f));
			break;
		}

		case PRESET_SASSAFRAS:
		{
			Levels = 4;
			BaseSize = Vector4(0.2f, 0.05f, 0.05f, 0.05f);
			AttractionUp = 0.5f;
			Flare = 0.5f;
			Lobes = 3;
			LobeDepth = 0.05f;
			Scale = 11.4625f;
			ScaleV = 3.5f;
			Ratio = 0.02f;
			RatioPower = 1.3f;
			Shape = Vector4i(2, 0, 0, 4);
			BaseSplits = Vector4i(0, 0, 0, 0);
			SegSplits = Vector4(0, 0, 0, 0);
			SegSplitBaseOffset = Vector4(0, 0, 0, 0);
			SplitAngle = Vector4(20.0f, 0.0f, 0.0f, 0.0f);
			SplitAngleV = Vector4(5.0f, 0.0f, 0.0f, 0.0f);
			Branches = Vector4i(0, 15, 20, 30);
			Length = Vector4(1.0f, 0.4f, 0.7f, 0.4f);
			LengthV = Vector4(0, 0, 0, 0);
			Curve = Vector4(0.0f, -60.0f, -40.0f, 0.0f);
			CurveV = Vector4(10.0f, 200.0f, 300.0f, 200.0f);
			CurveBack = Vector4(0.0f, 30.0f, 0.0f, 0.0f);
			Rotate = Vector4(0.0f, 140.0f, 140.0f, 140.0f);
			RotateV = Vector4(0, 0, 0, 0);
			DownAngle = Vector4(0.0f, 90.0f, 50.0f, 45.0f);
			DownAngleV = Vector4(0.0f, -10.0f, 10.0f, 10.0f);
			CurveRes = Vector4i(16, 15, 8, 3);
			Taper = Vector4(1.05f, 1.0f, 1.0f, 1.0f);

			Leaf->SetCount(12);
			Leaf->SetScale(0.125f);
			Leaf->SetScaleX(0.286f);
			Leaf->SetBotAngle(-85.889f);
			Leaf->SetMidAngle(-40.0f);
			Leaf->SetTopAngle(90.0f);
			Leaf->SetSideOffset(0.63f);
			Leaf->SetLobes(3);
			Leaf->SetLobeAngle(32.285f);
			Leaf->SetLobeFalloff(0.123f);
			Leaf->SetLeafColor(Color(0.0f, 0.25f, 0.0f, 1.0f));
			Leaf->SetSeasonOffset(0.347f);
			Leaf->SetTopConvex(true);

			Blossom->SetCount(8);
			Blossom->SetScale(0.0505f);
			Blossom->SetScaleX(0.303f);
			Blossom->SetBotAngle(-85.0f);
			Blossom->SetMidAngle(0.0f);
			Blossom->SetTopAngle(90.0f);
			Blossom->SetSideOffset(0.45f);
			Blossom->SetLobes(5);
			Blossom->SetLobeAngle(40.0f);
			Blossom->SetLobeFalloff(0.0f);
			Blossom->SetLeafColor(Color(0.8184931f, 0.5902166f, 0.005606096f, 1.0f));
			Blossom->SetTopConvex(true);

			Fruit->SetChance(0.2f);
			Fruit->SetDownForce(0.5f);
			Fruit->SetSize(0.025f);
			Fruit->SetShape(Vector4(0.649f, 0.0f, 0.5f, 1.0f));
			Fruit->SetFruitColor(Color(0.013326132f, 0.014881321f, 0.037209332f, 1.0f));
			break;
		}

		case PRESET_PALM:
		{
			Levels = 2;
			BaseSize = Vector4(0.95f, 0.05f, 0.05f, 0.05f);
			AttractionUp = 0.0f;
			Flare = 0.0f;
			Lobes = 0;
			LobeDepth = 0.0f;
			Scale = 6.0f;
			ScaleV = 1.5f;
			Ratio = 0.015f;
			RatioPower = 2.0f;
			Shape = Vector4i(4, 4, 0, 0);
			BaseSplits = Vector4i(0, 0, 0, 0);
			SegSplits = Vector4(0, 0, 0, 0);
			SegSplitBaseOffset = Vector4(0, 0, 0, 0);
			SplitAngle = Vector4(0, 0, 0, 0);
			SplitAngleV = Vector4(0, 0, 0, 0);
			Branches = Vector4i(0, 33, 0, 0);
			Length = Vector4(1.0f, 0.4f, 0.0f, 0.0f);
			LengthV = Vector4(0.0f, 0.05f, 0.0f, 0.0f);
			Curve = Vector4(20.0f, 50.0f, 0.0f, 0.0f);
			CurveV = Vector4(10.0f, 20.0f, 0.0f, 0.0f);
			CurveBack = Vector4(-5.0f, 0.0f, 0.0f, 0.0f);
			Rotate = Vector4(0.0f, 120.0f, -120.0f, 0.0f);
			RotateV = Vector4(0.0f, 60.0f, 20.0f, 0.0f);
			DownAngle = Vector4(0.0f, 70.0f, 50.0f, 0.0f);
			DownAngleV = Vector4(0.0f, -80.0f, -50.0f, 0.0f);
			CurveRes = Vector4i(12, 9, 1, 1);
			// Trunk taper above 1 gives the palm its stacked frond scars.
			Taper = Vector4(2.1f, 1.0f, 0.0f, 0.0f);

			Leaf->SetCount(250);
			Leaf->SetScale(0.3f);
			Leaf->SetScaleX(0.06f);
			Leaf->SetBotAngle(-85.0f);
			Leaf->SetMidAngle(0.0f);
			Leaf->SetTopAngle(45.0f);
			Leaf->SetSideOffset(0.45f);
			Leaf->SetLobes(1);
			Leaf->SetLobeAngle(0.0f);
			Leaf->SetLobeFalloff(0.0f);
			Leaf->SetLeafColor(Color(0.020941045f, 0.10232556f, 0.020941045f, 1.0f));
			Leaf->SetSeasonOffset(-0.75f);
			Leaf->SetEvergreen(true);

			Blossom->SetCount(0);
			Blossom->SetScale(0.1f);
			Blossom->SetScaleX(0.5f);
			Blossom->SetLobes(5);
			Blossom->SetLobeAngle(0.0f);
			Blossom->SetLobeFalloff(0.0f);
			Blossom->SetLeafColor(Color(0.0f, 0.125f, 0.0f, 1.0f));

			Fruit->SetChance(0.0f);
			Fruit->SetDownForce(0.0f);
			Fruit->SetSize(0.1f);
			Fruit->SetShape(Vector4(0.5f, 0.333f, 0.5f, 0.666f));
			Fruit->SetFruitColor(Color(0.25f, 0.0f, 0.0f, 1.0f));
			break;
		}

		case PRESET_TAMARACK:
		{
			Levels = 3;
			BaseSize = Vector4(0.1f, 0.05f, 0.05f, 0.05f);
			AttractionUp = 0.5f;
			Flare = 0.4f;
			Lobes = 0;
			LobeDepth = 0.0f;
			Scale = 10.925f;
			ScaleV = 1.5f;
			Ratio = 0.015f;
			RatioPower = 1.3f;
			Shape = Vector4i(0, 0, 4, 0);
			BaseSplits = Vector4i(0, 0, 0, 0);
			SegSplits = Vector4(0, 0, 0, 0);
			SegSplitBaseOffset = Vector4(0, 0, 0, 0);
			SplitAngle = Vector4(0, 0, 0, 0);
			SplitAngleV = Vector4(0, 0, 0, 0);
			Branches = Vector4i(0, 75, 50, 0);
			Length = Vector4(1.0f, 0.4f, 0.2f, 0.0f);
			LengthV = Vector4(0, 0, 0, 0);
			Curve = Vector4(0.0f, -30.0f, 0.0f, 0.0f);
			CurveV = Vector4(0.0f, 120.0f, 180.0f, 0.0f);
			CurveBack = Vector4(0, 0, 0, 0);
			Rotate = Vector4(0.0f, 140.0f, 140.0f, 140.0f);
			RotateV = Vector4(0, 0, 0, 0);
			DownAngle = Vector4(0.0f, 55.0f, 45.0f, 45.0f);
			DownAngleV = Vector4(0.0f, -45.0f, 10.0f, 10.0f);
			CurveRes = Vector4i(8, 8, 8, 1);
			Taper = Vector4(0.9f, 1.0f, 1.0f, 0.0f);

			Leaf->SetCount(50);
			Leaf->SetScale(0.1f);
			Leaf->SetScaleX(0.35f);
			Leaf->SetStemLen(0.0f);
			Leaf->SetBotAngle(-85.0f);
			Leaf->SetMidAngle(0.0f);
			Leaf->SetTopAngle(45.0f);
			Leaf->SetSideOffset(0.45f);
			Leaf->SetLobes(1);
			Leaf->SetLobeAngle(0.0f);
			Leaf->SetLobeFalloff(0.0f);
			Leaf->SetLeafColor(Color(0.0f, 0.2f, 0.0f, 1.0f));
			Leaf->SetSeasonOffset(-1.0f);
			Leaf->SetIsNeedle(true);

			Blossom->SetCount(0);
			Blossom->SetScale(0.1f);
			Blossom->SetScaleX(0.5f);
			Blossom->SetStemLen(0.0f);
			Blossom->SetLobes(5);
			Blossom->SetLobeAngle(0.0f);
			Blossom->SetLobeFalloff(0.0f);
			Blossom->SetLeafColor(Color(0.0f, 0.125f, 0.0f, 1.0f));

			Fruit->SetChance(0.0f);
			Fruit->SetDownForce(0.0f);
			Fruit->SetSize(0.1f);
			Fruit->SetShape(Vector4(0.5f, 0.333f, 0.5f, 0.666f));
			Fruit->SetFruitColor(Color(0.25f, 0.0f, 0.0f, 1.0f));
			break;
		}

		case PRESET_GINKGO:
		{
			// 银杏. Columnar, sparsely and stiffly branched, with notched fan leaves that go
			// gold in autumn — the season ramp already does the gold, so the leaf stays green.
			Levels = 3;
			BaseSize = Vector4(0.28f, 0.05f, 0.05f, 0.05f);
			AttractionUp = 0.8f;
			Flare = 0.7f;
			Lobes = 0;
			LobeDepth = 0.0f;
			Scale = 12.0f;
			ScaleV = 2.5f;
			Ratio = 0.02f;
			RatioPower = 1.2f;
			// Tapered-cylindrical branch length keeps the crown narrow rather than domed.
			Shape = Vector4i(4, 0, 0, 0);
			BaseSplits = Vector4i(0, 0, 0, 0);
			SegSplits = Vector4(0, 0, 0, 0);
			SegSplitBaseOffset = Vector4(0, 0, 0, 0);
			SplitAngle = Vector4(0, 0, 0, 0);
			SplitAngleV = Vector4(0, 0, 0, 0);
			Branches = Vector4i(0, 26, 44, 0);
			Length = Vector4(1.0f, 0.42f, 0.55f, 0.0f);
			LengthV = Vector4(0.0f, 0.05f, 0.1f, 0.0f);
			Curve = Vector4(0.0f, -18.0f, 0.0f, 0.0f);
			CurveV = Vector4(18.0f, 90.0f, 130.0f, 0.0f);
			CurveBack = Vector4(0, 0, 0, 0);
			Rotate = Vector4(0.0f, 140.0f, 140.0f, 0.0f);
			RotateV = Vector4(0, 0, 0, 0);
			DownAngle = Vector4(0.0f, 48.0f, 55.0f, 0.0f);
			DownAngleV = Vector4(0.0f, -28.0f, 15.0f, 0.0f);
			CurveRes = Vector4i(10, 8, 5, 1);
			Taper = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

			// Requesting far more foliage than the budget allows is intentional: the budget
			// thins uniformly and scales the survivors up, which reads as leaf clusters.
			Leaf->SetCount(48);
			Leaf->SetScale(0.15f);
			// Wide and widest near the tip, with a concave top: a ginkgo fan.
			Leaf->SetScaleX(0.95f);
			Leaf->SetStemLen(0.02f);
			Leaf->SetBotAngle(-70.0f);
			Leaf->SetMidAngle(25.0f);
			Leaf->SetTopAngle(100.0f);
			Leaf->SetSideOffset(0.82f);
			Leaf->SetLobes(1);
			Leaf->SetLobeAngle(0.0f);
			Leaf->SetLobeFalloff(0.0f);
			Leaf->SetLeafColor(Color(0.09f, 0.33f, 0.07f, 1.0f));
			Leaf->SetSeasonOffset(0.25f);
			Leaf->SetCurl(0.2f);

			Blossom->SetCount(0);
			Blossom->SetScale(0.05f);
			Blossom->SetScaleX(0.5f);
			Blossom->SetLobes(5);
			Blossom->SetLobeAngle(0.0f);
			Blossom->SetLobeFalloff(0.0f);
			Blossom->SetLeafColor(Color(0.6f, 0.55f, 0.2f, 1.0f));

			Fruit->SetChance(0.0f);
			Fruit->SetDownForce(1.0f);
			Fruit->SetSize(0.03f);
			Fruit->SetShape(Vector4(0.6f, 0.0f, 0.6f, 1.0f));
			Fruit->SetFruitColor(Color(0.5f, 0.42f, 0.14f, 1.0f));
			break;
		}

		case PRESET_PEACH:
		{
			// 桃花树. Small, low-forking and wide-spreading, carrying far more blossom than leaf.
			Levels = 3;
			BaseSize = Vector4(0.14f, 0.2f, 0.0f, 0.05f);
			AttractionUp = 1.7f;
			Flare = 0.85f;
			Lobes = 0;
			LobeDepth = 0.0f;
			Scale = 4.2f;
			ScaleV = 0.9f;
			Ratio = 0.021f;
			RatioPower = 1.4f;
			Shape = Vector4i(2, 2, 4, 0);
			BaseSplits = Vector4i(0, 0, 0, 0);
			SegSplits = Vector4(0.0f, 0.4f, 0.0f, 0.0f);
			SegSplitBaseOffset = Vector4(0, 0, 0, 0);
			SplitAngle = Vector4(0.0f, 22.0f, 0.0f, 0.0f);
			SplitAngleV = Vector4(0.0f, 10.0f, 0.0f, 0.0f);
			Branches = Vector4i(0, 26, 80, 0);
			Length = Vector4(1.0f, 0.48f, 0.4f, 0.0f);
			LengthV = Vector4(0.0f, 0.0f, 0.1f, 0.0f);
			Curve = Vector4(0.0f, -25.0f, 0.0f, 0.0f);
			CurveV = Vector4(35.0f, 130.0f, 110.0f, 0.0f);
			CurveBack = Vector4(0, 0, 0, 0);
			Rotate = Vector4(0.0f, 140.0f, 140.0f, 77.0f);
			RotateV = Vector4(0, 0, 0, 0);
			DownAngle = Vector4(0.0f, 62.0f, 58.0f, 0.0f);
			DownAngleV = Vector4(0.0f, -32.0f, 20.0f, 0.0f);
			CurveRes = Vector4i(5, 9, 4, 1);
			Taper = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

			Leaf->SetCount(22);
			Leaf->SetScale(0.075f);
			Leaf->SetScaleX(0.26f);
			Leaf->SetBotAngle(-85.0f);
			Leaf->SetMidAngle(-10.0f);
			Leaf->SetTopAngle(62.0f);
			Leaf->SetSideOffset(0.52f);
			Leaf->SetLobes(1);
			Leaf->SetLobeAngle(0.0f);
			Leaf->SetLobeFalloff(0.0f);
			Leaf->SetLeafColor(Color(0.05f, 0.26f, 0.05f, 1.0f));
			Leaf->SetSeasonOffset(-0.15f);
			Leaf->SetCurl(0.4f);

			// Blossom outnumbers leaf 30:14, so most foliage slots become flowers.
			Blossom->SetCount(34);
			Blossom->SetScale(0.042f);
			Blossom->SetScaleX(0.66f);
			Blossom->SetBotAngle(-85.0f);
			Blossom->SetMidAngle(0.0f);
			Blossom->SetTopAngle(40.0f);
			Blossom->SetSideOffset(0.48f);
			Blossom->SetLobes(5);
			Blossom->SetLobeAngle(44.0f);
			Blossom->SetLobeFalloff(0.02f);
			Blossom->SetLeafColor(Color(0.72f, 0.3f, 0.42f, 1.0f));

			Fruit->SetChance(0.05f);
			Fruit->SetDownForce(1.0f);
			Fruit->SetSize(0.05f);
			Fruit->SetShape(Vector4(0.7f, -0.3f, 0.72f, 1.05f));
			Fruit->SetFruitColor(Color(0.42f, 0.13f, 0.09f, 1.0f));
			break;
		}

		case PRESET_CAMPHOR:
		{
			// 樟树. Large broadleaf evergreen: low fork, dense spherical crown, lobed trunk.
			// Three levels rather than four: a fourth blows past the segment cap, and the dense
			// crown is better bought with foliage count than with another branch generation.
			Levels = 3;
			BaseSize = Vector4(0.16f, 0.05f, 0.05f, 0.05f);
			AttractionUp = 0.8f;
			Flare = 0.8f;
			Lobes = 4;
			LobeDepth = 0.06f;
			Scale = 12.0f;
			ScaleV = 2.5f;
			Ratio = 0.021f;
			RatioPower = 1.3f;
			Shape = Vector4i(1, 0, 4, 0);
			// One trunk fork near the base, which is what gives camphor its broad silhouette.
			BaseSplits = Vector4i(1, 0, 0, 0);
			SegSplits = Vector4(0, 0, 0, 0);
			SegSplitBaseOffset = Vector4(0, 0, 0, 0);
			SplitAngle = Vector4(26.0f, 0.0f, 0.0f, 0.0f);
			SplitAngleV = Vector4(8.0f, 0.0f, 0.0f, 0.0f);
			Branches = Vector4i(0, 22, 74, 0);
			Length = Vector4(1.0f, 0.5f, 0.36f, 0.0f);
			LengthV = Vector4(0.0f, 0.05f, 0.1f, 0.0f);
			Curve = Vector4(0.0f, -42.0f, -20.0f, 0.0f);
			CurveV = Vector4(14.0f, 150.0f, 190.0f, 0.0f);
			CurveBack = Vector4(0, 0, 0, 0);
			Rotate = Vector4(0.0f, 140.0f, 140.0f, 140.0f);
			RotateV = Vector4(0, 0, 0, 0);
			DownAngle = Vector4(0.0f, 68.0f, 56.0f, 0.0f);
			DownAngleV = Vector4(0.0f, -22.0f, 14.0f, 0.0f);
			CurveRes = Vector4i(12, 9, 4, 1);
			Taper = Vector4(1.02f, 1.0f, 1.0f, 1.0f);

			Leaf->SetCount(24);
			Leaf->SetScale(0.16f);
			Leaf->SetScaleX(0.44f);
			Leaf->SetBotAngle(-80.0f);
			Leaf->SetMidAngle(-5.0f);
			Leaf->SetTopAngle(78.0f);
			Leaf->SetSideOffset(0.55f);
			Leaf->SetLobes(1);
			Leaf->SetLobeAngle(0.0f);
			Leaf->SetLobeFalloff(0.0f);
			Leaf->SetLeafColor(Color(0.02f, 0.19f, 0.05f, 1.0f));
			Leaf->SetTopConvex(true);
			Leaf->SetEvergreen(true);
			Leaf->SetCurl(0.45f);
			Leaf->SetColorJitter(0.16f);

			Blossom->SetCount(0);
			Blossom->SetScale(0.03f);
			Blossom->SetScaleX(0.5f);
			Blossom->SetLobes(5);
			Blossom->SetLobeAngle(30.0f);
			Blossom->SetLobeFalloff(0.0f);
			Blossom->SetLeafColor(Color(0.5f, 0.5f, 0.35f, 1.0f));
			Blossom->SetEvergreen(true);

			Fruit->SetChance(0.06f);
			Fruit->SetDownForce(0.6f);
			Fruit->SetSize(0.012f);
			Fruit->SetShape(Vector4(0.62f, 0.0f, 0.6f, 1.0f));
			Fruit->SetFruitColor(Color(0.02f, 0.02f, 0.03f, 1.0f));
			break;
		}

		case PRESET_PINE:
		{
			// 松树. Conical, branches whorled around the trunk, long needles in fascicles.
			Levels = 3;
			BaseSize = Vector4(0.18f, 0.05f, 0.05f, 0.05f);
			AttractionUp = 0.35f;
			Flare = 0.5f;
			Lobes = 0;
			LobeDepth = 0.0f;
			Scale = 13.0f;
			ScaleV = 3.0f;
			Ratio = 0.018f;
			RatioPower = 1.35f;
			Shape = Vector4i(0, 0, 4, 0);
			BaseSplits = Vector4i(0, 0, 0, 0);
			SegSplits = Vector4(0, 0, 0, 0);
			SegSplitBaseOffset = Vector4(0, 0, 0, 0);
			SplitAngle = Vector4(0, 0, 0, 0);
			SplitAngleV = Vector4(0, 0, 0, 0);
			Branches = Vector4i(0, 46, 36, 0);
			Length = Vector4(1.0f, 0.36f, 0.42f, 0.0f);
			LengthV = Vector4(0.0f, 0.06f, 0.1f, 0.0f);
			Curve = Vector4(0.0f, -12.0f, 0.0f, 0.0f);
			CurveV = Vector4(6.0f, 55.0f, 85.0f, 0.0f);
			CurveBack = Vector4(0, 0, 0, 0);
			// A rotation near 105 degrees reads as the whorled tiers pines grow in.
			Rotate = Vector4(0.0f, 105.0f, 140.0f, 0.0f);
			RotateV = Vector4(0, 0, 0, 0);
			DownAngle = Vector4(0.0f, 76.0f, 62.0f, 0.0f);
			DownAngleV = Vector4(0.0f, -18.0f, 14.0f, 0.0f);
			CurveRes = Vector4i(11, 6, 4, 1);
			Taper = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

			Leaf->SetCount(70);
			Leaf->SetScale(0.22f);
			Leaf->SetScaleX(0.24f);
			Leaf->SetStemLen(0.0f);
			Leaf->SetBotAngle(-85.0f);
			Leaf->SetMidAngle(0.0f);
			Leaf->SetTopAngle(45.0f);
			Leaf->SetSideOffset(0.45f);
			Leaf->SetLobes(1);
			Leaf->SetLobeAngle(0.0f);
			Leaf->SetLobeFalloff(0.0f);
			Leaf->SetLeafColor(Color(0.03f, 0.15f, 0.06f, 1.0f));
			Leaf->SetIsNeedle(true);
			Leaf->SetNeedleBlades(6);
			Leaf->SetScaleJitter(0.25f);

			Blossom->SetCount(0);
			Blossom->SetScale(0.05f);
			Blossom->SetScaleX(0.5f);
			Blossom->SetStemLen(0.0f);
			Blossom->SetLobes(5);
			Blossom->SetLobeAngle(0.0f);
			Blossom->SetLobeFalloff(0.0f);
			Blossom->SetLeafColor(Color(0.2f, 0.15f, 0.08f, 1.0f));

			Fruit->SetChance(0.0f);
			Fruit->SetDownForce(1.0f);
			Fruit->SetSize(0.05f);
			Fruit->SetShape(Vector4(0.5f, 0.2f, 0.55f, 0.9f));
			Fruit->SetFruitColor(Color(0.12f, 0.07f, 0.04f, 1.0f));
			break;
		}

		case PRESET_CHINESE_FIR:
		{
			// 杉树. Tall and narrowly conical, with short branches that lift at the tips.
			Levels = 3;
			BaseSize = Vector4(0.1f, 0.05f, 0.05f, 0.05f);
			AttractionUp = 0.2f;
			Flare = 0.45f;
			Lobes = 0;
			LobeDepth = 0.0f;
			Scale = 15.0f;
			ScaleV = 3.0f;
			Ratio = 0.013f;
			RatioPower = 1.45f;
			Shape = Vector4i(0, 0, 4, 0);
			BaseSplits = Vector4i(0, 0, 0, 0);
			SegSplits = Vector4(0, 0, 0, 0);
			SegSplitBaseOffset = Vector4(0, 0, 0, 0);
			SplitAngle = Vector4(0, 0, 0, 0);
			SplitAngleV = Vector4(0, 0, 0, 0);
			Branches = Vector4i(0, 62, 34, 0);
			// Short first-order branches are what keep the cone narrow.
			Length = Vector4(1.0f, 0.26f, 0.4f, 0.0f);
			LengthV = Vector4(0.0f, 0.04f, 0.1f, 0.0f);
			Curve = Vector4(0.0f, 14.0f, 0.0f, 0.0f);
			CurveV = Vector4(5.0f, 40.0f, 65.0f, 0.0f);
			CurveBack = Vector4(0, 0, 0, 0);
			Rotate = Vector4(0.0f, 120.0f, 140.0f, 0.0f);
			RotateV = Vector4(0.0f, 15.0f, 0.0f, 0.0f);
			DownAngle = Vector4(0.0f, 82.0f, 58.0f, 0.0f);
			DownAngleV = Vector4(0.0f, -14.0f, 12.0f, 0.0f);
			CurveRes = Vector4i(15, 6, 3, 1);
			Taper = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

			Leaf->SetCount(80);
			Leaf->SetScale(0.12f);
			Leaf->SetScaleX(0.3f);
			Leaf->SetStemLen(0.0f);
			Leaf->SetBotAngle(-85.0f);
			Leaf->SetMidAngle(0.0f);
			Leaf->SetTopAngle(45.0f);
			Leaf->SetSideOffset(0.45f);
			Leaf->SetLobes(1);
			Leaf->SetLobeAngle(0.0f);
			Leaf->SetLobeFalloff(0.0f);
			Leaf->SetLeafColor(Color(0.03f, 0.17f, 0.07f, 1.0f));
			Leaf->SetIsNeedle(true);
			Leaf->SetNeedleBlades(6);

			Blossom->SetCount(0);
			Blossom->SetScale(0.05f);
			Blossom->SetScaleX(0.5f);
			Blossom->SetStemLen(0.0f);
			Blossom->SetLobes(5);
			Blossom->SetLobeAngle(0.0f);
			Blossom->SetLobeFalloff(0.0f);
			Blossom->SetLeafColor(Color(0.2f, 0.15f, 0.08f, 1.0f));

			Fruit->SetChance(0.0f);
			Fruit->SetDownForce(1.0f);
			Fruit->SetSize(0.03f);
			Fruit->SetShape(Vector4(0.5f, 0.2f, 0.55f, 0.9f));
			Fruit->SetFruitColor(Color(0.12f, 0.09f, 0.05f, 1.0f));
			break;
		}

		case PRESET_WILLOW:
		{
			// 柳树. The defining trait is the pendulous whips: a strongly negative attraction
			// pulls levels 2 and 3 downwards, and they are long and finely segmented so they hang.
			// Three levels: the whips become level 2, which is also the first level AttractionUp
			// acts on, so the droop lands exactly where it is wanted.
			Levels = 3;
			// A third of the trunk stays clear, or the hanging crown swallows it entirely.
			BaseSize = Vector4(0.3f, 0.05f, 0.05f, 0.05f);
			AttractionUp = -1.9f;
			Flare = 0.7f;
			Lobes = 3;
			LobeDepth = 0.04f;
			Scale = 11.0f;
			ScaleV = 2.5f;
			Ratio = 0.024f;
			RatioPower = 1.2f;
			Shape = Vector4i(2, 0, 4, 0);
			BaseSplits = Vector4i(1, 0, 0, 0);
			SegSplits = Vector4(0, 0, 0, 0);
			SegSplitBaseOffset = Vector4(0, 0, 0, 0);
			SplitAngle = Vector4(20.0f, 0.0f, 0.0f, 0.0f);
			SplitAngleV = Vector4(6.0f, 0.0f, 0.0f, 0.0f);
			// Fewer whips, so each one gets its full share of the leaf budget and reads as
			// clothed rather than as bare wire.
			Branches = Vector4i(0, 18, 26, 0);
			Length = Vector4(1.0f, 0.45f, 0.45f, 0.0f);
			LengthV = Vector4(0.0f, 0.05f, 0.12f, 0.0f);
			// Positive curve bends the whips over and down along their own length.
			Curve = Vector4(0.0f, 30.0f, 95.0f, 0.0f);
			CurveV = Vector4(16.0f, 60.0f, 70.0f, 0.0f);
			CurveBack = Vector4(0, 0, 0, 0);
			Rotate = Vector4(0.0f, 140.0f, 140.0f, 140.0f);
			RotateV = Vector4(0, 0, 0, 0);
			DownAngle = Vector4(0.0f, 40.0f, 85.0f, 0.0f);
			DownAngleV = Vector4(0.0f, -20.0f, 14.0f, 0.0f);
			CurveRes = Vector4i(10, 8, 9, 1);
			Taper = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

			Leaf->SetCount(26);
			Leaf->SetScale(0.16f);
			// Long and very narrow: willow's lanceolate leaf.
			Leaf->SetScaleX(0.13f);
			Leaf->SetBotAngle(-88.0f);
			Leaf->SetMidAngle(-8.0f);
			Leaf->SetTopAngle(72.0f);
			Leaf->SetSideOffset(0.5f);
			Leaf->SetLobes(1);
			Leaf->SetLobeAngle(0.0f);
			Leaf->SetLobeFalloff(0.0f);
			Leaf->SetLeafColor(Color(0.1f, 0.32f, 0.09f, 1.0f));
			Leaf->SetSeasonOffset(0.1f);
			Leaf->SetCurl(0.25f);

			Blossom->SetCount(0);
			Blossom->SetScale(0.03f);
			Blossom->SetScaleX(0.4f);
			Blossom->SetLobes(5);
			Blossom->SetLobeAngle(0.0f);
			Blossom->SetLobeFalloff(0.0f);
			Blossom->SetLeafColor(Color(0.45f, 0.45f, 0.2f, 1.0f));

			Fruit->SetChance(0.0f);
			Fruit->SetDownForce(1.0f);
			Fruit->SetSize(0.02f);
			Fruit->SetShape(Vector4(0.5f, 0.2f, 0.55f, 0.9f));
			Fruit->SetFruitColor(Color(0.2f, 0.18f, 0.08f, 1.0f));
			break;
		}

		default:
		{
			Levels = 3;
			BaseSize = Vector4(0.25f, 0.05f, 0.05f, 0.05f);
			AttractionUp = 0.0f;
			Flare = 0.5f;
			Lobes = 0;
			LobeDepth = 0.0f;
			Scale = 10.0f;
			ScaleV = 0.0f;
			Ratio = 0.05f;
			RatioPower = 1.0f;
			Shape = Vector4i(0, 0, 0, 0);
			BaseSplits = Vector4i(0, 0, 0, 0);
			SegSplits = Vector4(0, 0, 0, 0);
			SegSplitBaseOffset = Vector4(0, 0, 0, 0);
			SplitAngle = Vector4(0, 0, 0, 0);
			SplitAngleV = Vector4(0, 0, 0, 0);
			Branches = Vector4i(1, 10, 5, 0);
			Length = Vector4(1.0f, 0.5f, 0.5f, 0.0f);
			LengthV = Vector4(0, 0, 0, 0);
			Curve = Vector4(0, 0, 0, 0);
			CurveV = Vector4(0, 0, 0, 0);
			CurveBack = Vector4(0, 0, 0, 0);
			Rotate = Vector4(0.0f, 120.0f, 120.0f, 120.0f);
			RotateV = Vector4(0, 0, 0, 0);
			DownAngle = Vector4(0.0f, 30.0f, 30.0f, 30.0f);
			DownAngleV = Vector4(0, 0, 0, 0);
			CurveRes = Vector4i(3, 3, 1, 0);
			Taper = Vector4(1.0f, 1.0f, 1.0f, 0.0f);

			Leaf->SetCount(100);
			Leaf->SetScale(0.2f);
			Leaf->SetScaleX(0.5f);
			Leaf->SetStemLen(0.5f);
			Leaf->SetBotAngle(-85.0f);
			Leaf->SetMidAngle(0.0f);
			Leaf->SetTopAngle(45.0f);
			Leaf->SetSideOffset(0.45f);
			Leaf->SetLobes(1);
			Leaf->SetLobeAngle(0.0f);
			Leaf->SetLobeFalloff(0.0f);
			Leaf->SetLeafColor(Color(0.0f, 0.125f, 0.0f, 1.0f));

			Blossom->SetCount(0);
			Blossom->SetScale(0.2f);
			Blossom->SetScaleX(0.5f);
			Blossom->SetStemLen(0.5f);
			Blossom->SetLobes(5);
			Blossom->SetLobeAngle(0.0f);
			Blossom->SetLobeFalloff(0.0f);
			Blossom->SetLeafColor(Color(0.0f, 0.125f, 0.0f, 1.0f));

			Fruit->SetChance(0.0f);
			Fruit->SetDownForce(1.0f);
			Fruit->SetSize(0.1f);
			Fruit->SetShape(Vector4(0.5f, 0.333f, 0.5f, 0.666f));
			Fruit->SetFruitColor(Color(0.25f, 0.0f, 0.0f, 1.0f));
			break;
		}
	}

	emit_changed();
}
