#include "PlanarCharacterRecoveryExperiment.h"

namespace
{
bool Validate(const FPlanarCharacterRecoverySettings& Settings)
{
    return Settings.FixedDeltaSeconds > 0.0 && Settings.MaximumSteps > 0 && Settings.PushStep >= 0
        && Settings.PushStep < Settings.MaximumSteps && Settings.MassKilograms > 0.0
        && Settings.CenterOfMassHeightMeters > 0.0 && Settings.InertiaKilogramMetersSquared > 0.0
        && Settings.GravityMetersPerSecondSquared > 0.0 && Settings.SupportHalfWidthMeters > 0.0
        && Settings.GroundAngleRadians > 0.0 && Settings.GroundRestitution >= 0.0 && Settings.GroundRestitution < 1.0
        && Settings.MaximumTorqueNewtonMeters > 0.0 && Settings.SettleHoldSeconds >= 0.0
        && Settings.RecoveryTrajectorySeconds > 0.0 && Settings.FailureTimeoutSeconds > 0.0;
}

double SmoothStep(const double Alpha)
{
    const double Clamped = FMath::Clamp(Alpha, 0.0, 1.0);
    return Clamped * Clamped * (3.0 - 2.0 * Clamped);
}
}

FPlanarCharacterRecoveryResult FPlanarCharacterRecoveryExperiment::Simulate(const FPlanarCharacterRecoverySettings& Settings)
{
    FPlanarCharacterRecoveryResult Result;
    if (!Validate(Settings)) return Result;

    Result.bValid = true;
    Result.MinimumSupportMarginMeters = Settings.SupportHalfWidthMeters;
    Result.Samples.Reserve(Settings.MaximumSteps);

    EPlanarCharacterRecoveryPhase Phase = EPlanarCharacterRecoveryPhase::Balanced;
    double Angle = 0.0;
    double AngularVelocity = 0.0;
    double SettleTimer = 0.0;
    double RecoveryStartAngle = 0.0;
    double RecoveryPhaseTime = 0.0;
    bool bGroundContact = false;

    for (int32 Step = 0; Step < Settings.MaximumSteps; ++Step)
    {
        const double Time = Step * Settings.FixedDeltaSeconds;
        if (Step == Settings.PushStep)
        {
            AngularVelocity += Settings.AngularImpulseNewtonMeterSeconds / Settings.InertiaKilogramMetersSquared;
        }

        double TargetAngle = 0.0;
        double TargetAngularVelocity = 0.0;
        double Torque = 0.0;
        const double GravityTorque = Settings.MassKilograms * Settings.GravityMetersPerSecondSquared * Settings.CenterOfMassHeightMeters * FMath::Sin(Angle);
        const double SupportMargin = Settings.SupportHalfWidthMeters - Settings.CenterOfMassHeightMeters * FMath::Abs(FMath::Sin(Angle));

        if (Phase == EPlanarCharacterRecoveryPhase::Balanced && SupportMargin < 0.0)
        {
            Phase = EPlanarCharacterRecoveryPhase::Falling;
            Result.bFell = true;
            Result.FallTimeSeconds = Time;
        }

        if (Phase == EPlanarCharacterRecoveryPhase::Balanced || Phase == EPlanarCharacterRecoveryPhase::Recovered)
        {
            Torque = -Settings.BalanceStiffnessNewtonMetersPerRadian * Angle - Settings.BalanceDampingNewtonMeterSeconds * AngularVelocity;
            if (Settings.bUseGravityCompensation) Torque -= GravityTorque;
        }
        else if (Phase == EPlanarCharacterRecoveryPhase::Falling || Phase == EPlanarCharacterRecoveryPhase::Settling)
        {
            Torque = -Settings.FallDampingNewtonMeterSeconds * AngularVelocity;
        }
        else if (Phase == EPlanarCharacterRecoveryPhase::Recovering)
        {
            const double Alpha = RecoveryPhaseTime / Settings.RecoveryTrajectorySeconds;
            const double Blend = SmoothStep(Alpha);
            const double BlendDerivative = Alpha > 0.0 && Alpha < 1.0 ? 6.0 * Alpha * (1.0 - Alpha) / Settings.RecoveryTrajectorySeconds : 0.0;
            TargetAngle = RecoveryStartAngle * (1.0 - Blend);
            TargetAngularVelocity = -RecoveryStartAngle * BlendDerivative;
            Torque = Settings.RecoveryStiffnessNewtonMetersPerRadian * (TargetAngle - Angle)
                + Settings.RecoveryDampingNewtonMeterSeconds * (TargetAngularVelocity - AngularVelocity);
            if (Settings.bUseGravityCompensation) Torque -= GravityTorque;
            RecoveryPhaseTime += Settings.FixedDeltaSeconds;
        }
        Torque = FMath::Clamp(Torque, -Settings.MaximumTorqueNewtonMeters, Settings.MaximumTorqueNewtonMeters);

        const double AngularAcceleration = (GravityTorque + Torque) / Settings.InertiaKilogramMetersSquared;
        AngularVelocity += Settings.FixedDeltaSeconds * AngularAcceleration;
        Angle += Settings.FixedDeltaSeconds * AngularVelocity;

        if (FMath::Abs(Angle) >= Settings.GroundAngleRadians)
        {
            const double Side = FMath::Sign(Angle);
            Angle = Side * Settings.GroundAngleRadians;
            if (AngularVelocity * Side > 0.0)
            {
                AngularVelocity = -AngularVelocity * Settings.GroundRestitution;
                if (!bGroundContact) ++Result.GroundImpactCount;
            }
            bGroundContact = true;
        }
        else if (FMath::Abs(Angle) < Settings.GroundAngleRadians - 1.0e-6)
        {
            bGroundContact = false;
        }

        if (Phase == EPlanarCharacterRecoveryPhase::Falling && FMath::Abs(Angle) > Settings.GroundAngleRadians * 0.97
            && FMath::Abs(AngularVelocity) < Settings.SettleSpeedRadiansPerSecond)
        {
            Phase = EPlanarCharacterRecoveryPhase::Settling;
            SettleTimer = 0.0;
        }
        if (Phase == EPlanarCharacterRecoveryPhase::Settling)
        {
            if (FMath::Abs(AngularVelocity) < Settings.SettleSpeedRadiansPerSecond) SettleTimer += Settings.FixedDeltaSeconds;
            else SettleTimer = 0.0;
            if (SettleTimer >= Settings.SettleHoldSeconds && Settings.bEnableGetUp)
            {
                Phase = EPlanarCharacterRecoveryPhase::Recovering;
                RecoveryStartAngle = Angle;
                RecoveryPhaseTime = 0.0;
                Result.RecoveryStartTimeSeconds = Time;
                Result.bFrontGetUp = Angle > 0.0;
            }
        }
        if (Phase == EPlanarCharacterRecoveryPhase::Recovering
            && FMath::Abs(Angle) < Settings.SuccessAngleRadians
            && FMath::Abs(AngularVelocity) < Settings.SuccessSpeedRadiansPerSecond)
        {
            Phase = EPlanarCharacterRecoveryPhase::Recovered;
            Result.bRecovered = true;
            Result.RecoveryStep = Step;
            Result.RecoveryTimeSeconds = Time - Result.FallTimeSeconds;
            Result.GetUpControlTimeSeconds = Time - Result.RecoveryStartTimeSeconds;
        }

        const double PostStepSupportMargin = Settings.SupportHalfWidthMeters - Settings.CenterOfMassHeightMeters * FMath::Abs(FMath::Sin(Angle));
        Result.MaximumAbsoluteAngleRadians = FMath::Max(Result.MaximumAbsoluteAngleRadians, FMath::Abs(Angle));
        Result.MinimumSupportMarginMeters = FMath::Min(Result.MinimumSupportMarginMeters, PostStepSupportMargin);
        Result.PeakAbsoluteTorqueNewtonMeters = FMath::Max(Result.PeakAbsoluteTorqueNewtonMeters, FMath::Abs(Torque));
        Result.AbsoluteMechanicalWorkJoules += FMath::Abs(Torque * AngularVelocity) * Settings.FixedDeltaSeconds;
        Result.Samples.Add({Step, Time, Angle, AngularVelocity, TargetAngle, Torque, PostStepSupportMargin, Phase});

        if (!FMath::IsFinite(Angle) || !FMath::IsFinite(AngularVelocity) || !FMath::IsFinite(Torque))
        {
            Phase = EPlanarCharacterRecoveryPhase::Failed;
            Result.bFailed = true;
            break;
        }
        if (Time >= Settings.FailureTimeoutSeconds && Result.bFell && !Result.bRecovered)
        {
            Phase = EPlanarCharacterRecoveryPhase::Failed;
            Result.bFailed = true;
            Result.Samples.Last().Phase = Phase;
            break;
        }
    }

    Result.FinalPoseErrorRadians = FMath::Abs(Angle);
    return Result;
}

const TCHAR* FPlanarCharacterRecoveryExperiment::PhaseName(const EPlanarCharacterRecoveryPhase Phase)
{
    switch (Phase)
    {
    case EPlanarCharacterRecoveryPhase::Balanced: return TEXT("Balanced");
    case EPlanarCharacterRecoveryPhase::Falling: return TEXT("Falling");
    case EPlanarCharacterRecoveryPhase::Settling: return TEXT("Settling");
    case EPlanarCharacterRecoveryPhase::Recovering: return TEXT("Recovering");
    case EPlanarCharacterRecoveryPhase::Recovered: return TEXT("Recovered");
    case EPlanarCharacterRecoveryPhase::Failed: return TEXT("Failed");
    default: return TEXT("Unknown");
    }
}
