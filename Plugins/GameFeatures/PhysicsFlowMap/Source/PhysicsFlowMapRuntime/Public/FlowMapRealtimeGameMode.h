#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "FlowMapRealtimeGameMode.generated.h"

class AFlowMapRealtimeActor;

UCLASS()
class PHYSICSFLOWMAPRUNTIME_API AFlowMapRealtimeHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	AFlowMapRealtimeActor* FindRealtimeActor() const;
};

UCLASS()
class PHYSICSFLOWMAPRUNTIME_API AFlowMapRealtimeGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFlowMapRealtimeGameMode();
};
