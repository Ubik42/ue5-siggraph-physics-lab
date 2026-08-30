#include "SoftBodyDragonRealtimeGameMode.h"

#include "SoftBodyDragonRealtimeActor.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/SpectatorPawn.h"
#include "PhysicsExperimentComponent.h"

void ASoftBodyDragonRealtimeHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	const ASoftBodyDragonRealtimeActor* Actor = FindRealtimeActor();
	const UPhysicsExperimentComponent* Experiment = Actor ? Actor->GetExperimentComponent() : nullptr;
	const FPhysicsExperimentStats ExperimentStats = Experiment ? Experiment->GetExperimentStats() : FPhysicsExperimentStats();
	const FSoftBodyGpuXpbdStats Stats = Actor ? Actor->GetGpuStats() : FSoftBodyGpuXpbdStats();

	DrawRect(FLinearColor(0.008f, 0.014f, 0.028f, 0.93f), 28.0f, 26.0f, 760.0f, 430.0f);
	DrawRect(Actor && Actor->IsWeakGroundContactProfile() ? FLinearColor(1.0f, 0.28f, 0.12f, 1.0f) : FLinearColor(0.46f, 0.25f, 1.0f, 1.0f), 28.0f, 26.0f, 7.0f, 430.0f);
	UFont* LargeFont = GEngine ? GEngine->GetLargeFont() : nullptr;
	UFont* MediumFont = GEngine ? GEngine->GetMediumFont() : nullptr;
	DrawText(FString::Printf(TEXT("CASE 02 · 多尺度软体 Dragon · %s"), Actor && Actor->IsCpuReferenceBackend() ? TEXT("CPU 局部参考") : TEXT("GPU D3D12")), FLinearColor::White, 50.0f, 45.0f, LargeFont, 1.0f, false);
	DrawText(Actor ? Actor->GetStatusText() : TEXT("未找到 Dragon 实时 Actor"), Actor && Actor->DidRunPass() ? FLinearColor(0.3f, 1.0f, 0.55f) : FLinearColor(0.55f, 0.78f, 1.0f), 50.0f, 86.0f, MediumFont, 0.95f, false);
	DrawText(FString::Printf(TEXT("固定步：%lld / 300    求解已执行：%lld    最新表面步：%lld"), ExperimentStats.StepIndex, Actor ? Actor->GetDisplayedSolverSteps() : 0, Actor ? Actor->GetLatestReadbackStep() : 0), FLinearColor::White, 50.0f, 124.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("Dragon 表面：%d 点 / %d 面    体积 Cage：%d 点 / %d 四面体"), Actor ? Actor->GetSurfaceVertexCount() : 0, Actor ? Actor->GetSurfaceTriangleCount() : 0, Actor ? Actor->GetCageVertexCount() : 0, Actor ? Actor->GetCageTetrahedronCount() : 0), FLinearColor::White, 50.0f, 158.0f, MediumFont, 1.0f, false);
	DrawText(Actor && Actor->IsCpuReferenceBackend()
		? FString::Printf(TEXT("CPU 同步表面更新：%lld    平均 / 最大固定步：%.3f / %.3f ms"), Actor->GetSurfaceUpdateCount(), Actor->GetMeanCpuStepMilliseconds(), Actor->GetMaximumCpuStepMilliseconds())
		: FString::Printf(TEXT("GPU 异步表面更新：%lld    快照完成：%lld    等待中：%s"), Actor ? Actor->GetSurfaceUpdateCount() : 0, Stats.ReadbacksCompleted, Stats.bReadbackPending ? TEXT("是") : TEXT("否")), FLinearColor(0.72f, 0.82f, 0.96f), 50.0f, 192.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("可见最大形变：%.3f cm    静止绑定误差：%.6f cm"), Actor ? Actor->GetMaximumSurfaceDisplacementCm() : 0.0, Actor ? Actor->GetMaximumRestReconstructionErrorCm() : 0.0), FLinearColor(0.95f, 0.72f, 0.22f), 50.0f, 226.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("固定端：%d 点    最大漂移：%.8f cm"), Actor ? Actor->GetFixedCageVertexCount() : 0, Actor ? Actor->GetMaximumFixedFaceDriftCm() : 0.0), FLinearColor(1.0f, 0.42f, 0.3f), 50.0f, 260.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("低频模式：%d    投影：%lld"), Actor && Actor->IsCpuReferenceBackend() ? Actor->GetCpuStats().LowFrequencyModeCount : Stats.LowFrequencyModeCount, Actor ? Actor->GetDisplayedLowFrequencyPasses() : 0), FLinearColor(0.62f, 0.5f, 1.0f), 50.0f, 294.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("地面接触：%s  刚度 %.2f  Pass %lld  最大穿透 %.4f cm  接触点 %d"), Actor && Actor->IsWeakGroundContactProfile() ? TEXT("弱接触消融") : TEXT("标准"), Actor ? Actor->GetGroundContactStiffness() : 0.0f, Actor ? Actor->GetDisplayedGroundContactPasses() : 0, Actor ? Actor->GetMaximumGroundPenetrationCm() : 0.0, Actor ? Actor->GetActiveGroundContactVertices() : 0), Actor && Actor->IsWeakGroundContactProfile() ? FLinearColor(1.0f, 0.34f, 0.18f) : FLinearColor(0.25f, 1.0f, 0.55f), 50.0f, 330.0f, MediumFont, 0.9f, false);
	DrawText(TEXT("紫色表面：Dragon 动态网格    青线：GPU Cage    红点：固定端"), FLinearColor(0.55f, 0.68f, 0.82f), 50.0f, 364.0f, MediumFont, 0.9f, false);
	DrawText(TEXT("空格 暂停/继续  N 单步  R 重置  B GPU/CPU  F 标准/弱接触  D 调试  E 导出"), FLinearColor(0.42f, 1.0f, 0.72f), 50.0f, 398.0f, MediumFont, 0.84f, false);
	DrawText(FString::Printf(TEXT("交互计数 暂停:%d  单步:%d  重置:%d  调试:%d  导出:%d  接触:%d  后端:%d"), Actor ? Actor->GetPauseToggleCount() : 0, Actor ? Actor->GetSingleStepCount() : 0, Actor ? Actor->GetResetCount() : 0, Actor ? Actor->GetDebugToggleCount() : 0, Actor ? Actor->GetManualExportCount() : 0, Actor ? Actor->GetContactProfileToggleCount() : 0, Actor ? Actor->GetBackendToggleCount() : 0), FLinearColor(0.65f, 0.78f, 0.92f), 50.0f, 432.0f, MediumFont, 0.82f, false);
}

ASoftBodyDragonRealtimeActor* ASoftBodyDragonRealtimeHUD::FindRealtimeActor() const
{
	if (!GetWorld())
	{
		return nullptr;
	}
	for (TActorIterator<ASoftBodyDragonRealtimeActor> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

ASoftBodyDragonRealtimeGameMode::ASoftBodyDragonRealtimeGameMode()
{
	HUDClass = ASoftBodyDragonRealtimeHUD::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	bStartPlayersAsSpectators = true;
}
