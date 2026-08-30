#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "PhysicsControlCharacterHUD.generated.h"

UCLASS()
class PHYSICSDRIVENCHARACTERRUNTIME_API APhysicsControlCharacterHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;
};
