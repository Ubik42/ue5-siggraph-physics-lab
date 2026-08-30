#include "ContactClothCpuReferenceGameMode.h"

#include "ContactClothCpuReferenceActor.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/SpectatorPawn.h"
#include "PhysicsExperimentComponent.h"

void AContactClothCpuReferenceHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	const AContactClothCpuReferenceActor* Actor = FindReferenceActor();
	const UPhysicsExperimentComponent* Experiment = Actor ? Actor->GetExperimentComponent() : nullptr;
	const FPhysicsExperimentStats Stats = Experiment ? Experiment->GetExperimentStats() : FPhysicsExperimentStats();
	const FContactClothCpuStepStats StepStats = Actor ? Actor->GetLastStepStats() : FContactClothCpuStepStats();

	DrawRect(FLinearColor(0.008f, 0.018f, 0.032f, 0.9f), 28.0f, 26.0f, 540.0f, 275.0f);
	DrawRect(FLinearColor(0.55f, 0.2f, 0.95f, 1.0f), 28.0f, 26.0f, 7.0f, 275.0f);
	UFont* LargeFont = GEngine ? GEngine->GetLargeFont() : nullptr;
	UFont* MediumFont = GEngine ? GEngine->GetMediumFont() : nullptr;
	DrawText(TEXT("CASE 01 · CPU Projective Dynamics 数学参考"), FLinearColor::White, 50.0f, 45.0f, LargeFont, 1.0f, false);
	DrawText(Actor ? Actor->GetStatusText() : TEXT("未找到参考 Actor"), FLinearColor(0.3f, 0.9f, 1.0f), 50.0f, 90.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("粒子：%d    约束：%d    固定步：%lld / 300"), Actor ? Actor->GetParticleCount() : 0, Actor ? Actor->GetConstraintCount() : 0, Stats.StepIndex), FLinearColor::White, 50.0f, 126.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("最大拉伸比误差：%.5f    平均：%.5f"), StepStats.MaximumStretchRatioError, StepStats.MeanStretchRatioError), FLinearColor(0.8f, 0.84f, 0.92f), 50.0f, 160.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("接触投影累计：%lld    剩余穿透：%.7f cm"), Actor ? Actor->GetTotalCollisionProjections() : 0, StepStats.MaximumRemainingPenetrationCm), FLinearColor(0.8f, 0.84f, 0.92f), 50.0f, 194.0f, MediumFont, 1.0f, false);
	DrawText(TEXT("青：拉伸  橙：剪切  紫：弯曲  红：固定点  白：粒子"), FLinearColor(0.95f, 0.72f, 0.2f), 50.0f, 242.0f, MediumFont, 0.9f, false);
}

AContactClothCpuReferenceActor* AContactClothCpuReferenceHUD::FindReferenceActor() const
{
	if (!GetWorld())
	{
		return nullptr;
	}
	for (TActorIterator<AContactClothCpuReferenceActor> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

AContactClothCpuReferenceGameMode::AContactClothCpuReferenceGameMode()
{
	HUDClass = AContactClothCpuReferenceHUD::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	bStartPlayersAsSpectators = true;
}
