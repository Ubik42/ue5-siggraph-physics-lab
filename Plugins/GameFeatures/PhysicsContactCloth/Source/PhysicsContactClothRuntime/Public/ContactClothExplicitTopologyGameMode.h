#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "ContactClothExplicitTopologyGameMode.generated.h"

class AContactClothExplicitTopologyActor;

UCLASS()
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothExplicitTopologyHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	AContactClothExplicitTopologyActor* FindExplicitActor() const;
};

UCLASS()
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothExplicitTopologyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AContactClothExplicitTopologyGameMode();
};
