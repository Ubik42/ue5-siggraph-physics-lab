#include "PlanarCharacterRecoveryExperiment.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanarPushRejectionTest, "PhysicsResearch.DrivenCharacter.PlanarPushRejection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlanarPushRejectionTest::RunTest(const FString& Parameters)
{
    FPlanarCharacterRecoverySettings Settings;
    Settings.AngularImpulseNewtonMeterSeconds = 55.0;
    const FPlanarCharacterRecoveryResult Result = FPlanarCharacterRecoveryExperiment::Simulate(Settings);
    AddInfo(FString::Printf(TEXT("M6_PLANAR_PUSH impulse_nms=%.9g fell=%s recovered=%s max_angle_deg=%.9g final_error_deg=%.9g min_support_margin_m=%.9g peak_torque_nm=%.9g work_j=%.9g"),
        Settings.AngularImpulseNewtonMeterSeconds, Result.bFell ? TEXT("true") : TEXT("false"), Result.bRecovered ? TEXT("true") : TEXT("false"),
        FMath::RadiansToDegrees(Result.MaximumAbsoluteAngleRadians), FMath::RadiansToDegrees(Result.FinalPoseErrorRadians), Result.MinimumSupportMarginMeters,
        Result.PeakAbsoluteTorqueNewtonMeters, Result.AbsoluteMechanicalWorkJoules));
    TestTrue(TEXT("Moderate push remains a balance-rejection case"), Result.bValid && !Result.bFell && !Result.bFailed);
    TestTrue(TEXT("Moderate push returns close to upright"), Result.FinalPoseErrorRadians < FMath::DegreesToRadians(0.25));
    TestTrue(TEXT("Torque remains inside the explicit budget"), Result.PeakAbsoluteTorqueNewtonMeters <= Settings.MaximumTorqueNewtonMeters + 1.0e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanarFallAndRecoveryTest, "PhysicsResearch.DrivenCharacter.PlanarFallAndRecovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlanarFallAndRecoveryTest::RunTest(const FString& Parameters)
{
    FPlanarCharacterRecoverySettings Settings;
    Settings.AngularImpulseNewtonMeterSeconds = 120.0;
    const FPlanarCharacterRecoveryResult Result = FPlanarCharacterRecoveryExperiment::Simulate(Settings);
    AddInfo(FString::Printf(TEXT("M6_PLANAR_RECOVERY impulse_nms=%.9g fell=%s recovered=%s failed=%s front_get_up=%s impacts=%d fall_time_s=%.9g recovery_start_s=%.9g recovery_time_s=%.9g get_up_control_time_s=%.9g final_error_deg=%.9g peak_torque_nm=%.9g work_j=%.9g samples=%d"),
        Settings.AngularImpulseNewtonMeterSeconds, Result.bFell ? TEXT("true") : TEXT("false"), Result.bRecovered ? TEXT("true") : TEXT("false"), Result.bFailed ? TEXT("true") : TEXT("false"),
        Result.bFrontGetUp ? TEXT("true") : TEXT("false"), Result.GroundImpactCount, Result.FallTimeSeconds, Result.RecoveryStartTimeSeconds, Result.RecoveryTimeSeconds,
        Result.GetUpControlTimeSeconds, FMath::RadiansToDegrees(Result.FinalPoseErrorRadians), Result.PeakAbsoluteTorqueNewtonMeters, Result.AbsoluteMechanicalWorkJoules, Result.Samples.Num()));
    EPlanarCharacterRecoveryPhase PreviousPhase = EPlanarCharacterRecoveryPhase::Balanced;
    for (const FPlanarCharacterRecoverySample& Sample : Result.Samples)
    {
        const bool bPhaseTransition = Sample.Step == 0 || Sample.Phase != PreviousPhase;
        if (bPhaseTransition || Sample.Step % 12 == 0 || Sample.Step == Result.Samples.Num() - 1)
        {
            AddInfo(FString::Printf(TEXT("M6_PLANAR_SAMPLE step=%d time_s=%.9g angle_deg=%.9g angular_velocity_deg_s=%.9g target_deg=%.9g torque_nm=%.9g support_margin_m=%.9g phase=%s"),
                Sample.Step, Sample.TimeSeconds, FMath::RadiansToDegrees(Sample.AngleRadians), FMath::RadiansToDegrees(Sample.AngularVelocityRadiansPerSecond),
                FMath::RadiansToDegrees(Sample.TargetAngleRadians), Sample.AppliedTorqueNewtonMeters, Sample.SupportMarginMeters,
                FPlanarCharacterRecoveryExperiment::PhaseName(Sample.Phase)));
        }
        PreviousPhase = Sample.Phase;
    }
    TestTrue(TEXT("Large push crosses the support boundary"), Result.bValid && Result.bFell);
    TestTrue(TEXT("Phase-aware controller completes physical recovery"), Result.bRecovered && !Result.bFailed);
    TestTrue(TEXT("Recovery includes a ground impact and settling phase"), Result.GroundImpactCount > 0 && Result.RecoveryStartTimeSeconds > Result.FallTimeSeconds);
    TestEqual(TEXT("Persistent unilateral ground contact records one impact episode"), Result.GroundImpactCount, 1);
    TestTrue(TEXT("Recovered pose meets the explicit angle gate"), Result.FinalPoseErrorRadians < Settings.SuccessAngleRadians);
    TestTrue(TEXT("Recovery completes inside the timeout"), Result.RecoveryTimeSeconds > 0.0 && Result.RecoveryTimeSeconds < Settings.FailureTimeoutSeconds);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanarFrontBackSymmetryTest, "PhysicsResearch.DrivenCharacter.PlanarFrontBackSymmetry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlanarFrontBackSymmetryTest::RunTest(const FString& Parameters)
{
    FPlanarCharacterRecoverySettings FrontSettings;
    FrontSettings.AngularImpulseNewtonMeterSeconds = 120.0;
    FPlanarCharacterRecoverySettings BackSettings = FrontSettings;
    BackSettings.AngularImpulseNewtonMeterSeconds *= -1.0;
    const FPlanarCharacterRecoveryResult Front = FPlanarCharacterRecoveryExperiment::Simulate(FrontSettings);
    const FPlanarCharacterRecoveryResult Back = FPlanarCharacterRecoveryExperiment::Simulate(BackSettings);
    const double RecoveryTimeDifference = FMath::Abs(Front.RecoveryTimeSeconds - Back.RecoveryTimeSeconds);
    const double WorkDifference = FMath::Abs(Front.AbsoluteMechanicalWorkJoules - Back.AbsoluteMechanicalWorkJoules);
    AddInfo(FString::Printf(TEXT("M6_PLANAR_SYMMETRY front_recovered=%s back_recovered=%s front_orientation=%s back_orientation=%s recovery_time_difference_s=%.12g work_difference_j=%.12g"),
        Front.bRecovered ? TEXT("true") : TEXT("false"), Back.bRecovered ? TEXT("true") : TEXT("false"), Front.bFrontGetUp ? TEXT("front") : TEXT("back"),
        Back.bFrontGetUp ? TEXT("front") : TEXT("back"), RecoveryTimeDifference, WorkDifference));
    TestTrue(TEXT("Both push directions recover"), Front.bRecovered && Back.bRecovered);
    TestTrue(TEXT("Front and back orientation branches are distinct"), Front.bFrontGetUp && !Back.bFrontGetUp);
    TestTrue(TEXT("Symmetric model has symmetric recovery time"), RecoveryTimeDifference < FrontSettings.FixedDeltaSeconds * 0.5);
    TestTrue(TEXT("Symmetric model has symmetric mechanical work"), WorkDifference < 1.0e-8);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanarTorqueBudgetFailureTest, "PhysicsResearch.DrivenCharacter.PlanarTorqueBudgetFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPlanarTorqueBudgetFailureTest::RunTest(const FString& Parameters)
{
    FPlanarCharacterRecoverySettings Settings;
    Settings.AngularImpulseNewtonMeterSeconds = 120.0;
    Settings.MaximumTorqueNewtonMeters = 400.0;
    const FPlanarCharacterRecoveryResult Result = FPlanarCharacterRecoveryExperiment::Simulate(Settings);
    AddInfo(FString::Printf(TEXT("M6_PLANAR_TORQUE_FAILURE torque_limit_nm=%.9g gravity_peak_nm=%.9g fell=%s recovered=%s failed=%s final_error_deg=%.9g peak_torque_nm=%.9g expected_failure=true"),
        Settings.MaximumTorqueNewtonMeters, Settings.MassKilograms * Settings.GravityMetersPerSecondSquared * Settings.CenterOfMassHeightMeters,
        Result.bFell ? TEXT("true") : TEXT("false"), Result.bRecovered ? TEXT("true") : TEXT("false"), Result.bFailed ? TEXT("true") : TEXT("false"),
        FMath::RadiansToDegrees(Result.FinalPoseErrorRadians), Result.PeakAbsoluteTorqueNewtonMeters));
    TestTrue(TEXT("Underpowered controller still records the fall"), Result.bValid && Result.bFell);
    TestTrue(TEXT("Torque below peak gravity cannot complete get-up"), Result.bFailed && !Result.bRecovered);
    TestTrue(TEXT("Failure respects the same torque limit"), Result.PeakAbsoluteTorqueNewtonMeters <= Settings.MaximumTorqueNewtonMeters + 1.0e-9);
    TestTrue(TEXT("Failure remains visibly far from upright"), Result.FinalPoseErrorRadians > FMath::DegreesToRadians(45.0));
    return true;
}

#endif
