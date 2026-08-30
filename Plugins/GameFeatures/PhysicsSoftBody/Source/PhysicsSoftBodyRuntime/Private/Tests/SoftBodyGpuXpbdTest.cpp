#if WITH_DEV_AUTOMATION_TESTS

#include "SoftBodyGpuTopology.h"
#include "SoftBodyGpuXpbd.h"
#include "SoftBodyLocalXpbdSolver.h"

#include "Misc/AutomationTest.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "HAL/PlatformProcess.h"

namespace
{
FSoftBodyGpuXpbdInput MakeGpuInput(const FSoftBodyTetMesh& Mesh, const FSoftBodyGpuTopology& Topology)
{
	FSoftBodyGpuXpbdInput Input;
	Input.RestPositions = Mesh.RestPositions;
	Input.InverseMasses = Mesh.InverseMasses;
	Input.EdgeVertices = Topology.EdgeVertices;
	Input.EdgeRestLengthsCm = Topology.EdgeRestLengthsCm;
	Input.EdgeMaterialIds = Topology.EdgeMaterialIds;
	Input.VertexEdgeOffsets = Topology.VertexEdgeOffsets;
	Input.VertexEdgeConstraintIndices = Topology.VertexEdgeConstraintIndices;
	Input.VertexEdgeCorners = Topology.VertexEdgeCorners;
	Input.TetrahedronVertices = Topology.TetrahedronVertices;
	Input.TetrahedronRestVolumesCm3 = Topology.TetrahedronRestVolumesCm3;
	Input.TetrahedronMaterialIds = Topology.TetrahedronMaterialIds;
	Input.VertexTetrahedronOffsets = Topology.VertexTetrahedronOffsets;
	Input.VertexTetrahedronConstraintIndices = Topology.VertexTetrahedronConstraintIndices;
	Input.VertexTetrahedronCorners = Topology.VertexTetrahedronCorners;
	return Input;
}

double SignedVolume(const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D)
{
	return FVector3d::DotProduct(FVector3d(B - A), FVector3d::CrossProduct(FVector3d(C - A), FVector3d(D - A))) / 6.0;
}

struct FGpuOutputMetrics
{
	double ResidualRms = 0.0;
	double MaximumEdgeRelativeError = 0.0;
	double MeanVolumeRelativeError = 0.0;
	double FixedDriftCm = 0.0;
	double TipDisplacementCm = 0.0;
};

FGpuOutputMetrics MeasureOutput(
	const FSoftBodyTetMesh& Mesh,
	const FSoftBodyGpuTopology& Topology,
	TConstArrayView<FVector3f> Positions)
{
	FGpuOutputMetrics Metrics;
	double ResidualSquaredSum = 0.0;
	int32 ResidualCount = 0;
	for (int32 EdgeIndex = 0; EdgeIndex < Topology.EdgeVertices.Num(); ++EdgeIndex)
	{
		const FIntPoint Edge = Topology.EdgeVertices[EdgeIndex];
		const double Relative = ((Positions[Edge.Y] - Positions[Edge.X]).Length() - Topology.EdgeRestLengthsCm[EdgeIndex])
			/ Topology.EdgeRestLengthsCm[EdgeIndex];
		ResidualSquaredSum += Relative * Relative;
		Metrics.MaximumEdgeRelativeError = FMath::Max(Metrics.MaximumEdgeRelativeError, FMath::Abs(Relative));
		++ResidualCount;
	}
	double VolumeErrorSum = 0.0;
	for (int32 TetIndex = 0; TetIndex < Topology.TetrahedronVertices.Num(); ++TetIndex)
	{
		const FIntVector4 Tet = Topology.TetrahedronVertices[TetIndex];
		const double Relative = (SignedVolume(Positions[Tet.X], Positions[Tet.Y], Positions[Tet.Z], Positions[Tet.W])
			- Topology.TetrahedronRestVolumesCm3[TetIndex]) / Topology.TetrahedronRestVolumesCm3[TetIndex];
		ResidualSquaredSum += Relative * Relative;
		VolumeErrorSum += FMath::Abs(Relative);
		++ResidualCount;
	}
	Metrics.ResidualRms = FMath::Sqrt(ResidualSquaredSum / ResidualCount);
	Metrics.MeanVolumeRelativeError = VolumeErrorSum / Topology.TetrahedronVertices.Num();
	double MaximumRestX = -TNumericLimits<double>::Max();
	for (const FVector3f& Rest : Mesh.RestPositions) MaximumRestX = FMath::Max(MaximumRestX, static_cast<double>(Rest.X));
	double TipSum = 0.0;
	int32 TipCount = 0;
	for (int32 Vertex = 0; Vertex < Positions.Num(); ++Vertex)
	{
		if (Mesh.InverseMasses[Vertex] <= 0.0f)
		{
			Metrics.FixedDriftCm = FMath::Max(Metrics.FixedDriftCm,
				static_cast<double>((Positions[Vertex] - Mesh.RestPositions[Vertex]).Length()));
		}
		if (FMath::IsNearlyEqual(static_cast<double>(Mesh.RestPositions[Vertex].X), MaximumRestX))
		{
			TipSum += Mesh.RestPositions[Vertex].Z - Positions[Vertex].Z;
			++TipCount;
		}
	}
	Metrics.TipDisplacementCm = TipCount > 0 ? TipSum / TipCount : 0.0;
	return Metrics;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoftBodyGpuXpbdPersistentTest,
	"PhysicsResearch.SoftBody.GpuXpbd.PersistentCantilever",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSoftBodyGpuXpbdPersistentTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过真实软体 Compute；D3D12 报告单独生成。"));
		return true;
	}

	FSoftBodyTetGridSettings GridSettings;
	GridSettings.CellCounts = FIntVector(4, 2, 2);
	GridSettings.CellSizeCm = FVector3f(10.0f);
	FSoftBodyTetMesh Mesh;
	FSoftBodyGpuTopology Topology;
	FString FailureReason;
	TestTrue(TEXT("生成 GPU 悬臂四面体"), FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(GridSettings, Mesh, &FailureReason));
	TestTrue(TEXT("生成 GPU 悬臂 CSR"), FSoftBodyGpuTopologyBuilder::Build(Mesh, Topology, &FailureReason));

	FSoftBodyGpuXpbdSettings Settings;
	Settings.SolverIterations = 32;
	Settings.ReadbackIntervalSteps = 8;
	FSoftBodyGpuXpbdSimulation Simulation;
	TestTrue(TEXT("初始化持久软体 GPU 会话"), Simulation.Initialize(MakeGpuInput(Mesh, Topology), Settings));
	constexpr int32 StepCount = 120;
	bool bAllStepsAccepted = true;
	for (int32 Step = 0; Step < StepCount; ++Step) bAllStepsAccepted &= Simulation.EnqueueStep();
	TestTrue(TEXT("120 个固定步全部进入 GPU 队列"), bAllStepsAccepted);
	Simulation.FlushAndWaitForReadbackForValidation();

	TArray<FVector3f> GpuPositions;
	int64 ReadbackStep = 0;
	const bool bReadback = Simulation.ConsumeLatestPositions(GpuPositions, ReadbackStep);
	const FSoftBodyGpuXpbdStats GpuStats = Simulation.GetStats();
	TestTrue(TEXT("最终软体 GPU 快照成功消费"), bReadback);
	TestEqual(TEXT("GPU 执行全部固定步"), GpuStats.StepsExecuted, static_cast<int64>(StepCount));
	TestEqual(TEXT("最终快照对应第 120 步"), ReadbackStep, static_cast<int64>(StepCount));
	TestEqual(TEXT("GPU 输出顶点数"), GpuPositions.Num(), Mesh.RestPositions.Num());
	TestEqual(TEXT("距离 Pass 计数"), GpuStats.DistancePassesExecuted, static_cast<int64>(StepCount * Settings.SolverIterations));
	TestEqual(TEXT("体积 Pass 计数"), GpuStats.VolumePassesExecuted, static_cast<int64>(StepCount * Settings.SolverIterations));
	TestEqual(TEXT("聚合 Pass 计数"), GpuStats.GatherPassesExecuted, static_cast<int64>(StepCount * Settings.SolverIterations));

	const FGpuOutputMetrics GpuMetrics = MeasureOutput(Mesh, Topology, GpuPositions);
	TestTrue(TEXT("GPU 残差有限且有界"), FMath::IsFinite(GpuMetrics.ResidualRms) && GpuMetrics.ResidualRms < 0.15);
	TestTrue(TEXT("GPU 最大边误差有限且有界"), FMath::IsFinite(GpuMetrics.MaximumEdgeRelativeError) && GpuMetrics.MaximumEdgeRelativeError < 0.40);
	TestTrue(TEXT("GPU 平均体积误差有限且有界"), FMath::IsFinite(GpuMetrics.MeanVolumeRelativeError) && GpuMetrics.MeanVolumeRelativeError < 0.15);
	TestTrue(TEXT("GPU 固定端零漂移"), GpuMetrics.FixedDriftCm < 1.e-3);
	TestTrue(TEXT("GPU 自由端产生有限下垂"), GpuMetrics.TipDisplacementCm > 1.0 && GpuMetrics.TipDisplacementCm < 200.0);

	FSoftBodyLocalXpbdSettings CpuSettings;
	CpuSettings.SolverIterations = Settings.SolverIterations;
	FSoftBodyLocalXpbdSolver CpuSolver;
	TestTrue(TEXT("初始化同预算 CPU 对照"), CpuSolver.Initialize(Mesh, CpuSettings, &FailureReason));
	FSoftBodyLocalXpbdStats CpuStats;
	for (int32 Step = 0; Step < StepCount; ++Step)
	{
		if (!CpuSolver.Step(CpuStats, &FailureReason))
		{
			AddError(FailureReason);
			return false;
		}
	}
	TestTrue(TEXT("GPU 与 CPU 残差保持同一数量级"),
		GpuMetrics.ResidualRms > CpuStats.ResidualRms * 0.5 && GpuMetrics.ResidualRms < CpuStats.ResidualRms * 2.0);
	AddInfo(FString::Printf(
		TEXT("Persistent GPU soft body: rhi=%s steps=%lld iterations=%d vertices=%d edges=%d tets=%d readbacks=%lld latest_step=%lld distance_passes=%lld volume_passes=%lld gather_passes=%lld gpu_residual=%.9f cpu_residual=%.9f residual_ratio=%.9f gpu_edge_max=%.9f gpu_volume_mean=%.9f gpu_tip_cm=%.6f gpu_fixed_drift_cm=%.9f"),
		GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None"), GpuStats.StepsExecuted, Settings.SolverIterations,
		Mesh.RestPositions.Num(), Topology.EdgeVertices.Num(), Topology.TetrahedronVertices.Num(), GpuStats.ReadbacksCompleted,
		ReadbackStep, GpuStats.DistancePassesExecuted, GpuStats.VolumePassesExecuted, GpuStats.GatherPassesExecuted,
		GpuMetrics.ResidualRms, CpuStats.ResidualRms, GpuMetrics.ResidualRms / CpuStats.ResidualRms,
		GpuMetrics.MaximumEdgeRelativeError, GpuMetrics.MeanVolumeRelativeError,
		GpuMetrics.TipDisplacementCm, GpuMetrics.FixedDriftCm));
	Simulation.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoftBodyGpuXpbdResolutionScalingTest,
	"PhysicsResearch.SoftBody.GpuXpbd.ResolutionScaling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSoftBodyGpuXpbdResolutionScalingTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过软体 GPU 三档 timestamp；D3D12 报告单独生成。"));
		return true;
	}

	struct FTierResult
	{
		FString Name;
		int32 Vertices = 0;
		int32 Edges = 0;
		int32 Tetrahedra = 0;
		FSoftBodyGpuXpbdStats Stats;
		FGpuOutputMetrics Metrics;
		TArray<int64> TimingSteps;
		TArray<double> TimingMilliseconds;
		bool bSucceeded = false;
	};
	constexpr int32 StepCount = 24;
	constexpr int32 IterationCount = 32;
	auto RunTier = [this](const TCHAR* Name, const FIntVector& Cells) -> FTierResult
	{
		FTierResult Result;
		Result.Name = Name;
		FSoftBodyTetGridSettings GridSettings;
		GridSettings.CellCounts = Cells;
		GridSettings.CellSizeCm = FVector3f(
			40.0f / Cells.X,
			20.0f / Cells.Y,
			20.0f / Cells.Z);
		FSoftBodyTetMesh Mesh;
		FSoftBodyGpuTopology Topology;
		FString FailureReason;
		if (!FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(GridSettings, Mesh, &FailureReason)
			|| !FSoftBodyGpuTopologyBuilder::Build(Mesh, Topology, &FailureReason))
		{
			AddError(FailureReason);
			return Result;
		}
		FSoftBodyGpuXpbdSettings Settings;
		Settings.SolverIterations = IterationCount;
		Settings.ReadbackIntervalSteps = StepCount;
		Settings.GpuTimingWarmupSteps = 6;
		FSoftBodyGpuXpbdSimulation Simulation;
		if (!Simulation.Initialize(MakeGpuInput(Mesh, Topology), Settings))
		{
			AddError(FString::Printf(TEXT("%s 档 GPU 会话初始化失败"), Name));
			return Result;
		}
		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			if (!Simulation.EnqueueStep())
			{
				AddError(FString::Printf(TEXT("%s 档第 %d 步未进入 GPU 队列"), Name, Step + 1));
				Simulation.Shutdown();
				return Result;
			}
			FlushRenderingCommands();
			FPlatformProcess::SleepNoStats(0.001f);
			Simulation.PollReadback();
			FlushRenderingCommands();
		}
		Simulation.FlushAndWaitForReadbackForValidation();
		TArray<FVector3f> Positions;
		int64 ReadbackStep = 0;
		if (!Simulation.ConsumeLatestPositions(Positions, ReadbackStep) || ReadbackStep != StepCount)
		{
			AddError(FString::Printf(TEXT("%s 档最终异步快照缺失或步号错误"), Name));
			Simulation.Shutdown();
			return Result;
		}
		Result.Vertices = Mesh.RestPositions.Num();
		Result.Edges = Topology.EdgeVertices.Num();
		Result.Tetrahedra = Topology.TetrahedronVertices.Num();
		Result.Stats = Simulation.GetStats();
		Result.Metrics = MeasureOutput(Mesh, Topology, Positions);
		Simulation.GetGpuTimingSamples(Result.TimingSteps, Result.TimingMilliseconds);
		Result.bSucceeded = true;
		Simulation.Shutdown();
		return Result;
	};

	TArray<FTierResult> Results;
	Results.Add(RunTier(TEXT("low"), FIntVector(4, 2, 2)));
	Results.Add(RunTier(TEXT("medium"), FIntVector(8, 4, 4)));
	Results.Add(RunTier(TEXT("high"), FIntVector(16, 8, 8)));
	if (!Results[0].bSucceeded || !Results[1].bSucceeded || !Results[2].bSucceeded) return false;

	for (const FTierResult& Result : Results)
	{
		TestTrue(*FString::Printf(TEXT("%s 档支持 GPU timestamp"), *Result.Name), Result.Stats.bGpuTimingSupported);
		TestTrue(*FString::Printf(TEXT("%s 档取得至少 4 个 GPU 时间样本"), *Result.Name), Result.Stats.GpuTimingSamplesCompleted >= 4);
		TestTrue(*FString::Printf(TEXT("%s 档取得稳态 GPU 时间样本"), *Result.Name), Result.Stats.SteadyStateGpuTimingSamples >= 1);
		TestTrue(*FString::Printf(TEXT("%s 档 GPU P95 为正且有限"), *Result.Name),
			FMath::IsFinite(Result.Stats.P95SteadyStateGpuStepMilliseconds)
			&& Result.Stats.P95SteadyStateGpuStepMilliseconds > 0.0);
		TestTrue(*FString::Printf(TEXT("%s 档固定端零漂移"), *Result.Name), Result.Metrics.FixedDriftCm < 1.e-3);
		TestTrue(*FString::Printf(TEXT("%s 档输出残差有限"), *Result.Name), FMath::IsFinite(Result.Metrics.ResidualRms));
		AddInfo(FString::Printf(
			TEXT("GPU XPBD scaling tier=%s cells=%s vertices=%d edges=%d tets=%d steps=%lld iterations=%d timing_samples=%lld timing_skipped=%lld steady_samples=%lld mean_ms=%.9f median_ms=%.9f p95_ms=%.9f p99_ms=%.9f max_ms=%.9f steady_mean_ms=%.9f steady_median_ms=%.9f steady_p95_ms=%.9f steady_p99_ms=%.9f steady_max_ms=%.9f residual=%.9f edge_max=%.9f volume_mean=%.9f fixed_drift_cm=%.9f"),
			*Result.Name,
			Result.Name == TEXT("low") ? TEXT("4x2x2") : (Result.Name == TEXT("medium") ? TEXT("8x4x4") : TEXT("16x8x8")),
			Result.Vertices, Result.Edges, Result.Tetrahedra, Result.Stats.StepsExecuted, IterationCount,
			Result.Stats.GpuTimingSamplesCompleted, Result.Stats.GpuTimingSamplesSkipped,
			Result.Stats.SteadyStateGpuTimingSamples, Result.Stats.MeanGpuStepMilliseconds, Result.Stats.MedianGpuStepMilliseconds,
			Result.Stats.P95GpuStepMilliseconds, Result.Stats.P99GpuStepMilliseconds, Result.Stats.MaximumGpuStepMilliseconds,
			Result.Stats.MeanSteadyStateGpuStepMilliseconds, Result.Stats.MedianSteadyStateGpuStepMilliseconds,
			Result.Stats.P95SteadyStateGpuStepMilliseconds,
			Result.Stats.P99SteadyStateGpuStepMilliseconds, Result.Stats.MaximumSteadyStateGpuStepMilliseconds,
			Result.Metrics.ResidualRms, Result.Metrics.MaximumEdgeRelativeError,
			Result.Metrics.MeanVolumeRelativeError, Result.Metrics.FixedDriftCm));
		TArray<FString> Samples;
		for (int32 Index = 0; Index < Result.TimingMilliseconds.Num(); ++Index)
		{
			Samples.Add(FString::Printf(TEXT("%lld:%.9f"), Result.TimingSteps[Index], Result.TimingMilliseconds[Index]));
		}
		AddInfo(FString::Printf(TEXT("GPU XPBD timing samples tier=%s: %s"), *Result.Name, *FString::Join(Samples, TEXT(","))));
	}
	TestTrue(TEXT("中档稳态中位 GPU 成本高于低档"),
		Results[1].Stats.MedianSteadyStateGpuStepMilliseconds > Results[0].Stats.MedianSteadyStateGpuStepMilliseconds);
	TestTrue(TEXT("高档稳态中位 GPU 成本高于中档"),
		Results[2].Stats.MedianSteadyStateGpuStepMilliseconds > Results[1].Stats.MedianSteadyStateGpuStepMilliseconds);
	TestTrue(TEXT("高档稳态中位 GPU 成本至少高于低档百分之二十"),
		Results[2].Stats.MedianSteadyStateGpuStepMilliseconds > Results[0].Stats.MedianSteadyStateGpuStepMilliseconds * 1.2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoftBodyGpuXpbdLowFrequencyAblationTest,
	"PhysicsResearch.SoftBody.GpuXpbd.LowFrequencyAblation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSoftBodyGpuXpbdLowFrequencyAblationTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过软体 GPU 低频消融；D3D12 报告单独生成。"));
		return true;
	}
	struct FAblationResult
	{
		FGpuOutputMetrics Metrics;
		FSoftBodyGpuXpbdStats Stats;
		bool bSucceeded = false;
	};
	constexpr int32 StepCount = 120;
	constexpr int32 IterationCount = 32;
	auto Run = [this](const FSoftBodyTetMesh& Mesh, const FSoftBodyGpuTopology& Topology, bool bEnable) -> FAblationResult
	{
		FAblationResult Result;
		FSoftBodyGpuXpbdSettings Settings;
		Settings.SolverIterations = IterationCount;
		Settings.ReadbackIntervalSteps = StepCount;
		Settings.GpuTimingWarmupSteps = 16;
		Settings.bEnableLowFrequencyCorrection = bEnable;
		Settings.LowFrequencyCorrectionStrength = 0.35f;
		Settings.LowFrequencyCorrectionInterval = 1;
		FSoftBodyGpuXpbdSimulation Simulation;
		if (!Simulation.Initialize(MakeGpuInput(Mesh, Topology), Settings))
		{
			AddError(TEXT("GPU 低频消融会话初始化失败"));
			return Result;
		}
		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			if (!Simulation.EnqueueStep())
			{
				AddError(TEXT("GPU 低频消融固定步未进入队列"));
				Simulation.Shutdown();
				return Result;
			}
			FlushRenderingCommands();
			FPlatformProcess::SleepNoStats(0.001f);
			Simulation.PollReadback();
			FlushRenderingCommands();
		}
		Simulation.FlushAndWaitForReadbackForValidation();
		TArray<FVector3f> Positions;
		int64 ReadbackStep = 0;
		if (!Simulation.ConsumeLatestPositions(Positions, ReadbackStep) || ReadbackStep != StepCount)
		{
			AddError(TEXT("GPU 低频消融最终快照缺失"));
			Simulation.Shutdown();
			return Result;
		}
		Result.Metrics = MeasureOutput(Mesh, Topology, Positions);
		Result.Stats = Simulation.GetStats();
		Result.bSucceeded = true;
		Simulation.Shutdown();
		return Result;
	};

	struct FTierAblation
	{
		FString Name;
		FAblationResult Local;
		FAblationResult Corrected;
	};
	TArray<FTierAblation> Results;
	for (const TPair<FString, FIntVector>& Tier : TArray<TPair<FString, FIntVector>>{
		{ TEXT("high"), FIntVector(16, 8, 8) }, { TEXT("medium"), FIntVector(8, 4, 4) } })
	{
		FSoftBodyTetGridSettings GridSettings;
		GridSettings.CellCounts = Tier.Value;
		GridSettings.CellSizeCm = FVector3f(40.0f / Tier.Value.X, 20.0f / Tier.Value.Y, 20.0f / Tier.Value.Z);
		FSoftBodyTetMesh Mesh;
		FSoftBodyGpuTopology Topology;
		FString FailureReason;
		if (!FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(GridSettings, Mesh, &FailureReason)
			|| !FSoftBodyGpuTopologyBuilder::Build(Mesh, Topology, &FailureReason))
		{
			AddError(FailureReason);
			return false;
		}
		FTierAblation& Result = Results.AddDefaulted_GetRef();
		Result.Name = Tier.Key;
		Result.Local = Run(Mesh, Topology, false);
		Result.Corrected = Run(Mesh, Topology, true);
	}

	for (const FTierAblation& Result : Results)
	{
		if (!Result.Local.bSucceeded || !Result.Corrected.bSucceeded) return false;
		TestEqual(*FString::Printf(TEXT("%s 局部路径没有低频投影"), *Result.Name), Result.Local.Stats.LowFrequencyProjectionPassesExecuted, int64(0));
		TestEqual(*FString::Printf(TEXT("%s 校正路径投影次数精确"), *Result.Name), Result.Corrected.Stats.LowFrequencyProjectionPassesExecuted, int64(StepCount * IterationCount));
		TestEqual(*FString::Printf(TEXT("%s 校正路径应用次数精确"), *Result.Name), Result.Corrected.Stats.LowFrequencyApplyPassesExecuted, int64(StepCount * IterationCount));
		TestEqual(*FString::Printf(TEXT("%s 校正路径两级归约派发次数精确"), *Result.Name), Result.Corrected.Stats.LowFrequencyReductionDispatchesExecuted, int64(StepCount * IterationCount * 2));
		TestEqual(*FString::Printf(TEXT("%s 校正路径使用两个模态"), *Result.Name), Result.Corrected.Stats.LowFrequencyModeCount, 2);
		TestTrue(*FString::Printf(TEXT("%s 两条路径固定端零漂移"), *Result.Name),
			Result.Local.Metrics.FixedDriftCm < 1.e-3 && Result.Corrected.Metrics.FixedDriftCm < 1.e-3);
		TestTrue(*FString::Printf(TEXT("%s 低频校正降低同预算残差"), *Result.Name),
			Result.Corrected.Metrics.ResidualRms < Result.Local.Metrics.ResidualRms);
		TestTrue(*FString::Printf(TEXT("%s 低频校正降低最大结构边误差"), *Result.Name),
			Result.Corrected.Metrics.MaximumEdgeRelativeError < Result.Local.Metrics.MaximumEdgeRelativeError);
		TestTrue(*FString::Printf(TEXT("%s 低频校正降低平均体积误差"), *Result.Name),
			Result.Corrected.Metrics.MeanVolumeRelativeError < Result.Local.Metrics.MeanVolumeRelativeError);
		TestEqual(*FString::Printf(TEXT("%s 局部路径取得完整 GPU 时间样本"), *Result.Name), Result.Local.Stats.GpuTimingSamplesCompleted, int64(StepCount));
		TestEqual(*FString::Printf(TEXT("%s 校正路径取得完整 GPU 时间样本"), *Result.Name), Result.Corrected.Stats.GpuTimingSamplesCompleted, int64(StepCount));
		TestEqual(*FString::Printf(TEXT("%s 两条路径 GPU 时间均无跳过"), *Result.Name),
			Result.Local.Stats.GpuTimingSamplesSkipped + Result.Corrected.Stats.GpuTimingSamplesSkipped, int64(0));
		AddInfo(FString::Printf(
			TEXT("GPU XPBD low frequency ablation tier=%s steps=%d iterations=%d local_residual=%.9f corrected_residual=%.9f residual_ratio=%.9f local_edge_max=%.9f corrected_edge_max=%.9f local_volume_mean=%.9f corrected_volume_mean=%.9f local_tip_cm=%.9f corrected_tip_cm=%.9f local_fixed_drift_cm=%.9f corrected_fixed_drift_cm=%.9f local_timing_samples=%lld corrected_timing_samples=%lld local_timing_skipped=%lld corrected_timing_skipped=%lld local_steady_median_ms=%.9f corrected_steady_median_ms=%.9f timing_ratio=%.9f local_steady_p95_ms=%.9f corrected_steady_p95_ms=%.9f projection_passes=%lld reduction_dispatches=%lld apply_passes=%lld modes=%d"),
			*Result.Name, StepCount, IterationCount,
			Result.Local.Metrics.ResidualRms, Result.Corrected.Metrics.ResidualRms,
			Result.Corrected.Metrics.ResidualRms / Result.Local.Metrics.ResidualRms,
			Result.Local.Metrics.MaximumEdgeRelativeError, Result.Corrected.Metrics.MaximumEdgeRelativeError,
			Result.Local.Metrics.MeanVolumeRelativeError, Result.Corrected.Metrics.MeanVolumeRelativeError,
			Result.Local.Metrics.TipDisplacementCm, Result.Corrected.Metrics.TipDisplacementCm,
			Result.Local.Metrics.FixedDriftCm, Result.Corrected.Metrics.FixedDriftCm,
			Result.Local.Stats.GpuTimingSamplesCompleted, Result.Corrected.Stats.GpuTimingSamplesCompleted,
			Result.Local.Stats.GpuTimingSamplesSkipped, Result.Corrected.Stats.GpuTimingSamplesSkipped,
			Result.Local.Stats.MedianSteadyStateGpuStepMilliseconds,
			Result.Corrected.Stats.MedianSteadyStateGpuStepMilliseconds,
			Result.Corrected.Stats.MedianSteadyStateGpuStepMilliseconds / Result.Local.Stats.MedianSteadyStateGpuStepMilliseconds,
			Result.Local.Stats.P95SteadyStateGpuStepMilliseconds,
			Result.Corrected.Stats.P95SteadyStateGpuStepMilliseconds,
			Result.Corrected.Stats.LowFrequencyProjectionPassesExecuted,
			Result.Corrected.Stats.LowFrequencyReductionDispatchesExecuted,
			Result.Corrected.Stats.LowFrequencyApplyPassesExecuted,
			Result.Corrected.Stats.LowFrequencyModeCount));
	}
	TestTrue(TEXT("中档低频残差至少降低百分之十五"),
		Results[1].Corrected.Metrics.ResidualRms < Results[1].Local.Metrics.ResidualRms * 0.85);
	TestTrue(TEXT("高档低频残差至少降低百分之二十五"),
		Results[0].Corrected.Metrics.ResidualRms < Results[0].Local.Metrics.ResidualRms * 0.75);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoftBodyGpuXpbdCounterbalancedTimingTest,
	"PhysicsResearch.SoftBody.GpuXpbd.LowFrequencyCounterbalancedTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSoftBodyGpuXpbdCounterbalancedTimingTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过 GPU 低频 ABBA 反平衡计时；真实 D3D12 报告单独生成。"));
		return true;
	}
	struct FTimedRun
	{
		bool bCorrected = false;
		bool bSucceeded = false;
		FSoftBodyGpuXpbdStats Stats;
	};
	constexpr int32 StepCount = 128;
	constexpr int32 IterationCount = 32;
	constexpr int32 WarmupSteps = 48;
	const auto RunSession = [this](const FSoftBodyTetMesh& Mesh, const FSoftBodyGpuTopology& Topology, bool bCorrected)
	{
		FTimedRun Result;
		Result.bCorrected = bCorrected;
		FSoftBodyGpuXpbdSettings Settings;
		Settings.SolverIterations = IterationCount;
		Settings.ReadbackIntervalSteps = StepCount;
		Settings.GpuTimingWarmupSteps = WarmupSteps;
		Settings.bEnableLowFrequencyCorrection = bCorrected;
		Settings.LowFrequencyCorrectionStrength = 0.35f;
		Settings.LowFrequencyCorrectionInterval = 1;
		FSoftBodyGpuXpbdSimulation Simulation;
		if (!Simulation.Initialize(MakeGpuInput(Mesh, Topology), Settings))
		{
			AddError(TEXT("ABBA 计时会话初始化失败"));
			return Result;
		}
		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			if (!Simulation.EnqueueStep())
			{
				AddError(TEXT("ABBA 计时固定步未进入 GPU 队列"));
				Simulation.Shutdown();
				return Result;
			}
			FlushRenderingCommands();
			FPlatformProcess::SleepNoStats(0.001f);
			Simulation.PollReadback();
			FlushRenderingCommands();
		}
		Simulation.FlushAndWaitForReadbackForValidation();
		TArray<FVector3f> Positions;
		int64 ReadbackStep = 0;
		if (!Simulation.ConsumeLatestPositions(Positions, ReadbackStep) || ReadbackStep != StepCount)
		{
			AddError(TEXT("ABBA 计时最终快照缺失"));
			Simulation.Shutdown();
			return Result;
		}
		Result.Stats = Simulation.GetStats();
		Result.bSucceeded = true;
		Simulation.Shutdown();
		return Result;
	};

	for (const TPair<FString, FIntVector>& Tier : TArray<TPair<FString, FIntVector>>{
		{ TEXT("medium"), FIntVector(8, 4, 4) }, { TEXT("high"), FIntVector(16, 8, 8) } })
	{
		FSoftBodyTetGridSettings GridSettings;
		GridSettings.CellCounts = Tier.Value;
		GridSettings.CellSizeCm = FVector3f(40.0f / Tier.Value.X, 20.0f / Tier.Value.Y, 20.0f / Tier.Value.Z);
		FSoftBodyTetMesh Mesh;
		FSoftBodyGpuTopology Topology;
		FString FailureReason;
		if (!FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(GridSettings, Mesh, &FailureReason)
			|| !FSoftBodyGpuTopologyBuilder::Build(Mesh, Topology, &FailureReason))
		{
			AddError(FailureReason);
			return false;
		}
		TArray<FTimedRun> Runs;
		for (const bool bCorrected : { false, true, true, false })
		{
			Runs.Add(RunSession(Mesh, Topology, bCorrected));
		}
		if (Runs.ContainsByPredicate([](const FTimedRun& Run) { return !Run.bSucceeded; })) return false;
		TArray<double> LocalMedians;
		TArray<double> CorrectedMedians;
		TArray<double> LocalP95s;
		TArray<double> CorrectedP95s;
		for (const FTimedRun& Run : Runs)
		{
			TestEqual(*FString::Printf(TEXT("%s ABBA 每个会话取得完整样本"), *Tier.Key), Run.Stats.GpuTimingSamplesCompleted, int64(StepCount));
			TestEqual(*FString::Printf(TEXT("%s ABBA 每个会话无跳过"), *Tier.Key), Run.Stats.GpuTimingSamplesSkipped, int64(0));
			TestEqual(*FString::Printf(TEXT("%s ABBA 每个会话取得预期稳态样本"), *Tier.Key), Run.Stats.SteadyStateGpuTimingSamples, int64(StepCount - WarmupSteps));
			TestTrue(*FString::Printf(TEXT("%s ABBA 中位与 P95 有限为正"), *Tier.Key),
				FMath::IsFinite(Run.Stats.MedianSteadyStateGpuStepMilliseconds)
				&& Run.Stats.MedianSteadyStateGpuStepMilliseconds > 0.0
				&& FMath::IsFinite(Run.Stats.P95SteadyStateGpuStepMilliseconds)
				&& Run.Stats.P95SteadyStateGpuStepMilliseconds > 0.0);
			if (Run.bCorrected)
			{
				TestEqual(*FString::Printf(TEXT("%s ABBA 校正会话投影计数"), *Tier.Key), Run.Stats.LowFrequencyProjectionPassesExecuted, int64(StepCount * IterationCount));
				TestEqual(*FString::Printf(TEXT("%s ABBA 校正会话归约计数"), *Tier.Key), Run.Stats.LowFrequencyReductionDispatchesExecuted, int64(StepCount * IterationCount * 2));
				TestEqual(*FString::Printf(TEXT("%s ABBA 校正会话应用计数"), *Tier.Key), Run.Stats.LowFrequencyApplyPassesExecuted, int64(StepCount * IterationCount));
			}
			else
			{
				TestEqual(*FString::Printf(TEXT("%s ABBA 局部会话无低频 Pass"), *Tier.Key),
					Run.Stats.LowFrequencyProjectionPassesExecuted + Run.Stats.LowFrequencyReductionDispatchesExecuted + Run.Stats.LowFrequencyApplyPassesExecuted, int64(0));
			}
			(Run.bCorrected ? CorrectedMedians : LocalMedians).Add(Run.Stats.MedianSteadyStateGpuStepMilliseconds);
			(Run.bCorrected ? CorrectedP95s : LocalP95s).Add(Run.Stats.P95SteadyStateGpuStepMilliseconds);
		}
		const double LocalMedianMean = (LocalMedians[0] + LocalMedians[1]) * 0.5;
		const double CorrectedMedianMean = (CorrectedMedians[0] + CorrectedMedians[1]) * 0.5;
		const double LocalP95Mean = (LocalP95s[0] + LocalP95s[1]) * 0.5;
		const double CorrectedP95Mean = (CorrectedP95s[0] + CorrectedP95s[1]) * 0.5;
		const double Ratio = CorrectedMedianMean / LocalMedianMean;
		TestTrue(*FString::Printf(TEXT("%s ABBA 反平衡比值有限为正"), *Tier.Key), FMath::IsFinite(Ratio) && Ratio > 0.0);
		AddInfo(FString::Printf(
			TEXT("GPU XPBD counterbalanced timing tier=%s protocol=ABBA steps_per_run=%d warmup_per_run=%d iterations=%d local_run_medians_ms=%.9f,%.9f corrected_run_medians_ms=%.9f,%.9f local_mean_of_medians_ms=%.9f corrected_mean_of_medians_ms=%.9f corrected_to_local_ratio=%.9f local_mean_p95_ms=%.9f corrected_mean_p95_ms=%.9f samples_per_run=%d steady_samples_per_run=%d skipped_total=%lld corrected_projection_passes_per_run=%lld corrected_reduction_dispatches_per_run=%lld corrected_apply_passes_per_run=%lld"),
			*Tier.Key, StepCount, WarmupSteps, IterationCount,
			LocalMedians[0], LocalMedians[1], CorrectedMedians[0], CorrectedMedians[1],
			LocalMedianMean, CorrectedMedianMean, Ratio, LocalP95Mean, CorrectedP95Mean,
			StepCount, StepCount - WarmupSteps,
			Runs[0].Stats.GpuTimingSamplesSkipped + Runs[1].Stats.GpuTimingSamplesSkipped
				+ Runs[2].Stats.GpuTimingSamplesSkipped + Runs[3].Stats.GpuTimingSamplesSkipped,
			Runs[1].Stats.LowFrequencyProjectionPassesExecuted,
			Runs[1].Stats.LowFrequencyReductionDispatchesExecuted,
			Runs[1].Stats.LowFrequencyApplyPassesExecuted));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoftBodyGpuGroundContactAblationTest,
	"PhysicsResearch.SoftBody.GpuXpbd.GroundContactAblation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSoftBodyGpuGroundContactAblationTest::RunTest(const FString& Parameters)
{
	FSoftBodyTetGridSettings CpuGrid;
	CpuGrid.CellCounts = FIntVector(4, 2, 2);
	CpuGrid.CellSizeCm = FVector3f(10.0f);
	CpuGrid.bFixMinimumXFace = true;
	FSoftBodyTetMesh CpuMesh;
	FString CpuFailureReason;
	if (!FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(CpuGrid, CpuMesh, &CpuFailureReason))
	{
		AddError(CpuFailureReason);
		return false;
	}
	const auto RunCpu = [&](double ContactStiffness)
	{
		FSoftBodyLocalXpbdSettings Settings;
		Settings.SolverIterations = 32;
		Settings.bEnableLowFrequencyCorrection = true;
		Settings.bEnableGroundContact = true;
		Settings.GroundPlaneZCm = -5.0;
		Settings.GroundContactStiffness = ContactStiffness;
		FSoftBodyLocalXpbdSolver Solver;
		FSoftBodyLocalXpbdStats Stats;
		if (!Solver.Initialize(CpuMesh, Settings, &CpuFailureReason)) return Stats;
		for (int32 Step = 0; Step < 120; ++Step)
		{
			if (!Solver.Step(Stats, &CpuFailureReason)) return FSoftBodyLocalXpbdStats();
		}
		return Stats;
	};
	const FSoftBodyLocalXpbdStats CpuStandard = RunCpu(1.0);
	const FSoftBodyLocalXpbdStats CpuWeak = RunCpu(0.02);
	TestEqual(TEXT("CPU 标准接触完成 120 步"), CpuStandard.StepsExecuted, int64(120));
	TestEqual(TEXT("CPU 弱接触完成 120 步"), CpuWeak.StepsExecuted, int64(120));
	TestEqual(TEXT("CPU 标准接触最后一步执行一个 Pass"), CpuStandard.GroundContactPassesExecuted, 1);
	TestTrue(TEXT("CPU 标准接触无可测穿透"), CpuStandard.MaximumGroundPenetrationCm < 1.e-6);
	TestTrue(TEXT("CPU 弱接触产生明显穿透"), CpuWeak.MaximumGroundPenetrationCm > 0.5);

	if (GUsingNullRHI)
	{
		AddInfo(FString::Printf(TEXT("CPU 接触参考已通过：standard=%.9fcm weak=%.9fcm；NullRHI 下跳过真实 GPU 消融。"),
			CpuStandard.MaximumGroundPenetrationCm, CpuWeak.MaximumGroundPenetrationCm));
		return true;
	}
	FSoftBodyTetGridSettings GridSettings;
	GridSettings.CellCounts = FIntVector(4, 2, 2);
	GridSettings.CellSizeCm = FVector3f(10.0f);
	FSoftBodyTetMesh Mesh;
	FSoftBodyGpuTopology Topology;
	FString FailureReason;
	if (!FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(GridSettings, Mesh, &FailureReason)
		|| !FSoftBodyGpuTopologyBuilder::Build(Mesh, Topology, &FailureReason))
	{
		AddError(FailureReason);
		return false;
	}
	struct FContactResult
	{
		bool bSucceeded = false;
		double MaximumPenetrationCm = 0.0;
		double FixedDriftCm = 0.0;
		int32 ActiveContactVertices = 0;
		FSoftBodyGpuXpbdStats Stats;
	};
	constexpr int32 StepCount = 120;
	constexpr float GroundZ = -5.0f;
	const auto Run = [&](float ContactStiffness)
	{
		FContactResult Result;
		FSoftBodyGpuXpbdSettings Settings;
		Settings.SolverIterations = 32;
		Settings.ReadbackIntervalSteps = StepCount;
		Settings.bEnableLowFrequencyCorrection = true;
		Settings.bEnableGroundContact = true;
		Settings.GroundPlaneZCm = GroundZ;
		Settings.GroundContactStiffness = ContactStiffness;
		FSoftBodyGpuXpbdSimulation Simulation;
		if (!Simulation.Initialize(MakeGpuInput(Mesh, Topology), Settings)) return Result;
		for (int32 Step = 0; Step < StepCount; ++Step)
		{
			if (!Simulation.EnqueueStep()) return Result;
		}
		Simulation.FlushAndWaitForReadbackForValidation();
		TArray<FVector3f> Positions;
		int64 ReadbackStep = 0;
		if (!Simulation.ConsumeLatestPositions(Positions, ReadbackStep) || ReadbackStep != StepCount) return Result;
		Result.Stats = Simulation.GetStats();
		for (int32 Vertex = 0; Vertex < Positions.Num(); ++Vertex)
		{
			if (Mesh.InverseMasses[Vertex] <= 0.0f)
			{
				Result.FixedDriftCm = FMath::Max(Result.FixedDriftCm,
					static_cast<double>((Positions[Vertex] - Mesh.RestPositions[Vertex]).Length()));
				continue;
			}
			Result.MaximumPenetrationCm = FMath::Max(Result.MaximumPenetrationCm,
				static_cast<double>(GroundZ - Positions[Vertex].Z));
			if (FMath::Abs(Positions[Vertex].Z - GroundZ) < 0.01f) ++Result.ActiveContactVertices;
		}
		Result.MaximumPenetrationCm = FMath::Max(0.0, Result.MaximumPenetrationCm);
		Result.bSucceeded = true;
		Simulation.Shutdown();
		return Result;
	};
	const FContactResult Standard = Run(1.0f);
	const FContactResult Weak = Run(0.02f);
	TestTrue(TEXT("标准与弱接触路径均完成"), Standard.bSucceeded && Weak.bSucceeded);
	if (!Standard.bSucceeded || !Weak.bSucceeded) return false;
	TestEqual(TEXT("标准接触每固定步一个 GPU Pass"), Standard.Stats.GroundContactPassesExecuted, int64(StepCount));
	TestEqual(TEXT("弱接触每固定步一个 GPU Pass"), Weak.Stats.GroundContactPassesExecuted, int64(StepCount));
	TestTrue(TEXT("标准接触产生实际接触顶点"), Standard.ActiveContactVertices > 0);
	TestTrue(TEXT("标准接触无可测穿透"), Standard.MaximumPenetrationCm < 1.e-3);
	TestTrue(TEXT("弱接触单参数消融产生明显穿透"), Weak.MaximumPenetrationCm > 0.5);
	TestTrue(TEXT("两条路径固定端零漂移"), Standard.FixedDriftCm < 1.e-3 && Weak.FixedDriftCm < 1.e-3);
	AddInfo(FString::Printf(TEXT("GPU XPBD ground contact ablation steps=%d standard_stiffness=1 weak_stiffness=0.02 standard_max_penetration_cm=%.9f weak_max_penetration_cm=%.9f standard_active_contact_vertices=%d standard_fixed_drift_cm=%.9f weak_fixed_drift_cm=%.9f standard_contact_passes=%lld weak_contact_passes=%lld"),
		StepCount, Standard.MaximumPenetrationCm, Weak.MaximumPenetrationCm, Standard.ActiveContactVertices,
		Standard.FixedDriftCm, Weak.FixedDriftCm, Standard.Stats.GroundContactPassesExecuted, Weak.Stats.GroundContactPassesExecuted));
	return true;
}

#endif
