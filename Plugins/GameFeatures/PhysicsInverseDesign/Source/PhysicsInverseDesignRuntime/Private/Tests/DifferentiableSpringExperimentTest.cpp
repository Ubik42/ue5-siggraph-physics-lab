#include "DifferentiableSpringExperiment.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDifferentiableSpringGradientTest,
    "PhysicsResearch.InverseDesign.SpringGradient",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDifferentiableSpringGradientTest::RunTest(const FString& Parameters)
{
    const FSpringDesignParameters Design{31.0, 13.0, 2.25};
    const FSpringDesignTarget Target{1.7, -0.15, 1.0, 0.2};
    const FSpringDesignEvaluation Tangent = FDifferentiableSpringExperiment::Evaluate(Design, Target, 1.0 / 120.0, 240);
    const FSpringDesignGradient FiniteDifference = FDifferentiableSpringExperiment::EvaluateCenteredFiniteDifference(Design, Target, 1.0 / 120.0, 240);

    const double TangentValues[3] = {Tangent.Gradient.DriveForce, Tangent.Gradient.Stiffness, Tangent.Gradient.Damping};
    const double FiniteValues[3] = {FiniteDifference.DriveForce, FiniteDifference.Stiffness, FiniteDifference.Damping};
    const TCHAR* Names[3] = {TEXT("DriveForce"), TEXT("Stiffness"), TEXT("Damping")};
    double MaximumRelativeError = 0.0;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const double RelativeError = FMath::Abs(TangentValues[Index] - FiniteValues[Index]) / FMath::Max(1.0e-8, FMath::Abs(FiniteValues[Index]));
        MaximumRelativeError = FMath::Max(MaximumRelativeError, RelativeError);
        TestTrue(FString::Printf(TEXT("%s tangent gradient is finite"), Names[Index]), FMath::IsFinite(TangentValues[Index]));
        TestTrue(FString::Printf(TEXT("%s agrees with centered finite difference (relative=%g)"), Names[Index], RelativeError), RelativeError < 2.0e-5);
    }
    AddInfo(FString::Printf(TEXT("M5_INVERSE_GRADIENT steps=240 dt=%.12g drive=%g stiffness=%g damping=%g maximum_relative_error=%.12g"), 1.0 / 120.0, Design.DriveForce, Design.Stiffness, Design.Damping, MaximumRelativeError));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDifferentiableSpringOptimizationTest,
    "PhysicsResearch.InverseDesign.SpringOptimization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDifferentiableSpringOptimizationTest::RunTest(const FString& Parameters)
{
    const double Dt = 1.0 / 120.0;
    constexpr int32 Steps = 240;
    const FSpringDesignParameters Truth{34.0, 14.0, 2.8};
    const FSpringDesignEvaluation TruthState = FDifferentiableSpringExperiment::Evaluate(Truth, {}, Dt, Steps);
    const FSpringDesignTarget Target{TruthState.FinalState.Position, TruthState.FinalState.Velocity, 1.0, 0.25};
    const FSpringDesignParameters Initial{12.0, Truth.Stiffness, 0.4};
    const FSpringDesignBounds Bounds{{0.0, Truth.Stiffness, 0.05}, {60.0, Truth.Stiffness + 1.0, 8.0}};
    const double StartSeconds = FPlatformTime::Seconds();
    const FSpringDesignOptimizationResult Result = FDifferentiableSpringExperiment::Optimize(Initial, Bounds, Target, Dt, Steps, true, false, true, 250);
    const double OptimizationMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;

    TestTrue(TEXT("optimizer reduces loss by at least four orders"), Result.FinalLoss < Result.InitialLoss * 1.0e-4);
    TestTrue(TEXT("optimizer records a multi-step convergence curve"), Result.LossHistory.Num() > 5);
    TestTrue(TEXT("optimizer reports convergence"), Result.bConverged);
    TestTrue(TEXT("recovered drive force is close to truth"), FMath::Abs(Result.Parameters.DriveForce - Truth.DriveForce) < 0.25);
    TestTrue(TEXT("recovered damping is close to truth"), FMath::Abs(Result.Parameters.Damping - Truth.Damping) < 0.15);
    AddInfo(FString::Printf(TEXT("M5_INVERSE_OPTIMIZATION iterations=%d history=%d initial_loss=%.12g final_loss=%.12g recovered_drive=%.12g recovered_damping=%.12g truth_drive=%.12g truth_damping=%.12g optimization_ms=%.6f"), Result.Iterations, Result.LossHistory.Num(), Result.InitialLoss, Result.FinalLoss, Result.Parameters.DriveForce, Result.Parameters.Damping, Truth.DriveForce, Truth.Damping, OptimizationMilliseconds));
    TArray<FString> LossValues;
    LossValues.Reserve(Result.LossHistory.Num());
    for (const double Loss : Result.LossHistory)
    {
        LossValues.Add(FString::Printf(TEXT("%.12g"), Loss));
    }
    AddInfo(FString::Printf(TEXT("M5_INVERSE_LOSS_HISTORY values=%s"), *FString::Join(LossValues, TEXT(","))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDifferentiableSpringIdentifiabilityBoundaryTest,
    "PhysicsResearch.InverseDesign.SpringIdentifiabilityBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDifferentiableSpringIdentifiabilityBoundaryTest::RunTest(const FString& Parameters)
{
    const double Dt = 1.0 / 120.0;
    constexpr int32 Steps = 30;
    const FSpringDesignParameters Truth{34.0, 14.0, 2.8};
    const FSpringDesignEvaluation TruthState = FDifferentiableSpringExperiment::Evaluate(Truth, {}, Dt, Steps);
    const FSpringDesignTarget PositionOnlyTarget{TruthState.FinalState.Position, 0.0, 1.0, 0.0};
    const FSpringDesignParameters Initial{12.0, Truth.Stiffness, 7.0};
    const FSpringDesignBounds Bounds{{0.0, Truth.Stiffness, 0.05}, {60.0, Truth.Stiffness + 1.0, 8.0}};
    const FSpringDesignOptimizationResult Result = FDifferentiableSpringExperiment::Optimize(Initial, Bounds, PositionOnlyTarget, Dt, Steps, true, false, true, 250);
    const double ParameterDistance = FMath::Abs(Result.Parameters.DriveForce - Truth.DriveForce) + FMath::Abs(Result.Parameters.Damping - Truth.Damping);

    TestTrue(TEXT("position-only objective can be fitted"), Result.FinalLoss < Result.InitialLoss * 1.0e-4);
    TestTrue(TEXT("position-only short horizon does not uniquely recover physical parameters"), ParameterDistance > 1.0);
    AddInfo(FString::Printf(TEXT("M5_INVERSE_IDENTIFIABILITY steps=%d initial_loss=%.12g final_loss=%.12g recovered_drive=%.12g recovered_damping=%.12g parameter_l1_error=%.12g interpretation=position_only_short_horizon_is_non_identifiable"), Steps, Result.InitialLoss, Result.FinalLoss, Result.Parameters.DriveForce, Result.Parameters.Damping, ParameterDistance));
    return true;
}

#endif
