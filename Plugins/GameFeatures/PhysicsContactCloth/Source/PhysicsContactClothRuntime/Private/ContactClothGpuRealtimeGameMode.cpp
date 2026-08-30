#include "ContactClothGpuRealtimeGameMode.h"

#include "ContactClothGpuRealtimeActor.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/SpectatorPawn.h"
#include "PhysicsExperimentComponent.h"

void AContactClothGpuRealtimeHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	const AContactClothGpuRealtimeActor* Actor = FindRealtimeActor();
	const UPhysicsExperimentComponent* Experiment = Actor ? Actor->GetExperimentComponent() : nullptr;
	const FPhysicsExperimentStats ExperimentStats = Experiment ? Experiment->GetExperimentStats() : FPhysicsExperimentStats();
	const FContactClothGpuRealtimeStats GpuStats = Actor ? Actor->GetGpuStats() : FContactClothGpuRealtimeStats();

	DrawRect(FLinearColor(0.008f, 0.018f, 0.032f, 0.92f), 28.0f, 26.0f, 660.0f, 370.0f);
	DrawRect(FLinearColor(0.0f, 0.72f, 0.95f, 1.0f), 28.0f, 26.0f, 7.0f, 370.0f);
	UFont* LargeFont = GEngine ? GEngine->GetLargeFont() : nullptr;
	UFont* MediumFont = GEngine ? GEngine->GetMediumFont() : nullptr;
	DrawText(TEXT("CASE 01 · 持久 GPU 布料逐帧运行"), FLinearColor::White, 50.0f, 45.0f, LargeFont, 1.0f, false);
	DrawText(Actor ? Actor->GetStatusText() : TEXT("未找到逐帧 GPU Actor"), Actor && Actor->DidRunPass() ? FLinearColor(0.25f, 1.0f, 0.55f) : FLinearColor(0.3f, 0.9f, 1.0f), 50.0f, 88.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("共享固定步：%lld / 300    GPU 已执行：%lld"), ExperimentStats.StepIndex, GpuStats.StepsExecuted), FLinearColor::White, 50.0f, 124.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("粒子：%d    模态：%d    每步 Jacobi：%d    最终快照：%lld"), Actor ? Actor->GetParticleCount() : 0, Actor ? Actor->GetModalModeCount() : 0, Actor ? Actor->GetIterationCount() : 0, Actor ? Actor->GetLatestReadbackStep() : 0), FLinearColor::White, 50.0f, 158.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("模态 / Rank-one / NDB / CCD / 残差：%lld / %lld / %lld / %lld / %lld"), GpuStats.ModalCorrectionsExecuted, GpuStats.RankOneCollisionSystemsExecuted, GpuStats.NdbStateUpdatesExecuted, GpuStats.CompleteCcdPassesExecuted, GpuStats.ResidualReusePassesExecuted), FLinearColor(0.3f, 0.9f, 1.0f), 50.0f, 192.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("异步快照：%lld    等待中：%s"), GpuStats.ReadbacksCompleted, GpuStats.bReadbackPending ? TEXT("是") : TEXT("否")), FLinearColor(0.82f, 0.88f, 0.96f), 50.0f, 226.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("结构误差：%.4f cm    穿透：%.7f cm    残差：%.4f cm / 回退事件 %lld"), Actor ? Actor->GetMaximumEdgeErrorCm() : 0.0, Actor ? Actor->GetMaximumCapsulePenetrationCm() : 0.0, GpuStats.MaximumForwardedResidualCm, GpuStats.ResidualFallbackEvents), FLinearColor(0.82f, 0.88f, 0.96f), 50.0f, 260.0f, MediumFont, 0.85f, false);
	DrawText(FString::Printf(TEXT("动态碰撞：%d 球 / %d 胶囊    姿态更新：%lld"), Actor ? Actor->GetCollisionSphereCount() : 0, Actor ? Actor->GetCollisionCapsuleCount() : 0, Actor ? Actor->GetPoseUpdateCount() : 0), FLinearColor(0.95f, 0.72f, 0.2f), 50.0f, 294.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("活跃接触对：%d / 峰值 %d    NDB 连续激活：%d / 峰值 %d    提交：%.3f ms"), GpuStats.LatestActiveContactPairs, GpuStats.MaximumActiveContactPairs, GpuStats.LatestMaximumNdbActivationCount, GpuStats.MaximumNdbActivationCount, GpuStats.LatestRenderSubmissionMilliseconds), FLinearColor(0.95f, 0.72f, 0.2f), 50.0f, 326.0f, MediumFont, 0.85f, false);
	DrawText(TEXT("青：GPU 布片    红：固定顶边    橙：Assassin 动态 Physics Asset"), FLinearColor(0.55f, 0.68f, 0.82f), 50.0f, 360.0f, MediumFont, 0.85f, false);
}

AContactClothGpuRealtimeActor* AContactClothGpuRealtimeHUD::FindRealtimeActor() const
{
	if (!GetWorld())
	{
		return nullptr;
	}
	for (TActorIterator<AContactClothGpuRealtimeActor> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

AContactClothGpuRealtimeGameMode::AContactClothGpuRealtimeGameMode()
{
	HUDClass = AContactClothGpuRealtimeHUD::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	bStartPlayersAsSpectators = true;
}
