#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "ContactClothCpuReferenceGameMode.generated.h"

class AContactClothCpuReferenceActor;

UCLASS()
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothCpuReferenceHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	AContactClothCpuReferenceActor* FindReferenceActor() const;
};

UCLASS()
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothCpuReferenceGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AContactClothCpuReferenceGameMode();
};
