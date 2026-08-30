#include "ContactClothExplicitTopologyGameMode.h"

#include "ContactClothExplicitTopologyActor.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/SpectatorPawn.h"
#include "PhysicsExperimentComponent.h"

void AContactClothExplicitTopologyHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	const AContactClothExplicitTopologyActor* Actor = FindExplicitActor();
	const UPhysicsExperimentComponent* Experiment = Actor ? Actor->GetExperimentComponent() : nullptr;
	const FPhysicsExperimentStats ExperimentStats = Experiment ? Experiment->GetExperimentStats() : FPhysicsExperimentStats();
	const FContactClothGpuExplicitTopologyRealtimeStats GpuStats = Actor ? Actor->GetGpuStats() : FContactClothGpuExplicitTopologyRealtimeStats();

	DrawRect(FLinearColor(0.008f, 0.018f, 0.032f, 0.92f), 28.0f, 26.0f, 760.0f, 540.0f);
	DrawRect(FLinearColor(0.0f, 0.72f, 0.95f, 1.0f), 28.0f, 26.0f, 7.0f, 540.0f);
	UFont* LargeFont = GEngine ? GEngine->GetLargeFont() : nullptr;
	UFont* MediumFont = GEngine ? GEngine->GetMediumFont() : nullptr;
	DrawText(Actor && Actor->IsReplayMode()
		? TEXT("CASE 01 · Assassin 确定性输入重放 + 接触")
		: TEXT("CASE 01 · Assassin 真实拓扑动画 + 接触"),
		FLinearColor::White, 50.0f, 45.0f, LargeFont, 1.0f, false);
	DrawText(Actor ? Actor->GetStatusText() : TEXT("未找到显式拓扑 Actor"), Actor && Actor->DidRunPass() ? FLinearColor(0.25f, 1.0f, 0.55f) : FLinearColor(0.3f, 0.9f, 1.0f), 50.0f, 88.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("固定 60 Hz 合同：%s    共享步：%lld / 300    GPU 已执行：%lld    最终快照：%lld"), Actor && Actor->IsFixedFrameContractActive() ? TEXT("有效") : TEXT("无效"), ExperimentStats.StepIndex, GpuStats.StepsExecuted, Actor ? Actor->GetLatestReadbackStep() : 0), FLinearColor::White, 50.0f, 124.0f, MediumFont, 0.9f, false);
	DrawText(FString::Printf(TEXT("真实拓扑：%d 粒子 / %d 边    固定：%d    动态：%d"), Actor ? Actor->GetParticleCount() : 0, Actor ? Actor->GetEdgeCount() : 0, Actor ? Actor->GetFixedVertexCount() : 0, Actor ? Actor->GetDynamicVertexCount() : 0), FLinearColor::White, 50.0f, 158.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("运动学目标：接受 %lld / 应用 %lld / 版本 %lld"), GpuStats.KinematicTargetUpdatesAccepted, GpuStats.KinematicTargetApplicationsExecuted, GpuStats.LatestKinematicTargetVersion), FLinearColor(0.3f, 0.9f, 1.0f), 50.0f, 192.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("碰撞快照：接受 %lld / 应用 %lld / 版本 %lld    %d 球 / %d 胶囊"), GpuStats.CollisionPrimitiveUpdatesAccepted, GpuStats.CollisionPrimitiveApplicationsExecuted, GpuStats.LatestCollisionPrimitiveVersion, Actor ? Actor->GetCollisionSphereCount() : 0, Actor ? Actor->GetCollisionCapsuleCount() : 0), FLinearColor(0.95f, 0.72f, 0.2f), 50.0f, 226.0f, MediumFont, 0.86f, false);
	DrawText(FString::Printf(TEXT("边界运动：%.4f cm    固定误差：%.7f cm    穿透：%.7f cm"), Actor ? Actor->GetAnimationTargetMotionCm() : 0.0, Actor ? Actor->GetFixedTargetErrorCm() : 0.0, Actor ? Actor->GetMaximumCollisionPenetrationCm() : 0.0), FLinearColor(0.95f, 0.72f, 0.2f), 50.0f, 260.0f, MediumFont, 0.9f, false);
	DrawText(FString::Printf(TEXT("动态点平均位移：%.4f cm    最大结构误差：%.4f cm"), Actor ? Actor->GetAverageDynamicDisplacementCm() : 0.0, Actor ? Actor->GetMaximumEdgeErrorCm() : 0.0), FLinearColor(0.82f, 0.88f, 0.96f), 50.0f, 294.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("模态/Rank-one/NDB/CCD：%lld/%lld/%lld/%lld    模态数：%d"), GpuStats.ModalCorrectionsExecuted, GpuStats.RankOneCollisionSystemsExecuted, GpuStats.NdbStateUpdatesExecuted, GpuStats.CompleteCcdPassesExecuted, Actor ? Actor->GetModalModeCount() : 0), FLinearColor(0.4f, 1.0f, 0.58f), 50.0f, 328.0f, MediumFont, 0.85f, false);
	DrawText(FString::Printf(TEXT("GPU 稳态：样本 %lld    均值 %.4f ms    P95/P99 %.4f/%.4f ms"), GpuStats.SteadyStateGpuTimingSamples, GpuStats.MeanSteadyStateGpuStepMilliseconds, GpuStats.P95SteadyStateGpuStepMilliseconds, GpuStats.P99SteadyStateGpuStepMilliseconds), FLinearColor(0.75f, 0.9f, 1.0f), 50.0f, 362.0f, MediumFont, 0.88f, false);
	DrawText(FString::Printf(TEXT("后端距离：当前均值 %.4f cm / 全程最大 %.4f cm"), Actor ? Actor->GetLatestBackendMeanDistanceCm() : 0.0, Actor ? Actor->GetMaximumBackendDistanceCm() : 0.0), FLinearColor(0.75f, 0.9f, 1.0f), 50.0f, 396.0f, MediumFont, 0.95f, false);
	DrawText(FString::Printf(TEXT("最大穿透：Chaos %.7f cm / 研究求解器 %.7f cm"), Actor ? Actor->GetLatestChaosPenetrationCm() : 0.0, Actor ? Actor->GetMaximumCollisionPenetrationCm() : 0.0), FLinearColor(0.95f, 0.72f, 0.2f), 50.0f, 430.0f, MediumFont, 0.95f, false);
	DrawText(TEXT("绿：Chaos    青：研究求解器    红：动画边界    橙：同姿态 Physics Asset"), FLinearColor(0.55f, 0.68f, 0.82f), 50.0f, 464.0f, MediumFont, 0.86f, false);
	DrawText(FString::Printf(TEXT("配置：%s    观察：%s    Debug：%s"),
		Actor ? *Actor->GetFailureProfileText() : TEXT("无"), Actor ? *Actor->GetDisplayModeText() : TEXT("无"),
		Actor && Actor->IsDebugDrawEnabled() ? TEXT("开") : TEXT("关")),
		Actor && Actor->IsWeakBarrierFailureProfile() ? FLinearColor(1.0f, 0.42f, 0.18f) : FLinearColor(0.75f, 0.9f, 1.0f),
		50.0f, 500.0f, MediumFont, 0.82f, false);
	DrawText(TEXT("Space 暂停/继续   N 单步   R 重置   B 切换后端   D 调试   F 故障注入   E 导出"),
		FLinearColor(0.88f, 0.92f, 0.98f), 50.0f, 534.0f, MediumFont, 0.78f, false);

	if (!Actor || Canvas->ClipX < 1100.0f)
	{
		return;
	}
	const float PanelX = Canvas->ClipX - 788.0f;
	const float PanelY = 26.0f;
	const float PanelWidth = 760.0f;
	const float PanelHeight = 470.0f;
	DrawRect(FLinearColor(0.008f, 0.018f, 0.032f, 0.92f), PanelX, PanelY, PanelWidth, PanelHeight);
	DrawRect(FLinearColor(0.0f, 0.72f, 0.95f, 1.0f), PanelX, PanelY, 7.0f, PanelHeight);
	DrawText(TEXT("同源步响应曲线 · 不是算法误差趋零要求"), FLinearColor::White, PanelX + 22.0f, PanelY + 18.0f, LargeFont, 0.92f, false);

	const float PlotX = PanelX + 54.0f;
	const float PlotWidth = PanelWidth - 82.0f;
	const auto DrawPlotFrame = [this, MediumFont, PlotX, PlotWidth](float PlotY, float PlotHeight, const FString& Title, const FString& MaximumLabel)
	{
		DrawRect(FLinearColor(0.02f, 0.04f, 0.065f, 0.95f), PlotX, PlotY, PlotWidth, PlotHeight);
		DrawLine(PlotX, PlotY + PlotHeight, PlotX + PlotWidth, PlotY + PlotHeight, FLinearColor(0.25f, 0.34f, 0.45f), 1.0f);
		DrawLine(PlotX, PlotY, PlotX, PlotY + PlotHeight, FLinearColor(0.25f, 0.34f, 0.45f), 1.0f);
		DrawText(Title, FLinearColor(0.82f, 0.88f, 0.96f), PlotX, PlotY - 22.0f, MediumFont, 0.78f, false);
		DrawText(MaximumLabel, FLinearColor(0.55f, 0.68f, 0.82f), PlotX + PlotWidth - 146.0f, PlotY - 22.0f, MediumFont, 0.72f, false);
		DrawText(TEXT("1"), FLinearColor(0.55f, 0.68f, 0.82f), PlotX - 2.0f, PlotY + PlotHeight + 2.0f, MediumFont, 0.68f, false);
		DrawText(TEXT("300 步"), FLinearColor(0.55f, 0.68f, 0.82f), PlotX + PlotWidth - 44.0f, PlotY + PlotHeight + 2.0f, MediumFont, 0.68f, false);
	};

	const TArray<FContactClothBackendComparisonSample>& Samples = Actor->GetComparisonSamples();
	double DisplacementMaximum = 1.0;
	for (const FContactClothBackendComparisonSample& Sample : Samples)
	{
		DisplacementMaximum = FMath::Max(DisplacementMaximum,
			FMath::Max(Sample.ChaosMeanDynamicDisplacementCm, Sample.ResearchMeanDynamicDisplacementCm));
	}
	const float DisplacementY = PanelY + 88.0f;
	const float DisplacementHeight = 118.0f;
	DrawPlotFrame(DisplacementY, DisplacementHeight, TEXT("动态点平均位移（cm）"),
		FString::Printf(TEXT("纵轴 0–%.1f"), DisplacementMaximum));
	for (int32 Index = 1; Index < Samples.Num(); ++Index)
	{
		const auto ToPoint = [PlotX, PlotWidth, DisplacementY, DisplacementHeight, DisplacementMaximum](int64 Step, double Value)
		{
			return FVector2D(
				PlotX + PlotWidth * static_cast<float>(Step) / 300.0f,
				DisplacementY + DisplacementHeight * (1.0f - FMath::Clamp(static_cast<float>(Value / DisplacementMaximum), 0.0f, 1.0f)));
		};
		const FContactClothBackendComparisonSample& Previous = Samples[Index - 1];
		const FContactClothBackendComparisonSample& Current = Samples[Index];
		const FVector2D ChaosA = ToPoint(Previous.Step, Previous.ChaosMeanDynamicDisplacementCm);
		const FVector2D ChaosB = ToPoint(Current.Step, Current.ChaosMeanDynamicDisplacementCm);
		const FVector2D ResearchA = ToPoint(Previous.Step, Previous.ResearchMeanDynamicDisplacementCm);
		const FVector2D ResearchB = ToPoint(Current.Step, Current.ResearchMeanDynamicDisplacementCm);
		DrawLine(ChaosA.X, ChaosA.Y, ChaosB.X, ChaosB.Y, FLinearColor(0.20f, 1.0f, 0.45f), 2.0f);
		DrawLine(ResearchA.X, ResearchA.Y, ResearchB.X, ResearchB.Y, FLinearColor(0.0f, 0.82f, 1.0f), 2.0f);
	}
	DrawText(TEXT("绿 Chaos"), FLinearColor(0.20f, 1.0f, 0.45f), PlotX + 10.0f, DisplacementY + 8.0f, MediumFont, 0.72f, false);
	DrawText(TEXT("青 研究求解器"), FLinearColor(0.0f, 0.82f, 1.0f), PlotX + 82.0f, DisplacementY + 8.0f, MediumFont, 0.72f, false);

	TArray<int64> TimingSteps;
	TArray<double> TimingMilliseconds;
	Actor->GetGpuTimingSamples(TimingSteps, TimingMilliseconds);
	const double TimingMaximum = FMath::Max(0.25, GpuStats.P99SteadyStateGpuStepMilliseconds * 1.25);
	const float TimingY = PanelY + 292.0f;
	const float TimingHeight = 118.0f;
	DrawPlotFrame(TimingY, TimingHeight, TEXT("完整固定步 GPU 时间（ms）"),
		FString::Printf(TEXT("稳态纵轴 0–%.2f"), TimingMaximum));
	const float WarmupX = PlotX + PlotWidth * 16.0f / 300.0f;
	DrawLine(WarmupX, TimingY, WarmupX, TimingY + TimingHeight, FLinearColor(0.95f, 0.72f, 0.2f), 1.0f);
	const float P95Y = TimingY + TimingHeight * (1.0f - FMath::Clamp(static_cast<float>(GpuStats.P95SteadyStateGpuStepMilliseconds / TimingMaximum), 0.0f, 1.0f));
	DrawLine(PlotX, P95Y, PlotX + PlotWidth, P95Y, FLinearColor(0.55f, 0.68f, 0.82f), 1.0f);
	for (int32 Index = 1; Index < TimingSteps.Num() && Index < TimingMilliseconds.Num(); ++Index)
	{
		const double PreviousValue = TimingMilliseconds[Index - 1];
		const double CurrentValue = TimingMilliseconds[Index];
		const float AX = PlotX + PlotWidth * static_cast<float>(TimingSteps[Index - 1]) / 300.0f;
		const float BX = PlotX + PlotWidth * static_cast<float>(TimingSteps[Index]) / 300.0f;
		const float AY = TimingY + TimingHeight * (1.0f - FMath::Clamp(static_cast<float>(PreviousValue / TimingMaximum), 0.0f, 1.0f));
		const float BY = TimingY + TimingHeight * (1.0f - FMath::Clamp(static_cast<float>(CurrentValue / TimingMaximum), 0.0f, 1.0f));
		const bool bClipped = PreviousValue > TimingMaximum || CurrentValue > TimingMaximum;
		DrawLine(AX, AY, BX, BY, bClipped ? FLinearColor(1.0f, 0.18f, 0.12f) : FLinearColor(0.0f, 0.82f, 1.0f), bClipped ? 2.5f : 1.5f);
	}
	DrawText(TEXT("橙：预热 16 步"), FLinearColor(0.95f, 0.72f, 0.2f), PlotX + 10.0f, TimingY + 8.0f, MediumFont, 0.72f, false);
	DrawText(FString::Printf(TEXT("灰：稳态 P95 %.3f ms"), GpuStats.P95SteadyStateGpuStepMilliseconds), FLinearColor(0.65f, 0.74f, 0.84f), PlotX + 126.0f, TimingY + 8.0f, MediumFont, 0.72f, false);
	DrawText(FString::Printf(TEXT("红：超出稳态纵轴（全程最大 %.3f ms，未丢弃）"), GpuStats.MaximumGpuStepMilliseconds), FLinearColor(1.0f, 0.32f, 0.22f), PlotX + 310.0f, TimingY + 8.0f, MediumFont, 0.68f, false);
}

AContactClothExplicitTopologyActor* AContactClothExplicitTopologyHUD::FindExplicitActor() const
{
	if (!GetWorld())
	{
		return nullptr;
	}
	for (TActorIterator<AContactClothExplicitTopologyActor> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

AContactClothExplicitTopologyGameMode::AContactClothExplicitTopologyGameMode()
{
	HUDClass = AContactClothExplicitTopologyHUD::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	bStartPlayersAsSpectators = true;
}
