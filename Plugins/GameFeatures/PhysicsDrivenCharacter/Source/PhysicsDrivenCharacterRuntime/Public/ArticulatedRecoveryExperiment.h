#pragma once

#include "CoreMinimal.h"

struct PHYSICSDRIVENCHARACTERRUNTIME_API FArticulatedRecoverySettings
{
    double FixedDeltaSeconds = 1.0 / 500.0;
    int32 MaximumSteps = 3000;
    FVector InitialAnglesRadians = FVector(FMath::DegreesToRadians(50.0), FMath::DegreesToRadians(-70.0), FMath::DegreesToRadians(35.0));
    FVector InitialAngularVelocitiesRadiansPerSecond = FVector::ZeroVector;
    FVector InertiaKilogramMetersSquared = FVector(8.0, 5.0, 3.0);
    FVector GravityMomentNewtonMeters = FVector(120.0, 80.0, 45.0);
    FVector StiffnessNewtonMetersPerRadian = FVector(300.0, 220.0, 140.0);
    FVector DampingNewtonMeterSeconds = FVector(70.0, 55.0, 35.0);
    FVector MaximumTorqueNewtonMeters = FVector(450.0, 300.0, 180.0);
    double CouplingStiffnessNewtonMetersPerRadian = 60.0;
    double CouplingDampingNewtonMeterSeconds = 10.0;
    int32 DisturbanceStep = 50;
    double DistalAngularImpulseNewtonMeterSeconds = 0.0;
    double SuccessRmsAngleRadians = FMath::DegreesToRadians(2.0);
    double SuccessMaximumSpeedRadiansPerSecond = FMath::DegreesToRadians(4.0);
    double SuccessHoldSeconds = 0.2;
    double FailureTimeoutSeconds = 5.5;
};

struct PHYSICSDRIVENCHARACTERRUNTIME_API FArticulatedRecoveryResult
{
    bool bValid = false;
    bool bRecovered = false;
    bool bFailed = false;
    double RecoveryTimeSeconds = -1.0;
    double InitialRmsAngleRadians = 0.0;
    double FinalRmsAngleRadians = 0.0;
    double MaximumAbsoluteSpeedRadiansPerSecond = 0.0;
    FVector PeakAbsoluteAnglesRadians = FVector::ZeroVector;
    FVector PeakAbsoluteControlTorquesNewtonMeters = FVector::ZeroVector;
    double AbsoluteMechanicalWorkJoules = 0.0;
    double PeakProximalResponseRadians = 0.0;
    double PeakDistalResponseRadians = 0.0;
    int32 SimulatedSteps = 0;
};

class PHYSICSDRIVENCHARACTERRUNTIME_API FArticulatedRecoveryExperiment
{
public:
    static FArticulatedRecoveryResult Simulate(const FArticulatedRecoverySettings& Settings);
};
