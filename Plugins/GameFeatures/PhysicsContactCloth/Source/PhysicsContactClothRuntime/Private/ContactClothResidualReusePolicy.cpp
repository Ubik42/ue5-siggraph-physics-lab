#include "ContactClothResidualReusePolicy.h"

FContactClothResidualReusePolicy::FContactClothResidualReusePolicy(const FContactClothResidualReuseSettings& InSettings)
	: Settings(InSettings)
{
	Settings.ForwardScale = FMath::Clamp(Settings.ForwardScale, 0.0f, 1.0f);
	Settings.MaximumResidualCm = FMath::Max(Settings.MaximumResidualCm, 0.0f);
	Settings.MaximumCcdCorrectionCm = FMath::Max(Settings.MaximumCcdCorrectionCm, 0.0f);
}

FContactClothResidualReuseResult FContactClothResidualReusePolicy::Evaluate(const FContactClothResidualReuseSample& Sample)
{
	Stats.SamplesEvaluated++;
	FContactClothResidualReuseResult Result;
	if (!Settings.bEnabled || Settings.ForwardScale <= 0.0f)
	{
		Result.Decision = EContactClothResidualReuseDecision::Disabled;
		Stats.DisabledSamples++;
		return Result;
	}
	if (!FMath::IsFinite(Sample.MaximumResidualCm) || !FMath::IsFinite(Sample.MaximumCcdCorrectionCm))
	{
		Result.Decision = EContactClothResidualReuseDecision::NonFinite;
		Stats.NonFiniteFallbacks++;
		return Result;
	}
	if (Sample.CachedTopologyRevision != Sample.CurrentTopologyRevision)
	{
		Result.Decision = EContactClothResidualReuseDecision::TopologyChanged;
		Stats.TopologyFallbacks++;
		return Result;
	}
	if (Sample.MaximumResidualCm > Settings.MaximumResidualCm)
	{
		Result.Decision = EContactClothResidualReuseDecision::ResidualTooLarge;
		Stats.ResidualMagnitudeFallbacks++;
		return Result;
	}
	if (Sample.MaximumCcdCorrectionCm > Settings.MaximumCcdCorrectionCm)
	{
		Result.Decision = EContactClothResidualReuseDecision::CcdCorrectionTooLarge;
		Stats.CcdCorrectionFallbacks++;
		return Result;
	}
	Result.Decision = EContactClothResidualReuseDecision::Accepted;
	Result.ForwardScale = Settings.ForwardScale;
	Stats.AcceptedSamples++;
	return Result;
}

void FContactClothResidualReusePolicy::Reset()
{
	Stats = FContactClothResidualReuseStats();
}
