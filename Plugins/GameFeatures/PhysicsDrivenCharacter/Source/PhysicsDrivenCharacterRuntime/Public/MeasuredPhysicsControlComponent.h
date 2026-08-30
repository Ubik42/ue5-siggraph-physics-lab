#pragma once

#include "CoreMinimal.h"
#include "PhysicsControlComponent.h"
#include "MeasuredPhysicsControlComponent.generated.h"

UCLASS()
class PHYSICSDRIVENCHARACTERRUNTIME_API UMeasuredPhysicsControlComponent : public UPhysicsControlComponent
{
    GENERATED_BODY()

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    double GetLastUpdateMilliseconds() const { return LastUpdateMilliseconds; }

private:
    double LastUpdateMilliseconds = 0.0;
};
