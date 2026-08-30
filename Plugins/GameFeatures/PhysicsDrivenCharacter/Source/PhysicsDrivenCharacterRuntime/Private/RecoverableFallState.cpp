#include "RecoverableFallState.h"

ERecoverableFallState FRecoverableFallAuthorityPolicy::Evaluate(const ERecoverableFallState Current, const FRecoverableFallAuthorityInput& Input, EPhysicalGetUpBranch& OutBranch)
{
    OutBranch = EPhysicalGetUpBranch::Pending;
    if (!Input.bHasAuthority) return Current;
    if (Input.bDeathActive) return ERecoverableFallState::Failed;
    if (Current == ERecoverableFallState::Falling && Input.bGrounded) return ERecoverableFallState::Settling;
    if (Current != ERecoverableFallState::Settling || !Input.bRecoveryRequested) return Current;

    FPhysicalGetUpDecisionInput DecisionInput;
    DecisionInput.bGrounded = Input.bGrounded;
    DecisionInput.FacingUpDot = Input.FacingUpDot;
    DecisionInput.LinearSpeedCentimetersPerSecond = Input.LinearSpeed;
    DecisionInput.AngularSpeedRadiansPerSecond = Input.AngularSpeed;
    DecisionInput.StableSeconds = Input.StableSeconds;
    OutBranch = FPhysicalGetUpDecision::Evaluate(DecisionInput);
    return (OutBranch == EPhysicalGetUpBranch::Front || OutBranch == EPhysicalGetUpBranch::Back) ? ERecoverableFallState::Recovering : Current;
}

const TCHAR* FRecoverableFallAuthorityPolicy::ToString(const ERecoverableFallState State)
{
    switch (State)
    {
    case ERecoverableFallState::Balanced: return TEXT("Balanced");
    case ERecoverableFallState::Falling: return TEXT("Falling");
    case ERecoverableFallState::Settling: return TEXT("Settling");
    case ERecoverableFallState::Recovering: return TEXT("Recovering");
    case ERecoverableFallState::Recovered: return TEXT("Recovered");
    case ERecoverableFallState::Failed: return TEXT("Failed");
    default: return TEXT("Unknown");
    }
}
