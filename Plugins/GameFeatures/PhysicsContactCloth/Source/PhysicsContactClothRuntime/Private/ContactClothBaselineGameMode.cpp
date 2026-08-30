#include "ContactClothBaselineGameMode.h"

#include "ContactClothBaselineActor.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/SpectatorPawn.h"
#include "PhysicsExperimentComponent.h"

void AContactClothBaselineHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}

	const AContactClothBaselineActor* Actor = FindBaselineActor();
	const UPhysicsExperimentComponent* Experiment = Actor ? Actor->GetExperimentComponent() : nullptr;
	const FPhysicsExperimentStats Stats = Experiment ? Experiment->GetExperimentStats() : FPhysicsExperimentStats();

	DrawRect(FLinearColor(0.008f, 0.018f, 0.032f, 0.9f), 28.0f, 26.0f, 540.0f, 270.0f);
	DrawRect(FLinearColor(0.8f, 0.18f, 0.08f, 1.0f), 28.0f, 26.0f, 7.0f, 270.0f);
	UFont* LargeFont = GEngine ? GEngine->GetLargeFont() : nullptr;
	UFont* MediumFont = GEngine ? GEngine->GetMediumFont() : nullptr;
	DrawText(TEXT("CASE 01 · Chaos Cloth 资产运行门"), FLinearColor::White, 50.0f, 45.0f, LargeFont, 1.0f, false);
	DrawText(Actor ? Actor->GetGateStatusText() : TEXT("未找到验证 Actor"), FLinearColor(0.25f, 0.9f, 1.0f), 50.0f, 90.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("后端：Chaos Cloth    固定步：1/60 秒    步数：%lld"), Stats.StepIndex), FLinearColor::White, 50.0f, 126.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("Clothing Asset：%d    回写粒子：%d"), Actor ? Actor->GetClothingAssetCount() : 0, Actor ? Actor->GetSimulatedParticleCount() : 0), FLinearColor(0.78f, 0.84f, 0.92f), 50.0f, 160.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("最大观测位移：%.3f cm    指标样本：%d"), Actor ? Actor->GetMaximumObservedDisplacementCm() : 0.0, Stats.RecordedMetricSamples), FLinearColor(0.78f, 0.84f, 0.92f), 50.0f, 194.0f, MediumFont, 1.0f, false);
	DrawText(TEXT("判定链：资产加载 → Clothing 绑定 → Chaos 实例 → 粒子回写 → 动态位移"), FLinearColor(0.95f, 0.7f, 0.2f), 50.0f, 238.0f, MediumFont, 0.9f, false);
}

AContactClothBaselineActor* AContactClothBaselineHUD::FindBaselineActor() const
{
	if (!GetWorld())
	{
		return nullptr;
	}
	for (TActorIterator<AContactClothBaselineActor> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

AContactClothBaselineGameMode::AContactClothBaselineGameMode()
{
	HUDClass = AContactClothBaselineHUD::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	bStartPlayersAsSpectators = true;
}
