#include "VerticalLandingExperiment.h"

namespace
{
bool Validate(const FVerticalLandingSettings& Settings)
{
    return Settings.FixedDeltaSeconds > 0.0 && Settings.MaximumSteps > 0
        && Settings.MassKilograms > 0.0 && Settings.GravityMetersPerSecondSquared > 0.0
        && Settings.LegRestHeightMeters > 0.0 && Settings.InitialHeightMeters >= Settings.LegRestHeightMeters
        && Settings.MaximumCompressionMeters > 0.0 && Settings.MaximumCompressionMeters < Settings.LegRestHeightMeters
        && Settings.LegStiffnessNewtonsPerMeter > 0.0 && Settings.LegDampingNewtonSecondsPerMeter >= 0.0
        && Settings.MaximumGroundForceNewtons > 0.0 && Settings.SettlePositionToleranceMeters > 0.0
        && Settings.SettleSpeedMetersPerSecond > 0.0 && Settings.SettleHoldSeconds >= 0.0;
}
}

FVerticalLandingResult FVerticalLandingExperiment::Simulate(const FVerticalLandingSettings& Settings)
{
    FVerticalLandingResult Result;
    if (!Validate(Settings)) return Result;

    Result.bValid = true;
    Result.Samples.Reserve(Settings.MaximumSteps);
    EVerticalLandingPhase Phase = EVerticalLandingPhase::Airborne;
    double Height = Settings.InitialHeightMeters;
    double Velocity = Settings.InitialVerticalVelocityMetersPerSecond;
    double SettleTimer = 0.0;
    bool bInContact = false;
    const double EquilibriumHeight = Settings.LegRestHeightMeters
        - Settings.MassKilograms * Settings.GravityMetersPerSecondSquared / Settings.LegStiffnessNewtonsPerMeter;

    for (int32 Step = 0; Step < Settings.MaximumSteps; ++Step)
    {
        const double Time = Step * Settings.FixedDeltaSeconds;
        const double Compression = FMath::Max(0.0, Settings.LegRestHeightMeters - Height);
        double GroundForce = 0.0;
        if (Compression > 0.0)
        {
            const double SpringForce = Settings.LegStiffnessNewtonsPerMeter * Compression;
            const double DampingForce = -Settings.LegDampingNewtonSecondsPerMeter * Velocity;
            GroundForce = FMath::Clamp(SpringForce + DampingForce, 0.0, Settings.MaximumGroundForceNewtons);
            if (!bInContact)
            {
                bInContact = true;
                ++Result.ContactCount;
                Result.bLanded = true;
                Result.FirstContactTimeSeconds = Time;
                Result.ImpactSpeedMetersPerSecond = FMath::Max(0.0, -Velocity);
                Phase = EVerticalLandingPhase::Absorbing;
            }
        }
        else if (Velocity > 0.0)
        {
            bInContact = false;
        }

        const double Acceleration = GroundForce / Settings.MassKilograms - Settings.GravityMetersPerSecondSquared;
        Velocity += Acceleration * Settings.FixedDeltaSeconds;
        Height += Velocity * Settings.FixedDeltaSeconds;

        const double PostStepCompression = FMath::Max(0.0, Settings.LegRestHeightMeters - Height);
        Result.PeakGroundForceNewtons = FMath::Max(Result.PeakGroundForceNewtons, GroundForce);
        Result.MaximumCompressionMeters = FMath::Max(Result.MaximumCompressionMeters, PostStepCompression);
        Result.GroundImpulseNewtonSeconds += GroundForce * Settings.FixedDeltaSeconds;
        if (Compression > 0.0)
        {
            Result.DampingDissipationJoules += Settings.LegDampingNewtonSecondsPerMeter * Velocity * Velocity * Settings.FixedDeltaSeconds;
        }

        if (PostStepCompression > Settings.MaximumCompressionMeters)
        {
            Phase = EVerticalLandingPhase::Failed;
            Result.bFailed = true;
        }
        else if (Result.bLanded && FMath::Abs(Height - EquilibriumHeight) <= Settings.SettlePositionToleranceMeters
            && FMath::Abs(Velocity) <= Settings.SettleSpeedMetersPerSecond)
        {
            SettleTimer += Settings.FixedDeltaSeconds;
            if (SettleTimer >= Settings.SettleHoldSeconds)
            {
                Phase = EVerticalLandingPhase::Settled;
                Result.bSettled = true;
                Result.SettleTimeSeconds = Time - Result.FirstContactTimeSeconds;
            }
        }
        else
        {
            SettleTimer = 0.0;
        }

        Result.Samples.Add({Step, Time, Height, Velocity, GroundForce, PostStepCompression, Phase});
        if (!FMath::IsFinite(Height) || !FMath::IsFinite(Velocity) || !FMath::IsFinite(GroundForce))
        {
            Phase = EVerticalLandingPhase::Failed;
            Result.bFailed = true;
        }
        if (Result.bSettled || Result.bFailed) break;
    }

    Result.FinalHeightMeters = Height;
    Result.FinalSpeedMetersPerSecond = Velocity;
    return Result;
}

const TCHAR* FVerticalLandingExperiment::PhaseName(const EVerticalLandingPhase Phase)
{
    switch (Phase)
    {
    case EVerticalLandingPhase::Airborne: return TEXT("Airborne");
    case EVerticalLandingPhase::Absorbing: return TEXT("Absorbing");
    case EVerticalLandingPhase::Settled: return TEXT("Settled");
    case EVerticalLandingPhase::Failed: return TEXT("Failed");
    default: return TEXT("Unknown");
    }
}
