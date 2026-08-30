#pragma once

#include "CoreMinimal.h"

enum class EVerticalLandingPhase : uint8
{
    Airborne,
    Absorbing,
    Settled,
    Failed
};

struct PHYSICSDRIVENCHARACTERRUNTIME_API FVerticalLandingSettings
{
    double FixedDeltaSeconds = 1.0 / 1000.0;
    int32 MaximumSteps = 4000;
    double MassKilograms = 75.0;
    double GravityMetersPerSecondSquared = 9.81;
    double LegRestHeightMeters = 0.9;
    double InitialHeightMeters = 1.5;
    double InitialVerticalVelocityMetersPerSecond = 0.0;
    double MaximumCompressionMeters = 0.4;
    double LegStiffnessNewtonsPerMeter = 40000.0;
    double LegDampingNewtonSecondsPerMeter = 3500.0;
    double MaximumGroundForceNewtons = 20000.0;
    double SettlePositionToleranceMeters = 0.01;
    double SettleSpeedMetersPerSecond = 0.05;
    double SettleHoldSeconds = 0.2;
};

struct PHYSICSDRIVENCHARACTERRUNTIME_API FVerticalLandingSample
{
    int32 Step = 0;
    double TimeSeconds = 0.0;
    double HeightMeters = 0.0;
    double VerticalVelocityMetersPerSecond = 0.0;
    double GroundForceNewtons = 0.0;
    double CompressionMeters = 0.0;
    EVerticalLandingPhase Phase = EVerticalLandingPhase::Airborne;
};

struct PHYSICSDRIVENCHARACTERRUNTIME_API FVerticalLandingResult
{
    bool bValid = false;
    bool bLanded = false;
    bool bSettled = false;
    bool bFailed = false;
    int32 ContactCount = 0;
    double FirstContactTimeSeconds = -1.0;
    double ImpactSpeedMetersPerSecond = 0.0;
    double SettleTimeSeconds = -1.0;
    double PeakGroundForceNewtons = 0.0;
    double MaximumCompressionMeters = 0.0;
    double GroundImpulseNewtonSeconds = 0.0;
    double DampingDissipationJoules = 0.0;
    double FinalHeightMeters = 0.0;
    double FinalSpeedMetersPerSecond = 0.0;
    TArray<FVerticalLandingSample> Samples;
};

class PHYSICSDRIVENCHARACTERRUNTIME_API FVerticalLandingExperiment
{
public:
    static FVerticalLandingResult Simulate(const FVerticalLandingSettings& Settings);
    static const TCHAR* PhaseName(EVerticalLandingPhase Phase);
};
