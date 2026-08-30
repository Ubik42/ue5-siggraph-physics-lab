#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothChaosTopology.h"
#include "ContactClothGpuExplicitTopology.h"
#include "ContactClothSkinningAdapter.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuKinematicTargetsTest,
	"PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.KinematicTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuKinematicTargetsTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assassin/Mesh/SKM_Assassin_Skin1.SKM_Assassin_Skin1"));
	TestNotNull(TEXT("加载 Assassin Skeletal Mesh"), Mesh);
	if (!Mesh) return false;
	FContactClothExplicitTopology Topology;
	FString FailureReason;
	TestTrue(TEXT("提取带骨骼合同的显式拓扑"), FContactClothChaosTopology::ExtractLod0(*Mesh, Topology, &FailureReason));
	if (!Topology.IsValid())
	{
		AddError(FailureReason);
		return false;
	}

	FContactClothGpuExplicitTopologyRealtimeSettings Settings;
	Settings.Iterations = 12;
	Settings.Relaxation = 0.9f;
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.GravityCmPerSecondSquared = FVector3f::ZeroVector;
	Settings.VelocityDamping = 0.95f;
	Settings.ReadbackIntervalSteps = 10;
	FContactClothGpuExplicitTopologySimulation Simulation;
	TestTrue(TEXT("初始化显式拓扑持久 GPU 会话"), Simulation.Initialize(
		Topology.RestPositions, Topology.InverseMasses, Topology.StructuralEdges,
		Topology.StructuralRestLengthsCm, Settings));

	constexpr int32 StepCount = 30;
	FContactClothSkinnedTargets FinalTargets;
	bool bAllSkinningSucceeded = true;
	bool bAllTargetUpdatesAccepted = true;
	bool bAllStepsEnqueued = true;
	for (int32 Step = 1; Step <= StepCount; ++Step)
	{
		const float Alpha = static_cast<float>(Step) / StepCount;
		const FTransform PoseTransform(
			FRotator(0.0, 17.0 * Alpha, 0.0),
			FVector(12.0 * Alpha, -7.0 * Alpha, 5.0 * Alpha));
		TArray<FMatrix44f> RefToLocalMatrices;
		RefToLocalMatrices.Init(FMatrix44f(PoseTransform.ToMatrixWithScale()), Mesh->GetRefSkeleton().GetNum());
		FContactClothSkinnedTargets Targets;
		bAllSkinningSucceeded &= FContactClothSkinningAdapter::SkinRefToLocalMatrices(
			*Mesh, Topology, RefToLocalMatrices, Targets, &FailureReason);
		if (!bAllSkinningSucceeded) break;
		bAllTargetUpdatesAccepted &= Simulation.UpdateKinematicTargets(Targets.Positions);
		bAllStepsEnqueued &= Simulation.EnqueueStep();
		FinalTargets = MoveTemp(Targets);
	}
	Simulation.FlushAndWaitForReadbackForValidation();
	const FContactClothGpuExplicitTopologyRealtimeStats Stats = Simulation.GetStats();
	TArray<FVector3f> OutputPositions;
	int64 ReadbackStep = 0;
	const bool bConsumed = Simulation.ConsumeLatestPositions(OutputPositions, ReadbackStep);

	double MaximumFixedTargetErrorCm = 0.0;
	double MinimumFixedMotionCm = TNumericLimits<double>::Max();
	double DynamicDisplacementSumCm = 0.0;
	int32 DynamicCount = 0;
	for (int32 ParticleIndex = 0; ParticleIndex < OutputPositions.Num(); ++ParticleIndex)
	{
		const double MotionCm = (OutputPositions[ParticleIndex] - Topology.RestPositions[ParticleIndex]).Length();
		if (Topology.InverseMasses[ParticleIndex] <= 0.0f)
		{
			MaximumFixedTargetErrorCm = FMath::Max(MaximumFixedTargetErrorCm, static_cast<double>(
				(OutputPositions[ParticleIndex] - FinalTargets.Positions[ParticleIndex]).Length()));
			MinimumFixedMotionCm = FMath::Min(MinimumFixedMotionCm, MotionCm);
		}
		else
		{
			DynamicDisplacementSumCm += MotionCm;
			DynamicCount++;
		}
	}
	const double AverageDynamicDisplacementCm = DynamicCount > 0 ? DynamicDisplacementSumCm / DynamicCount : 0.0;
	const double MaximumEdgeErrorCm = FContactClothGpuExplicitTopology::MeasureMaximumEdgeErrorCm(
		OutputPositions, Topology.StructuralEdges, Topology.StructuralRestLengthsCm);
	AddInfo(FString::Printf(
		TEXT("GPU kinematic cloth targets: particles=%d fixed=%d dynamic=%d steps=%lld target_updates=%lld target_applications=%lld target_version=%lld final_readback_step=%lld fixed_target_error_cm=%.9f minimum_fixed_motion_cm=%.6f average_dynamic_displacement_cm=%.6f maximum_edge_error_cm=%.6f"),
		Topology.RestPositions.Num(), Topology.FixedVertexCount, Topology.DynamicVertexCount,
		Stats.StepsExecuted, Stats.KinematicTargetUpdatesAccepted, Stats.KinematicTargetApplicationsExecuted,
		Stats.LatestKinematicTargetVersion, ReadbackStep, MaximumFixedTargetErrorCm,
		MinimumFixedMotionCm, AverageDynamicDisplacementCm, MaximumEdgeErrorCm));
	if (!FailureReason.IsEmpty()) AddError(FailureReason);
	TestTrue(TEXT("30 组 UE Clothing 蒙皮目标全部生成"), bAllSkinningSucceeded);
	TestTrue(TEXT("30 组运动学目标全部被会话接受"), bAllTargetUpdatesAccepted);
	TestTrue(TEXT("30 个固定步全部入队"), bAllStepsEnqueued);
	TestEqual(TEXT("30 个固定步全部执行"), Stats.StepsExecuted, static_cast<int64>(StepCount));
	TestEqual(TEXT("目标更新计数为 30"), Stats.KinematicTargetUpdatesAccepted, static_cast<int64>(StepCount));
	TestEqual(TEXT("每个固定步应用一次目标"), Stats.KinematicTargetApplicationsExecuted, static_cast<int64>(StepCount));
	TestEqual(TEXT("最终目标版本为 30"), Stats.LatestKinematicTargetVersion, static_cast<int64>(StepCount));
	TestTrue(TEXT("最终 GPU 快照可消费"), bConsumed);
	TestEqual(TEXT("最终快照精确对应第 30 步"), ReadbackStep, static_cast<int64>(StepCount));
	TestEqual(TEXT("最终快照覆盖 2011 粒子"), OutputPositions.Num(), 2011);
	TestTrue(TEXT("固定粒子准确跟随最终蒙皮目标"), MaximumFixedTargetErrorCm < 1.e-5);
	TestTrue(TEXT("全部固定粒子产生明显目标运动"), MinimumFixedMotionCm > 1.0);
	TestTrue(TEXT("动态粒子被运动学边界带动"), FMath::IsFinite(AverageDynamicDisplacementCm) && AverageDynamicDisplacementCm > 0.1);
	TestTrue(TEXT("移动边界下结构误差保持有限"), FMath::IsFinite(MaximumEdgeErrorCm) && MaximumEdgeErrorCm < 15.0);
	Simulation.Shutdown();
	return true;
}

#endif
