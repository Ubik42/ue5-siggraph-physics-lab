#include "Misc/AutomationTest.h"
#include "RecoverableFallState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecoverableFallAuthorityOnlyTest, "PhysicsResearch.DrivenCharacter.RecoverableFall.AuthorityOnly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRecoverableFallAuthorityOnlyTest::RunTest(const FString& Parameters)
{
    FRecoverableFallAuthorityInput Input;
    Input.bHasAuthority = false;
    Input.bGrounded = true;
    Input.bRecoveryRequested = true;
    Input.FacingUpDot = 0.8;
    Input.StableSeconds = 1.0;
    EPhysicalGetUpBranch Branch;
    TestEqual(TEXT("client cannot advance replicated state"), FRecoverableFallAuthorityPolicy::Evaluate(ERecoverableFallState::Settling, Input, Branch), ERecoverableFallState::Settling);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecoverableFallDeathPriorityTest, "PhysicsResearch.DrivenCharacter.RecoverableFall.DeathPriority", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRecoverableFallDeathPriorityTest::RunTest(const FString& Parameters)
{
    FRecoverableFallAuthorityInput Input;
    Input.bHasAuthority = true;
    Input.bDeathActive = true;
    EPhysicalGetUpBranch Branch;
    TestEqual(TEXT("Lyra death wins during recovery"), FRecoverableFallAuthorityPolicy::Evaluate(ERecoverableFallState::Recovering, Input, Branch), ERecoverableFallState::Failed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecoverableFallServerSequenceTest, "PhysicsResearch.DrivenCharacter.RecoverableFall.ServerSequence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRecoverableFallServerSequenceTest::RunTest(const FString& Parameters)
{
    FRecoverableFallAuthorityInput Input;
    Input.bHasAuthority = true;
    Input.bGrounded = true;
    EPhysicalGetUpBranch Branch;
    TestEqual(TEXT("ground contact advances falling"), FRecoverableFallAuthorityPolicy::Evaluate(ERecoverableFallState::Falling, Input, Branch), ERecoverableFallState::Settling);
    Input.bRecoveryRequested = true;
    Input.FacingUpDot = -0.8;
    Input.StableSeconds = 0.5;
    TestEqual(TEXT("server grants stable recovery"), FRecoverableFallAuthorityPolicy::Evaluate(ERecoverableFallState::Settling, Input, Branch), ERecoverableFallState::Recovering);
    TestEqual(TEXT("front branch replicated with state"), Branch, EPhysicalGetUpBranch::Front);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRecoverableFallRejectMovingTest, "PhysicsResearch.DrivenCharacter.RecoverableFall.RejectMoving", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRecoverableFallRejectMovingTest::RunTest(const FString& Parameters)
{
    FRecoverableFallAuthorityInput Input;
    Input.bHasAuthority = true;
    Input.bGrounded = true;
    Input.bRecoveryRequested = true;
    Input.FacingUpDot = 0.8;
    Input.LinearSpeed = 40.0;
    Input.StableSeconds = 0.5;
    EPhysicalGetUpBranch Branch;
    TestEqual(TEXT("moving body stays settling"), FRecoverableFallAuthorityPolicy::Evaluate(ERecoverableFallState::Settling, Input, Branch), ERecoverableFallState::Settling);
    TestEqual(TEXT("reason is explicit"), Branch, EPhysicalGetUpBranch::RejectMoving);
    return true;
}
