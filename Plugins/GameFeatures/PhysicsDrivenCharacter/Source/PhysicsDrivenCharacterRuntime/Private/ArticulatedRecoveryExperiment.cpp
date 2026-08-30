#include "ArticulatedRecoveryExperiment.h"

namespace
{
double Rms(const FVector& Value)
{
    return FMath::Sqrt((Value.X * Value.X + Value.Y * Value.Y + Value.Z * Value.Z) / 3.0);
}

double MaxAbs(const FVector& Value)
{
    return FMath::Max3(FMath::Abs(Value.X), FMath::Abs(Value.Y), FMath::Abs(Value.Z));
}

FVector AbsVector(const FVector& Value)
{
    return FVector(FMath::Abs(Value.X), FMath::Abs(Value.Y), FMath::Abs(Value.Z));
}

FVector MaxVector(const FVector& A, const FVector& B)
{
    return FVector(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z));
}

FVector ClampVector(const FVector& Value, const FVector& Limit)
{
    return FVector(
        FMath::Clamp(Value.X, -Limit.X, Limit.X),
        FMath::Clamp(Value.Y, -Limit.Y, Limit.Y),
        FMath::Clamp(Value.Z, -Limit.Z, Limit.Z));
}

bool PositiveVector(const FVector& Value)
{
    return Value.X > 0.0 && Value.Y > 0.0 && Value.Z > 0.0;
}

bool Validate(const FArticulatedRecoverySettings& Settings)
{
    return Settings.FixedDeltaSeconds > 0.0 && Settings.MaximumSteps > 0
        && Settings.DisturbanceStep >= 0 && Settings.DisturbanceStep < Settings.MaximumSteps
        && PositiveVector(Settings.InertiaKilogramMetersSquared) && PositiveVector(Settings.StiffnessNewtonMetersPerRadian)
        && PositiveVector(Settings.DampingNewtonMeterSeconds) && PositiveVector(Settings.MaximumTorqueNewtonMeters)
        && Settings.CouplingStiffnessNewtonMetersPerRadian >= 0.0 && Settings.CouplingDampingNewtonMeterSeconds >= 0.0
        && Settings.SuccessRmsAngleRadians > 0.0 && Settings.SuccessMaximumSpeedRadiansPerSecond > 0.0
        && Settings.SuccessHoldSeconds >= 0.0 && Settings.FailureTimeoutSeconds > 0.0;
}
}

FArticulatedRecoveryResult FArticulatedRecoveryExperiment::Simulate(const FArticulatedRecoverySettings& Settings)
{
    FArticulatedRecoveryResult Result;
    if (!Validate(Settings)) return Result;

    Result.bValid = true;
    FVector Angles = Settings.InitialAnglesRadians;
    FVector Velocities = Settings.InitialAngularVelocitiesRadiansPerSecond;
    Result.InitialRmsAngleRadians = Rms(Angles);
    double SuccessTimer = 0.0;

    for (int32 Step = 0; Step < Settings.MaximumSteps; ++Step)
    {
        const double Time = Step * Settings.FixedDeltaSeconds;
        if (Step == Settings.DisturbanceStep && Settings.DistalAngularImpulseNewtonMeterSeconds != 0.0)
        {
            Velocities.Z += Settings.DistalAngularImpulseNewtonMeterSeconds / Settings.InertiaKilogramMetersSquared.Z;
        }

        const FVector RawControl(
            -Settings.StiffnessNewtonMetersPerRadian.X * Angles.X - Settings.DampingNewtonMeterSeconds.X * Velocities.X,
            -Settings.StiffnessNewtonMetersPerRadian.Y * Angles.Y - Settings.DampingNewtonMeterSeconds.Y * Velocities.Y,
            -Settings.StiffnessNewtonMetersPerRadian.Z * Angles.Z - Settings.DampingNewtonMeterSeconds.Z * Velocities.Z);
        const FVector Control = ClampVector(RawControl, Settings.MaximumTorqueNewtonMeters);

        FVector Coupling = FVector::ZeroVector;
        const double HipKneeTorque = -Settings.CouplingStiffnessNewtonMetersPerRadian * (Angles.X - Angles.Y)
            - Settings.CouplingDampingNewtonMeterSeconds * (Velocities.X - Velocities.Y);
        Coupling.X += HipKneeTorque;
        Coupling.Y -= HipKneeTorque;
        const double KneeSpineTorque = -Settings.CouplingStiffnessNewtonMetersPerRadian * (Angles.Y - Angles.Z)
            - Settings.CouplingDampingNewtonMeterSeconds * (Velocities.Y - Velocities.Z);
        Coupling.Y += KneeSpineTorque;
        Coupling.Z -= KneeSpineTorque;

        const FVector Gravity(
            Settings.GravityMomentNewtonMeters.X * FMath::Sin(Angles.X),
            Settings.GravityMomentNewtonMeters.Y * FMath::Sin(Angles.Y),
            Settings.GravityMomentNewtonMeters.Z * FMath::Sin(Angles.Z));
        const FVector Acceleration(
            (Control.X + Coupling.X + Gravity.X) / Settings.InertiaKilogramMetersSquared.X,
            (Control.Y + Coupling.Y + Gravity.Y) / Settings.InertiaKilogramMetersSquared.Y,
            (Control.Z + Coupling.Z + Gravity.Z) / Settings.InertiaKilogramMetersSquared.Z);

        Velocities += Acceleration * Settings.FixedDeltaSeconds;
        Angles += Velocities * Settings.FixedDeltaSeconds;

        Result.PeakAbsoluteAnglesRadians = MaxVector(Result.PeakAbsoluteAnglesRadians, AbsVector(Angles));
        Result.PeakAbsoluteControlTorquesNewtonMeters = MaxVector(Result.PeakAbsoluteControlTorquesNewtonMeters, AbsVector(Control));
        Result.MaximumAbsoluteSpeedRadiansPerSecond = FMath::Max(Result.MaximumAbsoluteSpeedRadiansPerSecond, MaxAbs(Velocities));
        Result.AbsoluteMechanicalWorkJoules += (FMath::Abs(Control.X * Velocities.X) + FMath::Abs(Control.Y * Velocities.Y)
            + FMath::Abs(Control.Z * Velocities.Z)) * Settings.FixedDeltaSeconds;
        if (Step >= Settings.DisturbanceStep)
        {
            Result.PeakProximalResponseRadians = FMath::Max(Result.PeakProximalResponseRadians, FMath::Max(FMath::Abs(Angles.X), FMath::Abs(Angles.Y)));
            Result.PeakDistalResponseRadians = FMath::Max(Result.PeakDistalResponseRadians, FMath::Abs(Angles.Z));
        }
        Result.SimulatedSteps = Step + 1;

        if (!FMath::IsFinite(Angles.X) || !FMath::IsFinite(Angles.Y) || !FMath::IsFinite(Angles.Z)
            || !FMath::IsFinite(Velocities.X) || !FMath::IsFinite(Velocities.Y) || !FMath::IsFinite(Velocities.Z))
        {
            Result.bFailed = true;
            break;
        }

        if (Rms(Angles) <= Settings.SuccessRmsAngleRadians && MaxAbs(Velocities) <= Settings.SuccessMaximumSpeedRadiansPerSecond
            && Step >= Settings.DisturbanceStep)
        {
            SuccessTimer += Settings.FixedDeltaSeconds;
            if (SuccessTimer >= Settings.SuccessHoldSeconds)
            {
                Result.bRecovered = true;
                Result.RecoveryTimeSeconds = Time;
                break;
            }
        }
        else
        {
            SuccessTimer = 0.0;
        }

        if (Time >= Settings.FailureTimeoutSeconds)
        {
            Result.bFailed = true;
            break;
        }
    }

    Result.FinalRmsAngleRadians = Rms(Angles);
    return Result;
}
