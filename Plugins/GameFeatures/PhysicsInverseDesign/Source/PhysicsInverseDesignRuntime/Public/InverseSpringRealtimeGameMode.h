#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "InverseSpringRealtimeGameMode.generated.h"

class AInverseSpringRealtimeActor;

UCLASS()
class PHYSICSINVERSEDESIGNRUNTIME_API AInverseSpringRealtimeHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
private:
    AInverseSpringRealtimeActor* FindExperimentActor() const;
};

UCLASS()
class PHYSICSINVERSEDESIGNRUNTIME_API AInverseSpringRealtimeGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AInverseSpringRealtimeGameMode();
};
