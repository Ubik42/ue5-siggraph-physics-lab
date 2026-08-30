#pragma once

#include "CoreMinimal.h"

enum class EPlanarCharacterRecoveryPhase : uint8
{
    Balanced,
    Falling,
    Settling,
    Recovering,
    Recovered,
    Failed
};

struct PHYSICSDRIVENCHARACTERRUNTIME_API FPlanarCharacterRecoverySettings
{
    double FixedDeltaSeconds = 1.0 / 240.0;
    int32 MaximumSteps = 1440;
    int32 PushStep = 24;
    double AngularImpulseNewtonMeterSeconds = 120.0;
    double MassKilograms = 75.0;
    double CenterOfMassHeightMeters = 0.9;
    double InertiaKilogramMetersSquared = 60.0;
    double GravityMetersPerSecondSquared = 9.81;
    double SupportHalfWidthMeters = 0.18;
    double GroundAngleRadians = PI * 0.5;
    double GroundRestitution = 0.0;
    double FallDampingNewtonMeterSeconds = 180.0;
    double BalanceStiffnessNewtonMetersPerRadian = 1250.0;
    double BalanceDampingNewtonMeterSeconds = 280.0;
    double RecoveryStiffnessNewtonMetersPerRadian = 1650.0;
    double RecoveryDampingNewtonMeterSeconds = 330.0;
    double MaximumTorqueNewtonMeters = 1250.0;
    double SettleSpeedRadiansPerSecond = FMath::DegreesToRadians(6.0);
    double SettleHoldSeconds = 0.15;
    double RecoveryTrajectorySeconds = 1.25;
    double SuccessAngleRadians = FMath::DegreesToRadians(3.0);
    double SuccessSpeedRadiansPerSecond = FMath::DegreesToRadians(5.0);
    double FailureTimeoutSeconds = 5.5;
    bool bEnableGetUp = true;
    bool bUseGravityCompensation = true;
};

struct PHYSICSDRIVENCHARACTERRUNTIME_API FPlanarCharacterRecoverySample
{
    int32 Step = 0;
    double TimeSeconds = 0.0;
    double AngleRadians = 0.0;
    double AngularVelocityRadiansPerSecond = 0.0;
    double TargetAngleRadians = 0.0;
    double AppliedTorqueNewtonMeters = 0.0;
    double SupportMarginMeters = 0.0;
    EPlanarCharacterRecoveryPhase Phase = EPlanarCharacterRecoveryPhase::Balanced;
};

struct PHYSICSDRIVENCHARACTERRUNTIME_API FPlanarCharacterRecoveryResult
{
    bool bValid = false;
    bool bFell = false;
    bool bRecovered = false;
    bool bFailed = false;
    bool bFrontGetUp = false;
    int32 GroundImpactCount = 0;
    int32 RecoveryStep = INDEX_NONE;
    double FallTimeSeconds = -1.0;
    double RecoveryStartTimeSeconds = -1.0;
    double RecoveryTimeSeconds = -1.0;
    double GetUpControlTimeSeconds = -1.0;
    double FinalPoseErrorRadians = 0.0;
    double MaximumAbsoluteAngleRadians = 0.0;
    double MinimumSupportMarginMeters = 0.0;
    double PeakAbsoluteTorqueNewtonMeters = 0.0;
    double AbsoluteMechanicalWorkJoules = 0.0;
    TArray<FPlanarCharacterRecoverySample> Samples;
};

class PHYSICSDRIVENCHARACTERRUNTIME_API FPlanarCharacterRecoveryExperiment
{
public:
    static FPlanarCharacterRecoveryResult Simulate(const FPlanarCharacterRecoverySettings& Settings);
    static const TCHAR* PhaseName(EPlanarCharacterRecoveryPhase Phase);
};
