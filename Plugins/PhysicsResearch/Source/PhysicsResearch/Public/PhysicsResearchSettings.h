#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PhysicsResearchTypes.h"
#include "PhysicsResearchSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "物理研究"))
class PHYSICSRESEARCH_API UPhysicsResearchSettings final : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	UPROPERTY(Config, EditAnywhere, Category = "默认实验")
	EPhysicsResearchCase DefaultCase = EPhysicsResearchCase::ContactCloth;

	UPROPERTY(Config, EditAnywhere, Category = "默认实验")
	EPhysicsSolverBackend DefaultBackend = EPhysicsSolverBackend::ChaosBaseline;

	UPROPERTY(Config, EditAnywhere, Category = "默认实验")
	FPhysicsStepConfig DefaultStepConfig;
};
