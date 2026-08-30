#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "ContactClothBaselineGameMode.generated.h"

class AContactClothBaselineActor;

UCLASS()
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothBaselineHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	AContactClothBaselineActor* FindBaselineActor() const;
};

UCLASS()
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothBaselineGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AContactClothBaselineGameMode();
};
