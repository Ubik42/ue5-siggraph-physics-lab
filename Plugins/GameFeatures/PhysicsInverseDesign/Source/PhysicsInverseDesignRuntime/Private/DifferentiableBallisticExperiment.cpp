#include "DifferentiableBallisticExperiment.h"

namespace
{
struct FBallisticTangent
{
    FVector2d Position[3] = {};
    FVector2d Velocity[3] = {};
};

double BallisticLossOnly(const FBallisticDesignParameters& Parameters, const FBallisticTarget& Target, const double Dt, const int32 Steps)
{
    return FDifferentiableBallisticExperiment::Evaluate(Parameters, Target, Dt, Steps).Loss;
}
}

FBallisticEvaluation FDifferentiableBallisticExperiment::Evaluate(const FBallisticDesignParameters& Parameters, const FBallisticTarget& Target, const double FixedDeltaSeconds, const int32 StepCount, const double GravityCmPerSecondSquared)
{
    FBallisticEvaluation Result;
    if (FixedDeltaSeconds <= 0.0 || StepCount <= 0 || Parameters.LinearDrag < 0.0)
    {
        Result.Loss = TNumericLimits<double>::Max();
        return Result;
    }
    Result.FinalState.Velocity = FVector2d(Parameters.InitialVelocityX, Parameters.InitialVelocityZ);
    FBallisticTangent Tangent;
    Tangent.Velocity[0].X = 1.0;
    Tangent.Velocity[1].Y = 1.0;
    for (int32 Step = 0; Step < StepCount; ++Step)
    {
        const FVector2d Acceleration(-Parameters.LinearDrag * Result.FinalState.Velocity.X, -GravityCmPerSecondSquared - Parameters.LinearDrag * Result.FinalState.Velocity.Y);
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const double DDrag = Index == 2 ? 1.0 : 0.0;
            const FVector2d DAcceleration = -DDrag * Result.FinalState.Velocity - Parameters.LinearDrag * Tangent.Velocity[Index];
            Tangent.Velocity[Index] += FixedDeltaSeconds * DAcceleration;
            Tangent.Position[Index] += FixedDeltaSeconds * Tangent.Velocity[Index];
        }
        Result.FinalState.Velocity += FixedDeltaSeconds * Acceleration;
        Result.FinalState.Position += FixedDeltaSeconds * Result.FinalState.Velocity;
    }
    const FVector2d PositionError = Result.FinalState.Position - Target.State.Position;
    const FVector2d VelocityError = Result.FinalState.Velocity - Target.State.Velocity;
    Result.Loss = 0.5 * (Target.PositionWeight.X * FMath::Square(PositionError.X) + Target.PositionWeight.Y * FMath::Square(PositionError.Y)
        + Target.VelocityWeight.X * FMath::Square(VelocityError.X) + Target.VelocityWeight.Y * FMath::Square(VelocityError.Y));
    double* Output[3] = {&Result.Gradient.InitialVelocityX, &Result.Gradient.InitialVelocityZ, &Result.Gradient.LinearDrag};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        *Output[Index] = Target.PositionWeight.X * PositionError.X * Tangent.Position[Index].X
            + Target.PositionWeight.Y * PositionError.Y * Tangent.Position[Index].Y
            + Target.VelocityWeight.X * VelocityError.X * Tangent.Velocity[Index].X
            + Target.VelocityWeight.Y * VelocityError.Y * Tangent.Velocity[Index].Y;
    }
    return Result;
}

FBallisticGradient FDifferentiableBallisticExperiment::EvaluateCenteredFiniteDifference(const FBallisticDesignParameters& Parameters, const FBallisticTarget& Target, const double FixedDeltaSeconds, const int32 StepCount, const double RelativeEpsilon)
{
    FBallisticGradient Result;
    const double Values[3] = {Parameters.InitialVelocityX, Parameters.InitialVelocityZ, Parameters.LinearDrag};
    double* Output[3] = {&Result.InitialVelocityX, &Result.InitialVelocityZ, &Result.LinearDrag};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const double Epsilon = FMath::Max(1.0, FMath::Abs(Values[Index])) * RelativeEpsilon;
        FBallisticDesignParameters Plus = Parameters;
        FBallisticDesignParameters Minus = Parameters;
        double* PlusValues[3] = {&Plus.InitialVelocityX, &Plus.InitialVelocityZ, &Plus.LinearDrag};
        double* MinusValues[3] = {&Minus.InitialVelocityX, &Minus.InitialVelocityZ, &Minus.LinearDrag};
        *PlusValues[Index] += Epsilon;
        *MinusValues[Index] -= Epsilon;
        *Output[Index] = (BallisticLossOnly(Plus, Target, FixedDeltaSeconds, StepCount) - BallisticLossOnly(Minus, Target, FixedDeltaSeconds, StepCount)) / (2.0 * Epsilon);
    }
    return Result;
}

FBallisticOptimizationResult FDifferentiableBallisticExperiment::Optimize(const FBallisticDesignParameters& Initial, const FBallisticDesignBounds& Bounds, const FBallisticTarget& Target, const double FixedDeltaSeconds, const int32 StepCount, const bool bOptimizeVelocityX, const bool bOptimizeVelocityZ, const bool bOptimizeDrag, const int32 MaximumIterations)
{
    FBallisticOptimizationResult Result;
    const double Minimum[3] = {Bounds.Minimum.InitialVelocityX, Bounds.Minimum.InitialVelocityZ, Bounds.Minimum.LinearDrag};
    const double Maximum[3] = {Bounds.Maximum.InitialVelocityX, Bounds.Maximum.InitialVelocityZ, Bounds.Maximum.LinearDrag};
    const double InitialValues[3] = {Initial.InitialVelocityX, Initial.InitialVelocityZ, Initial.LinearDrag};
    const bool Enabled[3] = {bOptimizeVelocityX, bOptimizeVelocityZ, bOptimizeDrag};
    double Q[3] = {};
    for (int32 Index = 0; Index < 3; ++Index) Q[Index] = FMath::Clamp((InitialValues[Index] - Minimum[Index]) / (Maximum[Index] - Minimum[Index]), 0.0, 1.0);
    auto Decode = [&]()
    {
        FBallisticDesignParameters Parameters;
        double* Values[3] = {&Parameters.InitialVelocityX, &Parameters.InitialVelocityZ, &Parameters.LinearDrag};
        for (int32 Index = 0; Index < 3; ++Index) *Values[Index] = Minimum[Index] + Q[Index] * (Maximum[Index] - Minimum[Index]);
        return Parameters;
    };
    FBallisticEvaluation Current = Evaluate(Decode(), Target, FixedDeltaSeconds, StepCount);
    Result.InitialLoss = Current.Loss;
    Result.LossHistory.Add(Current.Loss);
    for (int32 Iteration = 0; Iteration < MaximumIterations; ++Iteration)
    {
        const double LossBeforeIteration = Current.Loss;
        bool bAnyAccepted = false;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            if (!Enabled[Index]) continue;
            const double Physical[3] = {Current.Gradient.InitialVelocityX, Current.Gradient.InitialVelocityZ, Current.Gradient.LinearDrag};
            const double NormalizedGradient = Physical[Index] * (Maximum[Index] - Minimum[Index]);
            if (FMath::Abs(NormalizedGradient) < 1.0e-14) continue;
            const double Previous = Q[Index];
            double StepLength = 0.1;
            bool bAccepted = false;
            for (int32 Search = 0; Search < 28; ++Search)
            {
                Q[Index] = FMath::Clamp(Previous - FMath::Sign(NormalizedGradient) * StepLength, 0.0, 1.0);
                const FBallisticEvaluation Candidate = Evaluate(Decode(), Target, FixedDeltaSeconds, StepCount);
                if (Candidate.Loss < Current.Loss)
                {
                    Current = Candidate;
                    bAccepted = true;
                    bAnyAccepted = true;
                    break;
                }
                Q[Index] = Previous;
                StepLength *= 0.5;
            }
            if (!bAccepted) Q[Index] = Previous;
        }
        if (!bAnyAccepted) break;
        Result.LossHistory.Add(Current.Loss);
        Result.Iterations = Iteration + 1;
        if (Current.Loss < 1.0e-8) { Result.bConverged = true; break; }
        if (LossBeforeIteration - Current.Loss < FMath::Max(1.0, LossBeforeIteration) * 1.0e-13) { Result.bConverged = true; break; }
    }
    Result.Parameters = Decode();
    Result.FinalLoss = Current.Loss;
    Result.bConverged = Result.bConverged || Result.FinalLoss < Result.InitialLoss * 1.0e-6;
    return Result;
}
