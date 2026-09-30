#pragma once

#include "SlowTree/SlowTreeGrowth.h"
#include <godot_cpp/classes/curve.hpp>
#include <godot_cpp/classes/resource.hpp>

namespace godot
{
/** Optional species-profile overrides. Null curves keep the built-in species profile. */
class ProceduralTreeGrowthParameters : public Resource
{
	GDCLASS(ProceduralTreeGrowthParameters, Resource)

private:
	float TrunkBend = 1.0f;
	float BranchBend = 1.0f;
	float Forking = 1.0f;
	float RadiusPower = 2.35f;
	float JunctionShape = 1.0f;
	float BambooInternodeLength = 0.28f;
	float BambooNodeDefinition = 1.0f;
	float BambooLeafScale = 1.0f;
	float PeachTwigDensity = 1.0f;
	float PeachBlossomDensity = 1.0f;
	float PeachBlossomScale = 1.0f;
	Ref<Curve> LengthByHeight;
	Ref<Curve> DensityByHeight;
	Ref<Curve> BendAlongBranch;
	Ref<Curve> RadiusAlongBranch;
	void OnCurveChanged();
	void ChangeCurve(Ref<Curve>& Target, const Ref<Curve>& Value);

protected:
	static void _bind_methods();

public:
	TreeGrowthSettings MakeSnapshot() const;
	void SetTrunkBend(float Value);
	float GetTrunkBend() const
	{
		return TrunkBend;
	}
	void SetBranchBend(float Value);
	float GetBranchBend() const
	{
		return BranchBend;
	}
	void SetForking(float Value);
	float GetForking() const
	{
		return Forking;
	}
	void SetRadiusPower(float Value);
	float GetRadiusPower() const
	{
		return RadiusPower;
	}
	void SetJunctionShape(float Value);
	float GetJunctionShape() const
	{
		return JunctionShape;
	}
	void SetLengthByHeight(const Ref<Curve>& Value);
	void SetBambooInternodeLength(float Value);
	float GetBambooInternodeLength() const
	{
		return BambooInternodeLength;
	}
	void SetBambooNodeDefinition(float Value);
	float GetBambooNodeDefinition() const
	{
		return BambooNodeDefinition;
	}
	void SetBambooLeafScale(float Value);
	float GetBambooLeafScale() const
	{
		return BambooLeafScale;
	}
	Ref<Curve> GetLengthByHeight() const
	{
		return LengthByHeight;
	}
	void SetPeachTwigDensity(float Value);
	float GetPeachTwigDensity() const
	{
		return PeachTwigDensity;
	}
	void SetPeachBlossomDensity(float Value);
	float GetPeachBlossomDensity() const
	{
		return PeachBlossomDensity;
	}
	void SetPeachBlossomScale(float Value);
	float GetPeachBlossomScale() const
	{
		return PeachBlossomScale;
	}
	void SetDensityByHeight(const Ref<Curve>& Value);
	Ref<Curve> GetDensityByHeight() const
	{
		return DensityByHeight;
	}
	void SetBendAlongBranch(const Ref<Curve>& Value);
	Ref<Curve> GetBendAlongBranch() const
	{
		return BendAlongBranch;
	}
	void SetRadiusAlongBranch(const Ref<Curve>& Value);
	Ref<Curve> GetRadiusAlongBranch() const
	{
		return RadiusAlongBranch;
	}
};
} // namespace godot
