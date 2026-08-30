#include "MeasuredPhysicsControlComponent.h"

#include "HAL/PlatformTime.h"

void UMeasuredPhysicsControlComponent::TickComponent(
    const float DeltaTime,
    const ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    const uint64 StartCycles = FPlatformTime::Cycles64();
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    LastUpdateMilliseconds = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles);
}
