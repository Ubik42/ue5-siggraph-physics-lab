#pragma once

#include "CoreMinimal.h"
#include "PhysicalGetUpDecision.h"
#include "RecoverableFallState.generated.h"

UENUM(BlueprintType)
enum class ERecoverableFallState : uint8
{
    Balanced,
    Falling,
    Settling,
    Recovering,
    Recovered,
    Failed
};

struct FRecoverableFallAuthorityInput
{
    bool bHasAuthority = false;
    bool bDeathActive = false;
    bool bGrounded = false;
    bool bRecoveryRequested = false;
    double FacingUpDot = 0.0;
    double LinearSpeed = 0.0;
    double AngularSpeed = 0.0;
    double StableSeconds = 0.0;
};

struct PHYSICSDRIVENCHARACTERRUNTIME_API FRecoverableFallAuthorityPolicy
{
    static ERecoverableFallState Evaluate(ERecoverableFallState Current, const FRecoverableFallAuthorityInput& Input, EPhysicalGetUpBranch& OutBranch);
    static const TCHAR* ToString(ERecoverableFallState State);
};
