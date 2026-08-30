#pragma once

#include "CoreMinimal.h"
#include "PhysicalGetUpDecision.generated.h"

UENUM(BlueprintType)
enum class EPhysicalGetUpBranch : uint8
{
    Pending,
    Front,
    Back,
    RejectAirborne,
    RejectMoving,
    RejectSideways
};

struct FPhysicalGetUpDecisionInput
{
    double FacingUpDot = 0.0;
    double LinearSpeedCentimetersPerSecond = 0.0;
    double AngularSpeedRadiansPerSecond = 0.0;
    double StableSeconds = 0.0;
    bool bGrounded = false;
};

struct FPhysicalGetUpDecision
{
    static constexpr double MaximumLinearSpeed = 18.0;
    static constexpr double MaximumAngularSpeed = 0.9;
    static constexpr double RequiredStableSeconds = 0.35;
    static constexpr double MinimumFacingDot = 0.10;

    static EPhysicalGetUpBranch Evaluate(const FPhysicalGetUpDecisionInput& Input);
    static const TCHAR* ToString(EPhysicalGetUpBranch Branch);
};
