#include "DifferentiableSpringChainExperiment.h"

#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FSpringChainTarget MakeChainTarget(const FSpringChainParameters& Truth, const FSpringChainSettings& Settings)
{
    FSpringChainTarget Target;
    FDifferentiableSpringChainExperiment::BuildTarget(Truth, Settings, Target);
    return Target;
}

double RelativeError(const double A, const double B)
{
    return FMath::Abs(A - B) / FMath::Max(1.0e-12, FMath::Max(FMath::Abs(A), FMath::Abs(B)));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpringChainGradientTest, "PhysicsResearch.InverseDesign.SpringChainGradient", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSpringChainGradientTest::RunTest(const FString& Parameters)
{
    const FSpringChainSettings Settings;
    const FSpringChainTarget Target = MakeChainTarget({70.0, 30.0}, Settings);
    const FSpringChainParameters Probe{25.0, 80.0};
    const FSpringChainEvaluation Tangent = FDifferentiableSpringChainExperiment::Evaluate(Probe, Settings, Target);
    const FSpringChainGradient Finite = FDifferentiableSpringChainExperiment::EvaluateCenteredFiniteDifference(Probe, Settings, Target);
    const double ProximalError = RelativeError(Tangent.Gradient.ProximalStiffness, Finite.ProximalStiffness);
    const double DistalError = RelativeError(Tangent.Gradient.DistalStiffness, Finite.DistalStiffness);
    AddInfo(FString::Printf(TEXT("chain_gradient proximal_rel_error=%.12g distal_rel_error=%.12g"), ProximalError, DistalError));
    TestTrue(TEXT("Proximal tangent agrees with centered finite difference"), ProximalError < 2.0e-5);
    TestTrue(TEXT("Distal tangent agrees with centered finite difference"), DistalError < 2.0e-5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpringChainOptimizationTest, "PhysicsResearch.InverseDesign.SpringChainOptimization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSpringChainOptimizationTest::RunTest(const FString& Parameters)
{
    const FSpringChainSettings Settings;
    const FSpringChainParameters Truth{70.0, 30.0};
    const FSpringChainTarget Target = MakeChainTarget(Truth, Settings);
    const double StartSeconds = FPlatformTime::Seconds();
    const FSpringChainOptimizationResult Result = FDifferentiableSpringChainExperiment::Optimize({25.0, 80.0}, {5.0, 5.0}, {120.0, 120.0}, Settings, Target, 400);
    const double ElapsedMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
    const double ParameterL1 = FMath::Abs(Result.Parameters.ProximalStiffness - Truth.ProximalStiffness) + FMath::Abs(Result.Parameters.DistalStiffness - Truth.DistalStiffness);
    AddInfo(FString::Printf(TEXT("chain_optimization initial_loss=%.12g final_loss=%.12g iterations=%d proximal=%.9g distal=%.9g parameter_l1=%.9g optimization_ms=%.6f samples=%d"), Result.InitialLoss, Result.FinalLoss, Result.Iterations, Result.Parameters.ProximalStiffness, Result.Parameters.DistalStiffness, ParameterL1, ElapsedMilliseconds, Result.LossHistory.Num()));
    TArray<FString> LossValues;
    for (const double Loss : Result.LossHistory) LossValues.Add(FString::Printf(TEXT("%.12g"), Loss));
    AddInfo(FString::Printf(TEXT("chain_loss_history values=%s"), *FString::Join(LossValues, TEXT(","))));
    TestTrue(TEXT("Full-trajectory chain loss falls by at least six orders"), Result.FinalLoss < Result.InitialLoss * 1.0e-6);
    TestTrue(TEXT("Full-trajectory chain recovers both segment stiffnesses"), ParameterL1 < 1.0);
    TestTrue(TEXT("Optimization records a decreasing history"), Result.LossHistory.Num() > 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpringChainTerminalOnlyBoundaryTest, "PhysicsResearch.InverseDesign.SpringChainTerminalOnlyBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSpringChainTerminalOnlyBoundaryTest::RunTest(const FString& Parameters)
{
    const FSpringChainSettings Settings;
    const FSpringChainParameters Truth{70.0, 30.0};
    FSpringChainTarget Target = MakeChainTarget(Truth, Settings);
    Target.bUseFullTrajectory = false;
    Target.VelocityWeight = 0.0;
    const FSpringChainOptimizationResult Result = FDifferentiableSpringChainExperiment::Optimize({20.0, 90.0}, {5.0, 5.0}, {120.0, 120.0}, Settings, Target, 400);
    const double ParameterL1 = FMath::Abs(Result.Parameters.ProximalStiffness - Truth.ProximalStiffness) + FMath::Abs(Result.Parameters.DistalStiffness - Truth.DistalStiffness);
    AddInfo(FString::Printf(TEXT("chain_terminal_boundary initial_loss=%.12g final_loss=%.12g proximal=%.9g distal=%.9g parameter_l1=%.9g expected_failure=true"), Result.InitialLoss, Result.FinalLoss, Result.Parameters.ProximalStiffness, Result.Parameters.DistalStiffness, ParameterL1));
    TestTrue(TEXT("Terminal-only observation can fit the scalar endpoint"), Result.FinalLoss < Result.InitialLoss * 1.0e-6);
    TestTrue(TEXT("Terminal-only observation does not identify both segment stiffnesses"), ParameterL1 > 5.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpringChainCostScalingTest, "PhysicsResearch.InverseDesign.SpringChainCostScaling", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSpringChainCostScalingTest::RunTest(const FString& Parameters)
{
    constexpr int32 EvaluationCount = 96;
    for (const int32 MassCount : {4, 8, 16})
    {
        FSpringChainSettings Settings;
        Settings.MassCount = MassCount;
        const FSpringChainTarget Target = MakeChainTarget({70.0, 30.0}, Settings);
        TArray<double> Samples;
        Samples.Reserve(EvaluationCount);
        double LossAccumulator = 0.0;
        for (int32 Sample = 0; Sample < EvaluationCount; ++Sample)
        {
            const double StartSeconds = FPlatformTime::Seconds();
            LossAccumulator += FDifferentiableSpringChainExperiment::Evaluate({25.0, 80.0}, Settings, Target).Loss;
            Samples.Add((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        }
        Samples.Sort();
        double Sum = 0.0;
        for (const double Sample : Samples) Sum += Sample;
        const double Mean = Sum / Samples.Num();
        const double P95 = Samples[FMath::Clamp(FMath::CeilToInt(0.95 * Samples.Num()) - 1, 0, Samples.Num() - 1)];
        AddInfo(FString::Printf(TEXT("chain_cost mass_count=%d steps=%d evaluations=%d operation_proxy=%d mean_ms=%.9f p95_ms=%.9f checksum=%.9g"), MassCount, Settings.StepCount, EvaluationCount, MassCount * Settings.StepCount, Mean, P95, LossAccumulator));
        TestTrue(TEXT("Cost sample is finite and positive"), FMath::IsFinite(Mean) && Mean > 0.0 && FMath::IsFinite(P95) && P95 > 0.0);
    }
    return true;
}

#endif
