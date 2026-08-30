#include "Misc/AutomationTest.h"
#include "PhysicalGetUpDecision.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPhysicalGetUpFrontBackDecisionTest, "PhysicsResearch.DrivenCharacter.GetUpDecision.FrontBack", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPhysicalGetUpFrontBackDecisionTest::RunTest(const FString& Parameters)
{
    FPhysicalGetUpDecisionInput Input;
    Input.bGrounded = true;
    Input.StableSeconds = 0.5;
    Input.FacingUpDot = -0.8;
    TestEqual(TEXT("Face-down Chaos pose selects front get-up"), FPhysicalGetUpDecision::Evaluate(Input), EPhysicalGetUpBranch::Front);
    Input.FacingUpDot = 0.8;
    TestEqual(TEXT("Face-up Chaos pose selects back get-up"), FPhysicalGetUpDecision::Evaluate(Input), EPhysicalGetUpBranch::Back);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPhysicalGetUpVelocityRejectionTest, "PhysicsResearch.DrivenCharacter.GetUpDecision.VelocityRejection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPhysicalGetUpVelocityRejectionTest::RunTest(const FString& Parameters)
{
    FPhysicalGetUpDecisionInput Input;
    Input.bGrounded = true;
    Input.StableSeconds = 1.0;
    Input.FacingUpDot = 0.9;
    Input.LinearSpeedCentimetersPerSecond = FPhysicalGetUpDecision::MaximumLinearSpeed + 0.1;
    TestEqual(TEXT("Excess linear velocity rejects get-up"), FPhysicalGetUpDecision::Evaluate(Input), EPhysicalGetUpBranch::RejectMoving);
    Input.LinearSpeedCentimetersPerSecond = 0.0;
    Input.AngularSpeedRadiansPerSecond = FPhysicalGetUpDecision::MaximumAngularSpeed + 0.1;
    TestEqual(TEXT("Excess angular velocity rejects get-up"), FPhysicalGetUpDecision::Evaluate(Input), EPhysicalGetUpBranch::RejectMoving);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPhysicalGetUpGroundAndSideDecisionTest, "PhysicsResearch.DrivenCharacter.GetUpDecision.GroundAndSide", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPhysicalGetUpGroundAndSideDecisionTest::RunTest(const FString& Parameters)
{
    FPhysicalGetUpDecisionInput Input;
    Input.StableSeconds = 1.0;
    Input.FacingUpDot = 0.9;
    TestEqual(TEXT("Airborne pose rejects get-up"), FPhysicalGetUpDecision::Evaluate(Input), EPhysicalGetUpBranch::RejectAirborne);
    Input.bGrounded = true;
    Input.FacingUpDot = 0.05;
    TestEqual(TEXT("Side pose rejects ambiguous branch"), FPhysicalGetUpDecision::Evaluate(Input), EPhysicalGetUpBranch::RejectSideways);
    Input.FacingUpDot = 0.9;
    Input.StableSeconds = 0.2;
    TestEqual(TEXT("Insufficient stable time remains pending"), FPhysicalGetUpDecision::Evaluate(Input), EPhysicalGetUpBranch::Pending);
    return true;
}

#endif
