#include "DifferentiableBallisticExperiment.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
FBallisticTarget MakeBallisticTarget(const FBallisticDesignParameters& Truth, const double Dt, const int32 Steps)
{
    const FBallisticEvaluation TruthEvaluation = FDifferentiableBallisticExperiment::Evaluate(Truth, {}, Dt, Steps);
    FBallisticTarget Target;
    Target.State = TruthEvaluation.FinalState;
    return Target;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBallisticGradientTest, "PhysicsResearch.InverseDesign.BallisticGradient", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBallisticGradientTest::RunTest(const FString& Parameters)
{
    const double Dt = 1.0 / 120.0;
    constexpr int32 Steps = 240;
    const FBallisticDesignParameters Truth{1200.0, 1600.0, 0.35};
    const FBallisticTarget Target = MakeBallisticTarget(Truth, Dt, Steps);
    const FBallisticDesignParameters Probe{850.0, 1250.0, 0.9};
    const FBallisticEvaluation Tangent = FDifferentiableBallisticExperiment::Evaluate(Probe, Target, Dt, Steps);
    const FBallisticGradient Finite = FDifferentiableBallisticExperiment::EvaluateCenteredFiniteDifference(Probe, Target, Dt, Steps);
    const double A[3] = {Tangent.Gradient.InitialVelocityX, Tangent.Gradient.InitialVelocityZ, Tangent.Gradient.LinearDrag};
    const double F[3] = {Finite.InitialVelocityX, Finite.InitialVelocityZ, Finite.LinearDrag};
    double MaximumRelativeError = 0.0;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        MaximumRelativeError = FMath::Max(MaximumRelativeError, FMath::Abs(A[Index] - F[Index]) / FMath::Max(1.0e-8, FMath::Abs(F[Index])));
    }
    TestTrue(TEXT("ballistic tangent matches centered finite difference"), MaximumRelativeError < 2.0e-5);
    AddInfo(FString::Printf(TEXT("M5_BALLISTIC_GRADIENT steps=%d dt=%.12g maximum_relative_error=%.12g"), Steps, Dt, MaximumRelativeError));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBallisticOptimizationTest, "PhysicsResearch.InverseDesign.BallisticOptimization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBallisticOptimizationTest::RunTest(const FString& Parameters)
{
    const double Dt = 1.0 / 120.0;
    constexpr int32 Steps = 240;
    const FBallisticDesignParameters Truth{1200.0, 1600.0, 0.35};
    const FBallisticTarget Target = MakeBallisticTarget(Truth, Dt, Steps);
    const double StartSeconds = FPlatformTime::Seconds();
    const FBallisticOptimizationResult Result = FDifferentiableBallisticExperiment::Optimize({700.0, 1000.0, 1.2}, {}, Target, Dt, Steps, true, true, true, 400);
    const double OptimizationMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
    const double ParameterError = FMath::Abs(Result.Parameters.InitialVelocityX - Truth.InitialVelocityX) + FMath::Abs(Result.Parameters.InitialVelocityZ - Truth.InitialVelocityZ) + 1000.0 * FMath::Abs(Result.Parameters.LinearDrag - Truth.LinearDrag);
    TestTrue(TEXT("ballistic optimizer reduces loss by six orders"), Result.FinalLoss < Result.InitialLoss * 1.0e-6);
    TestTrue(TEXT("ballistic parameters recover within scaled L1 gate"), ParameterError < 2.0);
    TestTrue(TEXT("ballistic convergence curve has samples"), Result.LossHistory.Num() > 5);
    AddInfo(FString::Printf(TEXT("M5_BALLISTIC_OPTIMIZATION iterations=%d history=%d initial_loss=%.12g final_loss=%.12g recovered_vx=%.12g recovered_vz=%.12g recovered_drag=%.12g parameter_scaled_l1_error=%.12g optimization_ms=%.6f"), Result.Iterations, Result.LossHistory.Num(), Result.InitialLoss, Result.FinalLoss, Result.Parameters.InitialVelocityX, Result.Parameters.InitialVelocityZ, Result.Parameters.LinearDrag, ParameterError, OptimizationMilliseconds));
    TArray<FString> LossValues;
    for (const double Loss : Result.LossHistory) LossValues.Add(FString::Printf(TEXT("%.12g"), Loss));
    AddInfo(FString::Printf(TEXT("M5_BALLISTIC_LOSS_HISTORY values=%s"), *FString::Join(LossValues, TEXT(","))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBallisticIdentifiabilityTest, "PhysicsResearch.InverseDesign.BallisticLandingOnlyBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBallisticIdentifiabilityTest::RunTest(const FString& Parameters)
{
    const double Dt = 1.0 / 120.0;
    constexpr int32 Steps = 120;
    const FBallisticDesignParameters Truth{1200.0, 1600.0, 0.35};
    FBallisticTarget Target = MakeBallisticTarget(Truth, Dt, Steps);
    Target.PositionWeight = FVector2d(1.0, 0.0);
    Target.VelocityWeight = FVector2d::ZeroVector;
    const FBallisticOptimizationResult Result = FDifferentiableBallisticExperiment::Optimize({650.0, Truth.InitialVelocityZ, 1.5}, {}, Target, Dt, Steps, true, false, true, 300);
    const double ParameterError = FMath::Abs(Result.Parameters.InitialVelocityX - Truth.InitialVelocityX) + 1000.0 * FMath::Abs(Result.Parameters.LinearDrag - Truth.LinearDrag);
    TestTrue(TEXT("landing-only objective fits horizontal position"), Result.FinalLoss < Result.InitialLoss * 1.0e-6);
    TestTrue(TEXT("landing-only target is non-identifiable"), ParameterError > 50.0);
    AddInfo(FString::Printf(TEXT("M5_BALLISTIC_IDENTIFIABILITY initial_loss=%.12g final_loss=%.12g recovered_vx=%.12g recovered_drag=%.12g parameter_scaled_l1_error=%.12g interpretation=landing_x_only_is_non_identifiable"), Result.InitialLoss, Result.FinalLoss, Result.Parameters.InitialVelocityX, Result.Parameters.LinearDrag, ParameterError));
    return true;
}

#endif
