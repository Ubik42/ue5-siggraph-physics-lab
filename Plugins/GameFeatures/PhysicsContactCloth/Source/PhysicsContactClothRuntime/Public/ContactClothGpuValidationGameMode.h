#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "ContactClothGpuValidationGameMode.generated.h"

class AContactClothGpuValidationActor;

UCLASS()
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothGpuValidationHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	AContactClothGpuValidationActor* FindValidationActor() const;
};

UCLASS()
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothGpuValidationGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AContactClothGpuValidationGameMode();
};
