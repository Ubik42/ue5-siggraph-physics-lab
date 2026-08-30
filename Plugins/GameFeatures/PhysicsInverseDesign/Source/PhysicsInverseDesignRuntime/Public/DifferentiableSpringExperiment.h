#pragma once

#include "CoreMinimal.h"

struct PHYSICSINVERSEDESIGNRUNTIME_API FSpringDesignParameters
{
    double DriveForce = 20.0;
    double Stiffness = 10.0;
    double Damping = 1.0;
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FSpringDesignBounds
{
    FSpringDesignParameters Minimum{0.0, 1.0, 0.05};
    FSpringDesignParameters Maximum{80.0, 40.0, 12.0};
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FSpringDesignTarget
{
    double Position = 2.0;
    double Velocity = 0.0;
    double PositionWeight = 1.0;
    double VelocityWeight = 0.25;
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FSpringDesignState
{
    double Position = 0.0;
    double Velocity = 0.0;
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FSpringDesignGradient
{
    double DriveForce = 0.0;
    double Stiffness = 0.0;
    double Damping = 0.0;
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FSpringDesignEvaluation
{
    FSpringDesignState FinalState;
    FSpringDesignGradient Gradient;
    double Loss = 0.0;
};

struct PHYSICSINVERSEDESIGNRUNTIME_API FSpringDesignOptimizationResult
{
    FSpringDesignParameters Parameters;
    double InitialLoss = 0.0;
    double FinalLoss = 0.0;
    int32 Iterations = 0;
    bool bConverged = false;
    TArray<double> LossHistory;
};

class PHYSICSINVERSEDESIGNRUNTIME_API FDifferentiableSpringExperiment
{
public:
    static bool SimulateTrajectory(
        const FSpringDesignParameters& Parameters,
        double FixedDeltaSeconds,
        int32 StepCount,
        TArray<FSpringDesignState>& OutTrajectory);

    static FSpringDesignEvaluation Evaluate(
        const FSpringDesignParameters& Parameters,
        const FSpringDesignTarget& Target,
        double FixedDeltaSeconds,
        int32 StepCount);

    static FSpringDesignGradient EvaluateCenteredFiniteDifference(
        const FSpringDesignParameters& Parameters,
        const FSpringDesignTarget& Target,
        double FixedDeltaSeconds,
        int32 StepCount,
        double RelativeEpsilon = 1.0e-5);

    static FSpringDesignOptimizationResult Optimize(
        const FSpringDesignParameters& InitialParameters,
        const FSpringDesignBounds& Bounds,
        const FSpringDesignTarget& Target,
        double FixedDeltaSeconds,
        int32 StepCount,
        bool bOptimizeDriveForce,
        bool bOptimizeStiffness,
        bool bOptimizeDamping,
        int32 MaximumIterations = 200);
};
