#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "SoftBodyDragonRealtimeGameMode.generated.h"

class ASoftBodyDragonRealtimeActor;

UCLASS()
class PHYSICSSOFTBODYRUNTIME_API ASoftBodyDragonRealtimeHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	ASoftBodyDragonRealtimeActor* FindRealtimeActor() const;
};

UCLASS()
class PHYSICSSOFTBODYRUNTIME_API ASoftBodyDragonRealtimeGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASoftBodyDragonRealtimeGameMode();
};
