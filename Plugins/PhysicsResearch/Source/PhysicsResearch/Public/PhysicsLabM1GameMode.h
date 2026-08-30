#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "PhysicsLabM1GameMode.generated.h"

class APhysicsLabM1Actor;

UCLASS(DisplayName = "物理实验室 HUD")
class PHYSICSRESEARCH_API APhysicsLabHUD final : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	APhysicsLabM1Actor* FindLabActor() const;
};

UCLASS(DisplayName = "M1 共享实验 GameMode")
class PHYSICSRESEARCH_API APhysicsLabM1GameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	APhysicsLabM1GameMode();
};
