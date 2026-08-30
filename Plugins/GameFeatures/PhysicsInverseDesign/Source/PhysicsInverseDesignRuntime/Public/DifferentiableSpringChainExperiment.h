#pragma once

#include "CoreMinimal.h"

struct PHYSICSINVERSEDESIGNRUNTIME_API FSpringChainParameters
{
    double ProximalStiffness = 25.0;
    double DistalStiffness = 80.0;
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FSpringChainSettings
{
    int32 MassCount = 6;
    int32 StepCount = 240;
    double FixedDeltaSeconds = 1.0 / 120.0;
    double Damping = 3.0;
    double TipForceAmplitude = 320.0;
    double PrimaryFrequencyHz = 0.75;
    double SecondaryFrequencyHz = 1.7;
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FSpringChainTarget
{
    TArray<double> TipPosition;
    TArray<double> TipVelocity;
    double PositionWeight = 1.0;
    double VelocityWeight = 0.02;
    bool bUseFullTrajectory = true;
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FSpringChainGradient
{
    double ProximalStiffness = 0.0;
    double DistalStiffness = 0.0;
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FSpringChainEvaluation
{
    double Loss = 0.0;
    double FinalTipPosition = 0.0;
    double FinalTipVelocity = 0.0;
    FSpringChainGradient Gradient;
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FSpringChainOptimizationResult
{
    FSpringChainParameters Parameters;
    double InitialLoss = 0.0;
    double FinalLoss = 0.0;
    int32 Iterations = 0;
    bool bConverged = false;
    TArray<double> LossHistory;
};

class PHYSICSINVERSEDESIGNRUNTIME_API FDifferentiableSpringChainExperiment
{
public:
    static bool BuildTarget(const FSpringChainParameters& Parameters, const FSpringChainSettings& Settings, FSpringChainTarget& OutTarget);
    static FSpringChainEvaluation Evaluate(const FSpringChainParameters& Parameters, const FSpringChainSettings& Settings, const FSpringChainTarget& Target);
    static FSpringChainGradient EvaluateCenteredFiniteDifference(const FSpringChainParameters& Parameters, const FSpringChainSettings& Settings, const FSpringChainTarget& Target, double RelativeEpsilon = 1.0e-5);
    static FSpringChainOptimizationResult Optimize(const FSpringChainParameters& Initial, const FSpringChainParameters& Minimum, const FSpringChainParameters& Maximum, const FSpringChainSettings& Settings, const FSpringChainTarget& Target, int32 MaximumIterations = 300);
};
