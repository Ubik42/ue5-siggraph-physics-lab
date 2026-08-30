#include "DifferentiableSpringChainExperiment.h"

namespace
{
double SpringStiffness(const FSpringChainParameters& Parameters, const int32 SpringIndex, const int32 SpringCount)
{
    return SpringIndex < SpringCount / 2 ? Parameters.ProximalStiffness : Parameters.DistalStiffness;
}

double SpringDerivative(const int32 ParameterIndex, const int32 SpringIndex, const int32 SpringCount)
{
    return (ParameterIndex == 0) == (SpringIndex < SpringCount / 2) ? 1.0 : 0.0;
}

double TipForce(const FSpringChainSettings& Settings, const int32 Step)
{
    const double Time = (Step + 1) * Settings.FixedDeltaSeconds;
    return Settings.TipForceAmplitude * (FMath::Sin(2.0 * PI * Settings.PrimaryFrequencyHz * Time) + 0.4 * FMath::Sin(2.0 * PI * Settings.SecondaryFrequencyHz * Time));
}

bool Validate(const FSpringChainParameters& Parameters, const FSpringChainSettings& Settings)
{
    return Settings.MassCount >= 2 && Settings.StepCount > 0 && Settings.FixedDeltaSeconds > 0.0 && Settings.Damping >= 0.0
        && Parameters.ProximalStiffness > 0.0 && Parameters.DistalStiffness > 0.0;
}

void ComputeForces(const FSpringChainParameters& Parameters, const FSpringChainSettings& Settings, const TArray<double>& Position, const TArray<double>& Velocity, TArray<double>& OutForce)
{
    OutForce.Init(0.0, Settings.MassCount);
    for (int32 Mass = 0; Mass < Settings.MassCount; ++Mass)
    {
        const double Current = Position[Mass];
        const double Previous = Mass == 0 ? 0.0 : Position[Mass - 1];
        OutForce[Mass] -= SpringStiffness(Parameters, Mass, Settings.MassCount) * (Current - Previous);
        if (Mass + 1 < Settings.MassCount)
        {
            OutForce[Mass] += SpringStiffness(Parameters, Mass + 1, Settings.MassCount) * (Position[Mass + 1] - Current);
        }
        OutForce[Mass] -= Settings.Damping * Velocity[Mass];
    }
}
}

bool FDifferentiableSpringChainExperiment::BuildTarget(const FSpringChainParameters& Parameters, const FSpringChainSettings& Settings, FSpringChainTarget& OutTarget)
{
    OutTarget.TipPosition.Reset();
    OutTarget.TipVelocity.Reset();
    if (!Validate(Parameters, Settings)) return false;
    TArray<double> Position, Velocity, Force;
    Position.SetNumZeroed(Settings.MassCount);
    Velocity.SetNumZeroed(Settings.MassCount);
    OutTarget.TipPosition.Reserve(Settings.StepCount);
    OutTarget.TipVelocity.Reserve(Settings.StepCount);
    for (int32 Step = 0; Step < Settings.StepCount; ++Step)
    {
        ComputeForces(Parameters, Settings, Position, Velocity, Force);
        Force.Last() += TipForce(Settings, Step);
        for (int32 Mass = 0; Mass < Settings.MassCount; ++Mass)
        {
            Velocity[Mass] += Settings.FixedDeltaSeconds * Force[Mass];
            Position[Mass] += Settings.FixedDeltaSeconds * Velocity[Mass];
        }
        OutTarget.TipPosition.Add(Position.Last());
        OutTarget.TipVelocity.Add(Velocity.Last());
    }
    return true;
}

FSpringChainEvaluation FDifferentiableSpringChainExperiment::Evaluate(const FSpringChainParameters& Parameters, const FSpringChainSettings& Settings, const FSpringChainTarget& Target)
{
    FSpringChainEvaluation Result;
    if (!Validate(Parameters, Settings) || Target.TipPosition.Num() != Settings.StepCount || Target.TipVelocity.Num() != Settings.StepCount)
    {
        Result.Loss = TNumericLimits<double>::Max();
        return Result;
    }
    TArray<double> Position, Velocity, Force;
    Position.SetNumZeroed(Settings.MassCount);
    Velocity.SetNumZeroed(Settings.MassCount);
    TArray<double> DPosition[2], DVelocity[2], DForce[2];
    for (int32 Parameter = 0; Parameter < 2; ++Parameter)
    {
        DPosition[Parameter].SetNumZeroed(Settings.MassCount);
        DVelocity[Parameter].SetNumZeroed(Settings.MassCount);
    }
    double Gradient[2] = {};
    for (int32 Step = 0; Step < Settings.StepCount; ++Step)
    {
        ComputeForces(Parameters, Settings, Position, Velocity, Force);
        Force.Last() += TipForce(Settings, Step);
        for (int32 Parameter = 0; Parameter < 2; ++Parameter)
        {
            DForce[Parameter].Init(0.0, Settings.MassCount);
            for (int32 Mass = 0; Mass < Settings.MassCount; ++Mass)
            {
                const double CurrentDelta = Position[Mass] - (Mass == 0 ? 0.0 : Position[Mass - 1]);
                const double DCurrentDelta = DPosition[Parameter][Mass] - (Mass == 0 ? 0.0 : DPosition[Parameter][Mass - 1]);
                const double KCurrent = SpringStiffness(Parameters, Mass, Settings.MassCount);
                const double DKCurrent = SpringDerivative(Parameter, Mass, Settings.MassCount);
                DForce[Parameter][Mass] -= DKCurrent * CurrentDelta + KCurrent * DCurrentDelta;
                if (Mass + 1 < Settings.MassCount)
                {
                    const double NextDelta = Position[Mass + 1] - Position[Mass];
                    const double DNextDelta = DPosition[Parameter][Mass + 1] - DPosition[Parameter][Mass];
                    const double KNext = SpringStiffness(Parameters, Mass + 1, Settings.MassCount);
                    const double DKNext = SpringDerivative(Parameter, Mass + 1, Settings.MassCount);
                    DForce[Parameter][Mass] += DKNext * NextDelta + KNext * DNextDelta;
                }
                DForce[Parameter][Mass] -= Settings.Damping * DVelocity[Parameter][Mass];
            }
        }
        for (int32 Mass = 0; Mass < Settings.MassCount; ++Mass)
        {
            Velocity[Mass] += Settings.FixedDeltaSeconds * Force[Mass];
            Position[Mass] += Settings.FixedDeltaSeconds * Velocity[Mass];
            for (int32 Parameter = 0; Parameter < 2; ++Parameter)
            {
                DVelocity[Parameter][Mass] += Settings.FixedDeltaSeconds * DForce[Parameter][Mass];
                DPosition[Parameter][Mass] += Settings.FixedDeltaSeconds * DVelocity[Parameter][Mass];
            }
        }
        if (Target.bUseFullTrajectory || Step == Settings.StepCount - 1)
        {
            const double PositionError = Position.Last() - Target.TipPosition[Step];
            const double VelocityError = Velocity.Last() - Target.TipVelocity[Step];
            const double Scale = Target.bUseFullTrajectory ? 1.0 / Settings.StepCount : 1.0;
            Result.Loss += 0.5 * Scale * (Target.PositionWeight * PositionError * PositionError + Target.VelocityWeight * VelocityError * VelocityError);
            for (int32 Parameter = 0; Parameter < 2; ++Parameter)
            {
                Gradient[Parameter] += Scale * (Target.PositionWeight * PositionError * DPosition[Parameter].Last() + Target.VelocityWeight * VelocityError * DVelocity[Parameter].Last());
            }
        }
    }
    Result.FinalTipPosition = Position.Last();
    Result.FinalTipVelocity = Velocity.Last();
    Result.Gradient.ProximalStiffness = Gradient[0];
    Result.Gradient.DistalStiffness = Gradient[1];
    return Result;
}

FSpringChainGradient FDifferentiableSpringChainExperiment::EvaluateCenteredFiniteDifference(const FSpringChainParameters& Parameters, const FSpringChainSettings& Settings, const FSpringChainTarget& Target, const double RelativeEpsilon)
{
    FSpringChainGradient Result;
    const double Values[2] = {Parameters.ProximalStiffness, Parameters.DistalStiffness};
    double* Output[2] = {&Result.ProximalStiffness, &Result.DistalStiffness};
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const double Epsilon = FMath::Max(1.0, FMath::Abs(Values[Index])) * RelativeEpsilon;
        FSpringChainParameters Plus = Parameters, Minus = Parameters;
        double* PlusValues[2] = {&Plus.ProximalStiffness, &Plus.DistalStiffness};
        double* MinusValues[2] = {&Minus.ProximalStiffness, &Minus.DistalStiffness};
        *PlusValues[Index] += Epsilon;
        *MinusValues[Index] -= Epsilon;
        *Output[Index] = (Evaluate(Plus, Settings, Target).Loss - Evaluate(Minus, Settings, Target).Loss) / (2.0 * Epsilon);
    }
    return Result;
}

FSpringChainOptimizationResult FDifferentiableSpringChainExperiment::Optimize(const FSpringChainParameters& Initial, const FSpringChainParameters& Minimum, const FSpringChainParameters& Maximum, const FSpringChainSettings& Settings, const FSpringChainTarget& Target, const int32 MaximumIterations)
{
    FSpringChainOptimizationResult Result;
    const double Min[2] = {Minimum.ProximalStiffness, Minimum.DistalStiffness};
    const double Max[2] = {Maximum.ProximalStiffness, Maximum.DistalStiffness};
    const double Start[2] = {Initial.ProximalStiffness, Initial.DistalStiffness};
    double Q[2] = {FMath::Clamp((Start[0] - Min[0]) / (Max[0] - Min[0]), 0.0, 1.0), FMath::Clamp((Start[1] - Min[1]) / (Max[1] - Min[1]), 0.0, 1.0)};
    auto Decode = [&]() { return FSpringChainParameters{Min[0] + Q[0] * (Max[0] - Min[0]), Min[1] + Q[1] * (Max[1] - Min[1])}; };
    FSpringChainEvaluation Current = Evaluate(Decode(), Settings, Target);
    Result.InitialLoss = Current.Loss;
    Result.LossHistory.Add(Current.Loss);
    for (int32 Iteration = 0; Iteration < MaximumIterations; ++Iteration)
    {
        const double LossBefore = Current.Loss;
        bool bAnyAccepted = false;
        for (int32 Parameter = 0; Parameter < 2; ++Parameter)
        {
            const double Physical[2] = {Current.Gradient.ProximalStiffness, Current.Gradient.DistalStiffness};
            const double G = Physical[Parameter] * (Max[Parameter] - Min[Parameter]);
            if (FMath::Abs(G) < 1.0e-15) continue;
            const double Previous = Q[Parameter];
            double StepLength = 0.1;
            for (int32 Search = 0; Search < 28; ++Search)
            {
                Q[Parameter] = FMath::Clamp(Previous - FMath::Sign(G) * StepLength, 0.0, 1.0);
                const FSpringChainEvaluation Candidate = Evaluate(Decode(), Settings, Target);
                if (Candidate.Loss < Current.Loss) { Current = Candidate; bAnyAccepted = true; break; }
                Q[Parameter] = Previous;
                StepLength *= 0.5;
            }
        }
        if (!bAnyAccepted) break;
        Result.LossHistory.Add(Current.Loss);
        Result.Iterations = Iteration + 1;
        if (Current.Loss < 1.0e-10) { Result.bConverged = true; break; }
        if (LossBefore - Current.Loss < FMath::Max(1.0, LossBefore) * 1.0e-13) { Result.bConverged = true; break; }
    }
    Result.Parameters = Decode();
    Result.FinalLoss = Current.Loss;
    Result.bConverged = Result.bConverged || Result.FinalLoss < Result.InitialLoss * 1.0e-6;
    return Result;
}
