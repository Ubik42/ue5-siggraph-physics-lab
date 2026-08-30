#include "DifferentiableSpringExperiment.h"

namespace
{
struct FTangentState
{
    double Position[3] = {};
    double Velocity[3] = {};
};

double LossOnly(const FSpringDesignParameters& Parameters, const FSpringDesignTarget& Target, const double Dt, const int32 Steps)
{
    return FDifferentiableSpringExperiment::Evaluate(Parameters, Target, Dt, Steps).Loss;
}

double ClampUnit(const double Value)
{
    return FMath::Clamp(Value, 0.0, 1.0);
}
}

bool FDifferentiableSpringExperiment::SimulateTrajectory(
    const FSpringDesignParameters& Parameters,
    const double FixedDeltaSeconds,
    const int32 StepCount,
    TArray<FSpringDesignState>& OutTrajectory)
{
    OutTrajectory.Reset();
    if (FixedDeltaSeconds <= 0.0 || StepCount <= 0 || Parameters.Stiffness <= 0.0 || Parameters.Damping < 0.0)
    {
        return false;
    }
    OutTrajectory.Reserve(StepCount + 1);
    FSpringDesignState State;
    OutTrajectory.Add(State);
    for (int32 Step = 0; Step < StepCount; ++Step)
    {
        const double Acceleration = Parameters.DriveForce - Parameters.Stiffness * State.Position - Parameters.Damping * State.Velocity;
        State.Velocity += FixedDeltaSeconds * Acceleration;
        State.Position += FixedDeltaSeconds * State.Velocity;
        if (!FMath::IsFinite(State.Position) || !FMath::IsFinite(State.Velocity))
        {
            OutTrajectory.Reset();
            return false;
        }
        OutTrajectory.Add(State);
    }
    return true;
}

FSpringDesignEvaluation FDifferentiableSpringExperiment::Evaluate(
    const FSpringDesignParameters& Parameters,
    const FSpringDesignTarget& Target,
    const double FixedDeltaSeconds,
    const int32 StepCount)
{
    FSpringDesignEvaluation Result;
    if (FixedDeltaSeconds <= 0.0 || StepCount <= 0 || Parameters.Stiffness <= 0.0 || Parameters.Damping < 0.0)
    {
        Result.Loss = TNumericLimits<double>::Max();
        return Result;
    }

    FTangentState Tangent;
    for (int32 Step = 0; Step < StepCount; ++Step)
    {
        const double Acceleration = Parameters.DriveForce - Parameters.Stiffness * Result.FinalState.Position - Parameters.Damping * Result.FinalState.Velocity;
        for (int32 ParameterIndex = 0; ParameterIndex < 3; ++ParameterIndex)
        {
            const double DForce = ParameterIndex == 0 ? 1.0 : 0.0;
            const double DStiffness = ParameterIndex == 1 ? 1.0 : 0.0;
            const double DDamping = ParameterIndex == 2 ? 1.0 : 0.0;
            const double DAcceleration = DForce
                - DStiffness * Result.FinalState.Position
                - Parameters.Stiffness * Tangent.Position[ParameterIndex]
                - DDamping * Result.FinalState.Velocity
                - Parameters.Damping * Tangent.Velocity[ParameterIndex];
            Tangent.Velocity[ParameterIndex] += FixedDeltaSeconds * DAcceleration;
            Tangent.Position[ParameterIndex] += FixedDeltaSeconds * Tangent.Velocity[ParameterIndex];
        }
        Result.FinalState.Velocity += FixedDeltaSeconds * Acceleration;
        Result.FinalState.Position += FixedDeltaSeconds * Result.FinalState.Velocity;
    }

    const double PositionError = Result.FinalState.Position - Target.Position;
    const double VelocityError = Result.FinalState.Velocity - Target.Velocity;
    Result.Loss = 0.5 * Target.PositionWeight * PositionError * PositionError
        + 0.5 * Target.VelocityWeight * VelocityError * VelocityError;
    double* GradientValues[3] = {&Result.Gradient.DriveForce, &Result.Gradient.Stiffness, &Result.Gradient.Damping};
    for (int32 ParameterIndex = 0; ParameterIndex < 3; ++ParameterIndex)
    {
        *GradientValues[ParameterIndex] = Target.PositionWeight * PositionError * Tangent.Position[ParameterIndex]
            + Target.VelocityWeight * VelocityError * Tangent.Velocity[ParameterIndex];
    }
    return Result;
}

FSpringDesignGradient FDifferentiableSpringExperiment::EvaluateCenteredFiniteDifference(
    const FSpringDesignParameters& Parameters,
    const FSpringDesignTarget& Target,
    const double FixedDeltaSeconds,
    const int32 StepCount,
    const double RelativeEpsilon)
{
    FSpringDesignGradient Result;
    const double Values[3] = {Parameters.DriveForce, Parameters.Stiffness, Parameters.Damping};
    double* Output[3] = {&Result.DriveForce, &Result.Stiffness, &Result.Damping};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const double Epsilon = FMath::Max(1.0, FMath::Abs(Values[Index])) * RelativeEpsilon;
        FSpringDesignParameters Plus = Parameters;
        FSpringDesignParameters Minus = Parameters;
        double* PlusValues[3] = {&Plus.DriveForce, &Plus.Stiffness, &Plus.Damping};
        double* MinusValues[3] = {&Minus.DriveForce, &Minus.Stiffness, &Minus.Damping};
        *PlusValues[Index] += Epsilon;
        *MinusValues[Index] -= Epsilon;
        *Output[Index] = (LossOnly(Plus, Target, FixedDeltaSeconds, StepCount) - LossOnly(Minus, Target, FixedDeltaSeconds, StepCount)) / (2.0 * Epsilon);
    }
    return Result;
}

FSpringDesignOptimizationResult FDifferentiableSpringExperiment::Optimize(
    const FSpringDesignParameters& InitialParameters,
    const FSpringDesignBounds& Bounds,
    const FSpringDesignTarget& Target,
    const double FixedDeltaSeconds,
    const int32 StepCount,
    const bool bOptimizeDriveForce,
    const bool bOptimizeStiffness,
    const bool bOptimizeDamping,
    const int32 MaximumIterations)
{
    FSpringDesignOptimizationResult Result;
    const double Minimum[3] = {Bounds.Minimum.DriveForce, Bounds.Minimum.Stiffness, Bounds.Minimum.Damping};
    const double Maximum[3] = {Bounds.Maximum.DriveForce, Bounds.Maximum.Stiffness, Bounds.Maximum.Damping};
    const double Initial[3] = {InitialParameters.DriveForce, InitialParameters.Stiffness, InitialParameters.Damping};
    const bool Enabled[3] = {bOptimizeDriveForce, bOptimizeStiffness, bOptimizeDamping};
    double Normalized[3] = {};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        Normalized[Index] = ClampUnit((Initial[Index] - Minimum[Index]) / (Maximum[Index] - Minimum[Index]));
    }

    auto Decode = [&]()
    {
        FSpringDesignParameters Decoded;
        double* Values[3] = {&Decoded.DriveForce, &Decoded.Stiffness, &Decoded.Damping};
        for (int32 Index = 0; Index < 3; ++Index)
        {
            *Values[Index] = Minimum[Index] + Normalized[Index] * (Maximum[Index] - Minimum[Index]);
        }
        return Decoded;
    };

    FSpringDesignEvaluation Current = Evaluate(Decode(), Target, FixedDeltaSeconds, StepCount);
    Result.InitialLoss = Current.Loss;
    Result.LossHistory.Add(Current.Loss);
    for (int32 Iteration = 0; Iteration < MaximumIterations; ++Iteration)
    {
        const double PhysicalGradient[3] = {Current.Gradient.DriveForce, Current.Gradient.Stiffness, Current.Gradient.Damping};
        double NormalizedGradient[3] = {};
        double GradientNormSquared = 0.0;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            NormalizedGradient[Index] = Enabled[Index] ? PhysicalGradient[Index] * (Maximum[Index] - Minimum[Index]) : 0.0;
            GradientNormSquared += NormalizedGradient[Index] * NormalizedGradient[Index];
        }
        if (GradientNormSquared < 1.0e-16)
        {
            Result.bConverged = true;
            break;
        }

        const double Previous[3] = {Normalized[0], Normalized[1], Normalized[2]};
        bool bAccepted = false;
        double StepLength = 0.1 / FMath::Sqrt(GradientNormSquared);
        for (int32 LineSearch = 0; LineSearch < 20; ++LineSearch)
        {
            for (int32 Index = 0; Index < 3; ++Index)
            {
                Normalized[Index] = Enabled[Index] ? ClampUnit(Previous[Index] - StepLength * NormalizedGradient[Index]) : Previous[Index];
            }
            const FSpringDesignEvaluation Candidate = Evaluate(Decode(), Target, FixedDeltaSeconds, StepCount);
            if (Candidate.Loss < Current.Loss)
            {
                Current = Candidate;
                bAccepted = true;
                break;
            }
            StepLength *= 0.5;
        }
        if (!bAccepted)
        {
            for (int32 Index = 0; Index < 3; ++Index)
            {
                Normalized[Index] = Previous[Index];
            }
            break;
        }
        Result.LossHistory.Add(Current.Loss);
        Result.Iterations = Iteration + 1;
        if (Current.Loss < 1.0e-10)
        {
            Result.bConverged = true;
            break;
        }
    }
    Result.Parameters = Decode();
    Result.FinalLoss = Current.Loss;
    Result.bConverged = Result.bConverged || Result.FinalLoss < Result.InitialLoss * 1.0e-4;
    return Result;
}
