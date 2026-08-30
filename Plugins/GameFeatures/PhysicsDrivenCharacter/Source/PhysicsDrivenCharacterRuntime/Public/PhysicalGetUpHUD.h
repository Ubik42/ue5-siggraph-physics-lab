#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "PhysicalGetUpHUD.generated.h"

UCLASS()
class PHYSICSDRIVENCHARACTERRUNTIME_API APhysicalGetUpHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
};
