#include "FlowMapRealtimeGameMode.h"

#include "FlowMapRealtimeActor.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/SpectatorPawn.h"
#include "PhysicsExperimentComponent.h"

void AFlowMapRealtimeHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}

	const AFlowMapRealtimeActor* Actor = FindRealtimeActor();
	const UPhysicsExperimentComponent* Experiment = Actor ? Actor->GetExperimentComponent() : nullptr;
	const FPhysicsExperimentStats ExperimentStats = Experiment ? Experiment->GetExperimentStats() : FPhysicsExperimentStats();
	const FFlowMapGpuClosedLoopStats Stats = Actor ? Actor->GetGpuStats() : FFlowMapGpuClosedLoopStats();
	UFont* LargeFont = GEngine ? GEngine->GetLargeFont() : nullptr;
	UFont* MediumFont = GEngine ? GEngine->GetMediumFont() : nullptr;

	DrawRect(FLinearColor(0.006f, 0.018f, 0.032f, 0.94f), 28.0f, 26.0f, 790.0f, 480.0f);
	DrawRect(Actor&&Actor->IsVelocityBudgetAblation()?FLinearColor(1.0f,0.16f,0.04f,1.0f):FLinearColor(0.0f, 0.72f, 0.94f, 1.0f), 28.0f, 26.0f, 7.0f, 480.0f);
	DrawText(TEXT("CASE 03 · Flow Map 流体 · Chaos 运动边界"), FLinearColor::White, 50.0f, 44.0f, LargeFont, 1.0f, false);
	DrawText(Actor ? Actor->GetStatusText() : TEXT("未找到 Flow Map 实时 Actor"), Actor&&Actor->IsVelocityBudgetAblation()?FLinearColor(1.0f,0.28f,0.12f):(Actor && Actor->IsInitialized() ? FLinearColor(0.25f, 1.0f, 0.58f) : FLinearColor(1.0f, 0.35f, 0.2f)), 50.0f, 84.0f, MediumFont, 0.94f, false);
	DrawText(FString::Printf(TEXT("固定步：%lld    GPU 执行：%lld    最新快照：%lld"), ExperimentStats.StepIndex, Stats.StepsExecuted, Actor ? Actor->GetLatestSnapshotStep() : 0), FLinearColor::White, 50.0f, 122.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("Chaos Pose：%lld    边界提交 / 应用：%lld / %lld    版本：%lld"), Actor ? Actor->GetChaosPoseSamples() : 0, Actor ? Actor->GetBoundaryUpdatesSubmitted() : 0, Stats.SolidBoundaryApplicationsExecuted, Stats.LatestSolidBoundaryVersion), FLinearColor(0.62f, 0.86f, 1.0f), 50.0f, 158.0f, MediumFont, 0.95f, false);
	DrawText(FString::Printf(TEXT("MAC 固体：%d Cell    %d U 面    %d V 面    圆形碰撞体：%d"), Stats.LatestSolidCellCount, Stats.LatestSolidFaceUCount, Stats.LatestSolidFaceVCount, Stats.LatestSolidCircleCount), FLinearColor(1.0f, 0.72f, 0.25f), 50.0f, 194.0f, MediumFont, 0.95f, false);
	DrawText(FString::Printf(TEXT("粒子：%d    固体投影 Pass：%lld    压力 Jacobi Pass：%lld"), Actor ? Actor->GetParticleInstanceCount() : 0, Stats.ParticleSolidProjectionPasses, Stats.PressureJacobiPasses), FLinearColor(0.42f, 1.0f, 0.76f), 50.0f, 230.0f, MediumFont, 0.95f, false);
	DrawText(FString::Printf(TEXT("GPU 计时：最新 %.3f ms    稳态平均 / P95：%.3f / %.3f ms"), Stats.LatestGpuStepMilliseconds, Stats.MeanSteadyStateGpuStepMilliseconds, Stats.P95SteadyStateGpuStepMilliseconds), FLinearColor(0.78f, 0.68f, 1.0f), 50.0f, 266.0f, MediumFont, 0.95f, false);
	DrawText(FString::Printf(TEXT("异步读回：完成 %lld    等待中：%s    拒绝坏快照：%lld    跳过计时槽：%lld"), Stats.ReadbacksCompleted, Stats.bReadbackPending ? TEXT("是") : TEXT("否"), Actor ? Actor->GetRejectedSnapshots() : 0, Stats.GpuTimingSamplesSkipped), FLinearColor(0.72f, 0.82f, 0.94f), 50.0f, 302.0f, MediumFont, 0.88f, false);
	DrawText(FString::Printf(TEXT("后端：%s    档位：%s    ChaosRigidBoundary 始终启用"),Actor?*Actor->GetRuntimeBackendDisplayName():TEXT("未知"),Actor?*Actor->GetBudgetProfileDisplayName():TEXT("未知")), FLinearColor(0.3f, 0.95f, 1.0f), 50.0f, 340.0f, MediumFont, 0.88f, false);
	DrawText(FString::Printf(TEXT("青粒子:%d  紫色 MAC 箭头:%d  最大速度:%.2f cm/s  CFL 限制面:%d"),Actor?Actor->GetParticleInstanceCount():0,Actor?Actor->GetVelocityArrowCount():0,Actor?Actor->GetLatestMaximumVelocityCmPerSecond():0.0f,Actor?Actor->GetLatestVelocityLimitedFaceCount():0), FLinearColor(0.58f, 0.82f, 1.0f), 50.0f, 376.0f, MediumFont, 0.82f, false);
	DrawText(FString::Printf(TEXT("压力势 蓝负/红正：%.3f / %.3f    显著 Cell：%d / %d"),Actor?Actor->GetLatestMinimumPressurePotential():0.0f,Actor?Actor->GetLatestMaximumPressurePotential():0.0f,Actor?Actor->GetNegativePressureCellCount():0,Actor?Actor->GetPositivePressureCellCount():0),FLinearColor(1.0f,0.45f,0.62f),50.0f,410.0f,MediumFont,0.84f,false);
	DrawText(TEXT("空格 暂停  N 单步  R 重置  B 真后端切换  C 预算档位  D 调试  E 导出"), FLinearColor(0.38f, 1.0f, 0.68f), 50.0f, 442.0f, MediumFont, 0.82f, false);
	DrawText(FString::Printf(TEXT("交互计数 后端:%d 档位:%d 暂停:%d 单步:%d 重置:%d 调试:%d 导出:%d"),Actor?Actor->GetBackendToggleCount():0,Actor?Actor->GetBudgetToggleCount():0,Actor?Actor->GetPauseToggleCount():0,Actor?Actor->GetSingleStepCount():0,Actor?Actor->GetResetCount():0,Actor?Actor->GetDebugToggleCount():0,Actor?Actor->GetManualExportCount():0),FLinearColor(0.66f,0.78f,0.92f),50.0f,474.0f,MediumFont,0.78f,false);
}

AFlowMapRealtimeActor* AFlowMapRealtimeHUD::FindRealtimeActor() const
{
	if (!GetWorld())
	{
		return nullptr;
	}
	for (TActorIterator<AFlowMapRealtimeActor> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

AFlowMapRealtimeGameMode::AFlowMapRealtimeGameMode()
{
	HUDClass = AFlowMapRealtimeHUD::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	bStartPlayersAsSpectators = true;
}
