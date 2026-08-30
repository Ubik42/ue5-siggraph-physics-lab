#include "ArticulatedRecoveryExperiment.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArticulatedRecoveryTest, "PhysicsResearch.DrivenCharacter.Articulated.Recovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArticulatedRecoveryTest::RunTest(const FString& Parameters)
{
    FArticulatedRecoverySettings Settings;
    const FArticulatedRecoveryResult Result = FArticulatedRecoveryExperiment::Simulate(Settings);
    AddInfo(FString::Printf(TEXT("M6_ARTICULATED_RECOVERY recovered=%s failed=%s initial_rms_deg=%.9g final_rms_deg=%.9g recovery_time_s=%.9g peak_torque_hip_nm=%.9g peak_torque_knee_nm=%.9g peak_torque_spine_nm=%.9g work_j=%.9g steps=%d"),
        Result.bRecovered ? TEXT("true") : TEXT("false"), Result.bFailed ? TEXT("true") : TEXT("false"),
        FMath::RadiansToDegrees(Result.InitialRmsAngleRadians), FMath::RadiansToDegrees(Result.FinalRmsAngleRadians), Result.RecoveryTimeSeconds,
        Result.PeakAbsoluteControlTorquesNewtonMeters.X, Result.PeakAbsoluteControlTorquesNewtonMeters.Y,
        Result.PeakAbsoluteControlTorquesNewtonMeters.Z, Result.AbsoluteMechanicalWorkJoules, Result.SimulatedSteps));
    TestTrue(TEXT("Coupled three-joint posture recovers"), Result.bValid && Result.bRecovered && !Result.bFailed);
    TestTrue(TEXT("Final RMS angle meets the explicit gate"), Result.FinalRmsAngleRadians <= Settings.SuccessRmsAngleRadians);
    TestTrue(TEXT("All joint torques respect their budgets"),
        Result.PeakAbsoluteControlTorquesNewtonMeters.X <= Settings.MaximumTorqueNewtonMeters.X + 1.0e-9
        && Result.PeakAbsoluteControlTorquesNewtonMeters.Y <= Settings.MaximumTorqueNewtonMeters.Y + 1.0e-9
        && Result.PeakAbsoluteControlTorquesNewtonMeters.Z <= Settings.MaximumTorqueNewtonMeters.Z + 1.0e-9);
    TestTrue(TEXT("Recovery performs finite positive mechanical work"), Result.AbsoluteMechanicalWorkJoules > 0.0 && FMath::IsFinite(Result.AbsoluteMechanicalWorkJoules));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArticulatedCouplingAblationTest, "PhysicsResearch.DrivenCharacter.Articulated.CouplingAblation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArticulatedCouplingAblationTest::RunTest(const FString& Parameters)
{
    FArticulatedRecoverySettings Coupled;
    Coupled.InitialAnglesRadians = FVector::ZeroVector;
    Coupled.DistalAngularImpulseNewtonMeterSeconds = 12.0;
    FArticulatedRecoverySettings Uncoupled = Coupled;
    Uncoupled.CouplingStiffnessNewtonMetersPerRadian = 0.0;
    Uncoupled.CouplingDampingNewtonMeterSeconds = 0.0;
    const FArticulatedRecoveryResult CoupledResult = FArticulatedRecoveryExperiment::Simulate(Coupled);
    const FArticulatedRecoveryResult UncoupledResult = FArticulatedRecoveryExperiment::Simulate(Uncoupled);
    const double TransferRatio = CoupledResult.PeakProximalResponseRadians / FMath::Max(CoupledResult.PeakDistalResponseRadians, 1.0e-12);
    AddInfo(FString::Printf(TEXT("M6_ARTICULATED_COUPLING impulse_nms=%.9g coupled_proximal_deg=%.9g coupled_distal_deg=%.9g transfer_ratio=%.9g uncoupled_proximal_deg=%.9g coupled_recovered=%s uncoupled_recovered=%s"),
        Coupled.DistalAngularImpulseNewtonMeterSeconds, FMath::RadiansToDegrees(CoupledResult.PeakProximalResponseRadians),
        FMath::RadiansToDegrees(CoupledResult.PeakDistalResponseRadians), TransferRatio,
        FMath::RadiansToDegrees(UncoupledResult.PeakProximalResponseRadians), CoupledResult.bRecovered ? TEXT("true") : TEXT("false"),
        UncoupledResult.bRecovered ? TEXT("true") : TEXT("false")));
    TestTrue(TEXT("Both controllers recover after the same distal impulse"), CoupledResult.bRecovered && UncoupledResult.bRecovered);
    TestTrue(TEXT("Physical coupling transfers a measurable response upstream"), TransferRatio > 0.05);
    TestTrue(TEXT("Uncoupled ablation keeps proximal joints isolated"), UncoupledResult.PeakProximalResponseRadians < FMath::DegreesToRadians(0.001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArticulatedTorqueFailureTest, "PhysicsResearch.DrivenCharacter.Articulated.TorqueFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FArticulatedTorqueFailureTest::RunTest(const FString& Parameters)
{
    FArticulatedRecoverySettings Settings;
    Settings.MaximumTorqueNewtonMeters = FVector(5.0, 4.0, 3.0);
    const FArticulatedRecoveryResult Result = FArticulatedRecoveryExperiment::Simulate(Settings);
    AddInfo(FString::Printf(TEXT("M6_ARTICULATED_FAILURE recovered=%s failed=%s final_rms_deg=%.9g torque_limit_hip_nm=%.9g torque_limit_knee_nm=%.9g torque_limit_spine_nm=%.9g peak_hip_nm=%.9g peak_knee_nm=%.9g peak_spine_nm=%.9g expected_failure=true"),
        Result.bRecovered ? TEXT("true") : TEXT("false"), Result.bFailed ? TEXT("true") : TEXT("false"),
        FMath::RadiansToDegrees(Result.FinalRmsAngleRadians), Settings.MaximumTorqueNewtonMeters.X, Settings.MaximumTorqueNewtonMeters.Y,
        Settings.MaximumTorqueNewtonMeters.Z, Result.PeakAbsoluteControlTorquesNewtonMeters.X,
        Result.PeakAbsoluteControlTorquesNewtonMeters.Y, Result.PeakAbsoluteControlTorquesNewtonMeters.Z));
    TestTrue(TEXT("Underpowered articulated controller fails instead of snapping upright"), Result.bValid && Result.bFailed && !Result.bRecovered);
    TestTrue(TEXT("Failure remains visibly outside the success pose"), Result.FinalRmsAngleRadians > Settings.SuccessRmsAngleRadians);
    TestTrue(TEXT("Failure respects all three torque caps"),
        Result.PeakAbsoluteControlTorquesNewtonMeters.X <= Settings.MaximumTorqueNewtonMeters.X + 1.0e-9
        && Result.PeakAbsoluteControlTorquesNewtonMeters.Y <= Settings.MaximumTorqueNewtonMeters.Y + 1.0e-9
        && Result.PeakAbsoluteControlTorquesNewtonMeters.Z <= Settings.MaximumTorqueNewtonMeters.Z + 1.0e-9);
    return true;
}

#endif
