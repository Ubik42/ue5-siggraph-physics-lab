#include "PhysicalGetUpDecision.h"

EPhysicalGetUpBranch FPhysicalGetUpDecision::Evaluate(const FPhysicalGetUpDecisionInput& Input)
{
    if (!Input.bGrounded) return EPhysicalGetUpBranch::RejectAirborne;
    if (Input.LinearSpeedCentimetersPerSecond > MaximumLinearSpeed || Input.AngularSpeedRadiansPerSecond > MaximumAngularSpeed)
    {
        return EPhysicalGetUpBranch::RejectMoving;
    }
    if (FMath::Abs(Input.FacingUpDot) < MinimumFacingDot) return EPhysicalGetUpBranch::RejectSideways;
    if (Input.StableSeconds < RequiredStableSeconds) return EPhysicalGetUpBranch::Pending;
    return Input.FacingUpDot > 0.0 ? EPhysicalGetUpBranch::Back : EPhysicalGetUpBranch::Front;
}

const TCHAR* FPhysicalGetUpDecision::ToString(const EPhysicalGetUpBranch Branch)
{
    switch (Branch)
    {
    case EPhysicalGetUpBranch::Pending: return TEXT("Pending");
    case EPhysicalGetUpBranch::Front: return TEXT("Front");
    case EPhysicalGetUpBranch::Back: return TEXT("Back");
    case EPhysicalGetUpBranch::RejectAirborne: return TEXT("RejectAirborne");
    case EPhysicalGetUpBranch::RejectMoving: return TEXT("RejectMoving");
    case EPhysicalGetUpBranch::RejectSideways: return TEXT("RejectSideways");
    default: return TEXT("Unknown");
    }
}
