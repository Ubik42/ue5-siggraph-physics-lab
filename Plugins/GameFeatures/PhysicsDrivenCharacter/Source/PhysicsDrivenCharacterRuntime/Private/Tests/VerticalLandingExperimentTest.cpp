#include "VerticalLandingExperiment.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVerticalLandingAbsorptionTest, "PhysicsResearch.DrivenCharacter.VerticalLanding.Absorption", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVerticalLandingAbsorptionTest::RunTest(const FString& Parameters)
{
    FVerticalLandingSettings Settings;
    const FVerticalLandingResult Result = FVerticalLandingExperiment::Simulate(Settings);
    const double ExpectedImpactSpeed = FMath::Sqrt(2.0 * Settings.GravityMetersPerSecondSquared
        * (Settings.InitialHeightMeters - Settings.LegRestHeightMeters));
    AddInfo(FString::Printf(TEXT("M6_VERTICAL_LANDING drop_height_m=%.9g landed=%s settled=%s failed=%s contacts=%d impact_speed_m_s=%.9g expected_impact_speed_m_s=%.9g peak_force_n=%.9g max_compression_m=%.9g settle_time_s=%.9g damping_j=%.9g impulse_ns=%.9g samples=%d"),
        Settings.InitialHeightMeters - Settings.LegRestHeightMeters, Result.bLanded ? TEXT("true") : TEXT("false"),
        Result.bSettled ? TEXT("true") : TEXT("false"), Result.bFailed ? TEXT("true") : TEXT("false"), Result.ContactCount,
        Result.ImpactSpeedMetersPerSecond, ExpectedImpactSpeed, Result.PeakGroundForceNewtons, Result.MaximumCompressionMeters,
        Result.SettleTimeSeconds, Result.DampingDissipationJoules, Result.GroundImpulseNewtonSeconds, Result.Samples.Num()));
    TestTrue(TEXT("Drop enters the compliant contact phase"), Result.bValid && Result.bLanded && Result.ContactCount >= 1);
    TestTrue(TEXT("Impact speed matches the gravity-only airborne solution"), FMath::Abs(Result.ImpactSpeedMetersPerSecond - ExpectedImpactSpeed) < 0.03);
    TestTrue(TEXT("Compliant leg settles without bottoming out"), Result.bSettled && !Result.bFailed);
    TestTrue(TEXT("Peak force respects the explicit actuator/contact budget"), Result.PeakGroundForceNewtons <= Settings.MaximumGroundForceNewtons + 1.0e-9);
    TestTrue(TEXT("Compression remains inside the explicit travel budget"), Result.MaximumCompressionMeters < Settings.MaximumCompressionMeters);
    TestTrue(TEXT("Landing dissipates nonzero energy"), Result.DampingDissipationJoules > 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVerticalLandingTravelFailureTest, "PhysicsResearch.DrivenCharacter.VerticalLanding.TravelFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVerticalLandingTravelFailureTest::RunTest(const FString& Parameters)
{
    FVerticalLandingSettings Settings;
    Settings.InitialHeightMeters = 1.8;
    Settings.MaximumCompressionMeters = 0.12;
    Settings.MaximumGroundForceNewtons = 2500.0;
    const FVerticalLandingResult Result = FVerticalLandingExperiment::Simulate(Settings);
    AddInfo(FString::Printf(TEXT("M6_VERTICAL_FAILURE drop_height_m=%.9g travel_budget_m=%.9g force_budget_n=%.9g landed=%s settled=%s failed=%s peak_force_n=%.9g max_compression_m=%.9g expected_failure=true"),
        Settings.InitialHeightMeters - Settings.LegRestHeightMeters, Settings.MaximumCompressionMeters, Settings.MaximumGroundForceNewtons,
        Result.bLanded ? TEXT("true") : TEXT("false"), Result.bSettled ? TEXT("true") : TEXT("false"), Result.bFailed ? TEXT("true") : TEXT("false"),
        Result.PeakGroundForceNewtons, Result.MaximumCompressionMeters));
    TestTrue(TEXT("Over-budget drop still records physical contact"), Result.bValid && Result.bLanded);
    TestTrue(TEXT("Insufficient travel fails instead of teleporting to rest height"), Result.bFailed && !Result.bSettled);
    TestTrue(TEXT("Failure remains constrained by the same force budget"), Result.PeakGroundForceNewtons <= Settings.MaximumGroundForceNewtons + 1.0e-9);
    TestTrue(TEXT("Failure crosses the declared compression travel"), Result.MaximumCompressionMeters > Settings.MaximumCompressionMeters);
    return true;
}

#endif
