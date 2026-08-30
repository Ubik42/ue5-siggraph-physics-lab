#pragma once

#include "CoreMinimal.h"

enum class EContactClothResidualReuseDecision : uint8
{
	Accepted,
	Disabled,
	NonFinite,
	TopologyChanged,
	ResidualTooLarge,
	CcdCorrectionTooLarge
};

struct FContactClothResidualReuseSettings
{
	bool bEnabled = true;
	float ForwardScale = 0.5f;
	float MaximumResidualCm = 2.0f;
	float MaximumCcdCorrectionCm = 0.25f;
};

struct FContactClothResidualReuseSample
{
	float MaximumResidualCm = 0.0f;
	float MaximumCcdCorrectionCm = 0.0f;
	uint64 CachedTopologyRevision = 0;
	uint64 CurrentTopologyRevision = 0;
};

struct FContactClothResidualReuseResult
{
	EContactClothResidualReuseDecision Decision = EContactClothResidualReuseDecision::Disabled;
	float ForwardScale = 0.0f;
	bool IsAccepted() const { return Decision == EContactClothResidualReuseDecision::Accepted; }
};

struct FContactClothResidualReuseStats
{
	int64 SamplesEvaluated = 0;
	int64 AcceptedSamples = 0;
	int64 DisabledSamples = 0;
	int64 NonFiniteFallbacks = 0;
	int64 TopologyFallbacks = 0;
	int64 ResidualMagnitudeFallbacks = 0;
	int64 CcdCorrectionFallbacks = 0;
};

/** Global per-step safety contract for the paper-inspired residual forwarding heuristic. */
class PHYSICSCONTACTCLOTHRUNTIME_API FContactClothResidualReusePolicy
{
public:
	explicit FContactClothResidualReusePolicy(const FContactClothResidualReuseSettings& InSettings = FContactClothResidualReuseSettings());

	FContactClothResidualReuseResult Evaluate(const FContactClothResidualReuseSample& Sample);
	void Reset();
	const FContactClothResidualReuseSettings& GetSettings() const { return Settings; }
	const FContactClothResidualReuseStats& GetStats() const { return Stats; }

private:
	FContactClothResidualReuseSettings Settings;
	FContactClothResidualReuseStats Stats;
};
