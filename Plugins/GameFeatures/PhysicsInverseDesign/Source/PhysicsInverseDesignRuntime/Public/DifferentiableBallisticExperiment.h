#pragma once

#include "CoreMinimal.h"

struct PHYSICSINVERSEDESIGNRUNTIME_API FBallisticDesignParameters
{
    double InitialVelocityX = 700.0;
    double InitialVelocityZ = 1000.0;
    double LinearDrag = 1.0;
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FBallisticDesignBounds
{
    FBallisticDesignParameters Minimum{100.0, 100.0, 0.01};
    FBallisticDesignParameters Maximum{2500.0, 3000.0, 2.0};
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FBallisticState
{
    FVector2d Position = FVector2d::ZeroVector;
    FVector2d Velocity = FVector2d::ZeroVector;
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FBallisticTarget
{
    FBallisticState State;
    FVector2d PositionWeight = FVector2d(1.0, 1.0);
    FVector2d VelocityWeight = FVector2d(0.05, 0.05);
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FBallisticGradient
{
    double InitialVelocityX = 0.0;
    double InitialVelocityZ = 0.0;
    double LinearDrag = 0.0;
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FBallisticEvaluation
{
    FBallisticState FinalState;
    FBallisticGradient Gradient;
    double Loss = 0.0;
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FBallisticOptimizationResult
{
    FBallisticDesignParameters Parameters;
    double InitialLoss = 0.0;
    double FinalLoss = 0.0;
    int32 Iterations = 0;
    bool bConverged = false;
    TArray<double> LossHistory;
};

class PHYSICSINVERSEDESIGNRUNTIME_API FDifferentiableBallisticExperiment
{
public:
    static FBallisticEvaluation Evaluate(const FBallisticDesignParameters& Parameters, const FBallisticTarget& Target, double FixedDeltaSeconds, int32 StepCount, double GravityCmPerSecondSquared = 980.0);
    static FBallisticGradient EvaluateCenteredFiniteDifference(const FBallisticDesignParameters& Parameters, const FBallisticTarget& Target, double FixedDeltaSeconds, int32 StepCount, double RelativeEpsilon = 1.0e-5);
    static FBallisticOptimizationResult Optimize(const FBallisticDesignParameters& Initial, const FBallisticDesignBounds& Bounds, const FBallisticTarget& Target, double FixedDeltaSeconds, int32 StepCount, bool bOptimizeVelocityX, bool bOptimizeVelocityZ, bool bOptimizeDrag, int32 MaximumIterations = 300);
};
