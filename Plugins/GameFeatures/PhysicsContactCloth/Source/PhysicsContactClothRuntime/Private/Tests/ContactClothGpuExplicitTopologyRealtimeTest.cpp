#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothChaosTopology.h"
#include "ContactClothGpuExplicitTopology.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuExplicitTopologyRealtimeTest,
	"PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.Realtime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuExplicitTopologyRealtimeTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assassin/Mesh/SKM_Assassin_Skin1.SKM_Assassin_Skin1"));
	TestNotNull(TEXT("加载 Assassin Skeletal Mesh"), Mesh);
	if (!Mesh) return false;

	FContactClothExplicitTopology Topology;
	FString ExtractionFailure;
	TestTrue(TEXT("提取 Assassin Chaos LOD0 显式拓扑"), FContactClothChaosTopology::ExtractLod0(*Mesh, Topology, &ExtractionFailure));
	if (!Topology.IsValid())
	{
		AddError(ExtractionFailure);
		return false;
	}

	FContactClothGpuExplicitTopologyRealtimeSettings Settings;
	Settings.Iterations = 12;
	Settings.Relaxation = 0.9f;
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.GravityCmPerSecondSquared = FVector3f(0.0f, 0.0f, -980.0f);
	Settings.VelocityDamping = 0.98f;
	Settings.ReadbackIntervalSteps = 15;
	FContactClothGpuExplicitTopologySimulation Simulation;
	TestTrue(TEXT("初始化 Assassin 任意拓扑持久 GPU 会话"), Simulation.Initialize(
		Topology.RestPositions,
		Topology.InverseMasses,
		Topology.StructuralEdges,
		Topology.StructuralRestLengthsCm,
		Settings));

	constexpr int32 StepCount = 90;
	for (int32 Step = 0; Step < StepCount; ++Step)
	{
		TestTrue(TEXT("固定步成功入队"), Simulation.EnqueueStep());
	}
	Simulation.FlushAndWaitForReadbackForValidation();
	const FContactClothGpuExplicitTopologyRealtimeStats Stats = Simulation.GetStats();
	TArray<FVector3f> OutputPositions;
	int64 ReadbackStep = 0;
	const bool bConsumed = Simulation.ConsumeLatestPositions(OutputPositions, ReadbackStep);

	double MaximumFixedDriftCm = 0.0;
	double DynamicDisplacementSumCm = 0.0;
	double DynamicVerticalDisplacementSumCm = 0.0;
	int32 DynamicCount = 0;
	TArray<double> DynamicDisplacementsCm;
	DynamicDisplacementsCm.Reserve(Topology.DynamicVertexCount);
	for (int32 ParticleIndex = 0; ParticleIndex < OutputPositions.Num(); ++ParticleIndex)
	{
		const FVector3f Delta = OutputPositions[ParticleIndex] - Topology.RestPositions[ParticleIndex];
		if (Topology.InverseMasses[ParticleIndex] > 0.0f)
		{
			const double DisplacementCm = Delta.Length();
			DynamicDisplacementSumCm += DisplacementCm;
			DynamicVerticalDisplacementSumCm += Delta.Z;
			DynamicDisplacementsCm.Add(DisplacementCm);
			DynamicCount++;
		}
		else
		{
			MaximumFixedDriftCm = FMath::Max(MaximumFixedDriftCm, static_cast<double>(Delta.Length()));
		}
	}
	const double AverageDynamicDisplacementCm = DynamicCount > 0 ? DynamicDisplacementSumCm / DynamicCount : 0.0;
	const double AverageDynamicVerticalDisplacementCm = DynamicCount > 0 ? DynamicVerticalDisplacementSumCm / DynamicCount : 0.0;
	const double MaximumEdgeErrorCm = FContactClothGpuExplicitTopology::MeasureMaximumEdgeErrorCm(
		OutputPositions, Topology.StructuralEdges, Topology.StructuralRestLengthsCm);
	TArray<double> EdgeErrorsCm;
	EdgeErrorsCm.Reserve(Topology.StructuralEdges.Num());
	double EdgeErrorSumCm = 0.0;
	for (int32 EdgeIndex = 0; EdgeIndex < Topology.StructuralEdges.Num(); ++EdgeIndex)
	{
		const FIntPoint Edge = Topology.StructuralEdges[EdgeIndex];
		const double ErrorCm = FMath::Abs(
			static_cast<double>((OutputPositions[Edge.X] - OutputPositions[Edge.Y]).Length())
			- Topology.StructuralRestLengthsCm[EdgeIndex]);
		EdgeErrorsCm.Add(ErrorCm);
		EdgeErrorSumCm += ErrorCm;
	}
	DynamicDisplacementsCm.Sort();
	EdgeErrorsCm.Sort();
	const auto Percentile = [](TConstArrayView<double> SortedValues, double Fraction)
	{
		return SortedValues.IsEmpty()
			? 0.0
			: SortedValues[FMath::Clamp(FMath::CeilToInt(Fraction * (SortedValues.Num() - 1)), 0, SortedValues.Num() - 1)];
	};
	const double DynamicDisplacementP95Cm = Percentile(DynamicDisplacementsCm, 0.95);
	const double AverageEdgeErrorCm = EdgeErrorsCm.IsEmpty() ? 0.0 : EdgeErrorSumCm / EdgeErrorsCm.Num();
	const double EdgeErrorP95Cm = Percentile(EdgeErrorsCm, 0.95);
	const double EdgeErrorP99Cm = Percentile(EdgeErrorsCm, 0.99);
	const double AverageSubmissionMilliseconds = Stats.StepsExecuted > 0
		? Stats.TotalRenderSubmissionMilliseconds / Stats.StepsExecuted
		: 0.0;
	AddInfo(FString::Printf(
		TEXT("GPU explicit realtime Chaos topology: particles=%d edges=%d fixed=%d dynamic=%d steps=%lld readbacks=%lld final_readback_step=%lld average_dynamic_displacement_cm=%.6f dynamic_displacement_p95_cm=%.6f average_dynamic_z_cm=%.6f average_edge_error_cm=%.6f edge_error_p95_cm=%.6f edge_error_p99_cm=%.6f maximum_edge_error_cm=%.6f fixed_drift_cm=%.9f average_submission_ms=%.6f maximum_submission_ms=%.6f"),
		Topology.RestPositions.Num(), Topology.StructuralEdges.Num(), Topology.FixedVertexCount, Topology.DynamicVertexCount,
		Stats.StepsExecuted, Stats.ReadbacksCompleted, ReadbackStep, AverageDynamicDisplacementCm, DynamicDisplacementP95Cm,
		AverageDynamicVerticalDisplacementCm, AverageEdgeErrorCm, EdgeErrorP95Cm, EdgeErrorP99Cm, MaximumEdgeErrorCm, MaximumFixedDriftCm,
		AverageSubmissionMilliseconds, Stats.MaximumRenderSubmissionMilliseconds));
	TestTrue(TEXT("持久会话初始化在渲染线程完成"), Stats.bInitialized);
	TestEqual(TEXT("全部固定步入队"), Stats.StepsEnqueued, static_cast<int64>(StepCount));
	TestEqual(TEXT("全部固定步由 GPU 会话执行"), Stats.StepsExecuted, static_cast<int64>(StepCount));
	TestTrue(TEXT("异步回读至少完成一次"), Stats.ReadbacksCompleted > 0);
	TestTrue(TEXT("最终位置快照可消费"), bConsumed);
	TestEqual(TEXT("最终快照精确对应最后固定步"), ReadbackStep, static_cast<int64>(StepCount));
	TestEqual(TEXT("最终快照覆盖全部 2011 粒子"), OutputPositions.Num(), 2011);
	TestTrue(TEXT("704 个动态粒子对重力产生有限响应"), FMath::IsFinite(AverageDynamicDisplacementCm) && AverageDynamicDisplacementCm > 0.01);
	TestTrue(TEXT("动态粒子平均沿重力方向移动"), FMath::IsFinite(AverageDynamicVerticalDisplacementCm) && AverageDynamicVerticalDisplacementCm < -0.01);
	TestTrue(TEXT("结构边 P99 误差保持有限"), FMath::IsFinite(EdgeErrorP99Cm));
	TestTrue(TEXT("最大结构边误差保持有限且受控"), FMath::IsFinite(MaximumEdgeErrorCm) && MaximumEdgeErrorCm < 10.0);
	TestTrue(TEXT("1307 个固定粒子跨 90 步不漂移"), MaximumFixedDriftCm < 1.e-5);
	Simulation.Shutdown();
	return true;
}

#endif
