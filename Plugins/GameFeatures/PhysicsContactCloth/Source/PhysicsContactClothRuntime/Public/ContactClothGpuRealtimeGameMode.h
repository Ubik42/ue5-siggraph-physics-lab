#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "ContactClothGpuRealtimeGameMode.generated.h"

class AContactClothGpuRealtimeActor;

UCLASS()
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothGpuRealtimeHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	AContactClothGpuRealtimeActor* FindRealtimeActor() const;
};

UCLASS()
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothGpuRealtimeGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AContactClothGpuRealtimeGameMode();
};
