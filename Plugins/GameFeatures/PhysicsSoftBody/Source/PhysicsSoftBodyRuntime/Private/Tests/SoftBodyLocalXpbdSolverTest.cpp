#if WITH_DEV_AUTOMATION_TESTS

#include "SoftBodyLocalXpbdSolver.h"

#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoftBodyLocalXpbdCantileverTest,
	"PhysicsResearch.SoftBody.LocalXpbd.Cantilever",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSoftBodyLocalXpbdCantileverTest::RunTest(const FString& Parameters)
{
	FSoftBodyTetGridSettings GridSettings;
	GridSettings.CellCounts = FIntVector(4, 2, 2);
	GridSettings.CellSizeCm = FVector3f(10.0f);
	FSoftBodyTetMesh Mesh;
	FString FailureReason;
	TestTrue(TEXT("生成低分辨率悬臂梁"), FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(GridSettings, Mesh, &FailureReason));

	FSoftBodyLocalXpbdSettings OneIterationSettings;
	OneIterationSettings.SolverIterations = 1;
	FSoftBodyLocalXpbdSolver OneIterationSolver;
	TestTrue(TEXT("初始化单迭代局部 XPBD"), OneIterationSolver.Initialize(Mesh, OneIterationSettings, &FailureReason));
	FSoftBodyLocalXpbdStats OneIterationStats;
	TestTrue(TEXT("单迭代固定步成功"), OneIterationSolver.Step(OneIterationStats, &FailureReason));

	FSoftBodyLocalXpbdSettings FullSettings;
	FullSettings.SolverIterations = 64;
	FSoftBodyLocalXpbdSolver Solver;
	TestTrue(TEXT("初始化 64 迭代局部 XPBD"), Solver.Initialize(Mesh, FullSettings, &FailureReason));
	FSoftBodyLocalXpbdStats FirstStepStats;
	const double FirstStepStartSeconds = FPlatformTime::Seconds();
	TestTrue(TEXT("64 迭代固定步成功"), Solver.Step(FirstStepStats, &FailureReason));
	double TotalStepMilliseconds = (FPlatformTime::Seconds() - FirstStepStartSeconds) * 1000.0;
	double MaximumStepMilliseconds = TotalStepMilliseconds;
	TestTrue(TEXT("同一预测状态增加迭代降低残差"), FirstStepStats.ResidualRms < OneIterationStats.ResidualRms);
	TestEqual(TEXT("逐迭代残差曲线完整"), FirstStepStats.IterationResidualRms.Num(), 64);
	TestTrue(TEXT("固定面严格不漂移"), FirstStepStats.FixedVertexDriftCm < 1.e-9);

	FSoftBodyLocalXpbdStats FinalStats = FirstStepStats;
	for (int32 Step = 1; Step < 120; ++Step)
	{
		const double StepStartSeconds = FPlatformTime::Seconds();
		if (!Solver.Step(FinalStats, &FailureReason))
		{
			AddError(FailureReason);
			return false;
		}
		const double StepMilliseconds = (FPlatformTime::Seconds() - StepStartSeconds) * 1000.0;
		TotalStepMilliseconds += StepMilliseconds;
		MaximumStepMilliseconds = FMath::Max(MaximumStepMilliseconds, StepMilliseconds);
	}
	const double MeanStepMilliseconds = TotalStepMilliseconds / 120.0;
	AddInfo(FString::Printf(
		TEXT("Local XPBD cantilever: steps=%lld iterations=%d vertices=%d tets=%d distance_constraints=%d volume_constraints=%d residual_rms=%.9f edge_relative_max=%.9f volume_relative_mean=%.9f volume_relative_max=%.9f total_volume_ratio=%.9f tip_displacement_cm=%.6f fixed_drift_cm=%.12f first_step_one_iter_residual=%.9f first_step_full_iter_residual=%.9f curve_r1=%.9f curve_r8=%.9f curve_r16=%.9f curve_r32=%.9f curve_r64=%.9f mean_step_ms=%.6f maximum_step_ms=%.6f"),
		FinalStats.StepsExecuted, FinalStats.IterationsExecuted, Mesh.RestPositions.Num(), Mesh.Tetrahedra.Num(),
		FinalStats.DistanceConstraintCount, FinalStats.VolumeConstraintCount, FinalStats.ResidualRms,
		FinalStats.MaximumEdgeRelativeError, FinalStats.MeanVolumeRelativeError, FinalStats.MaximumVolumeRelativeError,
		FinalStats.TotalVolumeRatio, FinalStats.TipDisplacementCm, FinalStats.FixedVertexDriftCm,
		OneIterationStats.ResidualRms, FirstStepStats.ResidualRms,
		FirstStepStats.IterationResidualRms[0], FirstStepStats.IterationResidualRms[7],
		FirstStepStats.IterationResidualRms[15], FirstStepStats.IterationResidualRms[31],
		FirstStepStats.IterationResidualRms[63], MeanStepMilliseconds, MaximumStepMilliseconds));
	TArray<FString> ResidualCurveValues;
	ResidualCurveValues.Reserve(FirstStepStats.IterationResidualRms.Num());
	for (double Residual : FirstStepStats.IterationResidualRms)
	{
		ResidualCurveValues.Add(FString::Printf(TEXT("%.12f"), Residual));
	}
	AddInfo(FString::Printf(TEXT("Local XPBD convergence curve: %s"), *FString::Join(ResidualCurveValues, TEXT(","))));
	TestEqual(TEXT("执行 120 个固定步"), FinalStats.StepsExecuted, static_cast<int64>(120));
	TestEqual(TEXT("体积约束覆盖全部四面体"), FinalStats.VolumeConstraintCount, Mesh.Tetrahedra.Num());
	TestTrue(TEXT("悬臂自由端产生有限向下位移"), FinalStats.TipDisplacementCm > 1.0 && FinalStats.TipDisplacementCm < 200.0);
	TestTrue(TEXT("最终残差保持有界"), FinalStats.ResidualRms < 0.15);
	TestTrue(TEXT("最大结构边相对误差保持有界"), FinalStats.MaximumEdgeRelativeError < 0.25);
	TestTrue(TEXT("平均体积相对误差保持有界"), FinalStats.MeanVolumeRelativeError < 0.10);
	TestTrue(TEXT("总体积比保持正且有限"), FinalStats.TotalVolumeRatio > 0.75 && FinalStats.TotalVolumeRatio < 1.25);
	TestTrue(TEXT("固定面全程保持零漂移"), FinalStats.FixedVertexDriftCm < 1.e-9);
	TestTrue(TEXT("低分辨率 CPU 固定步成本有限"), MeanStepMilliseconds > 0.0 && MeanStepMilliseconds < 20.0);

	FSoftBodyLocalXpbdSettings InvalidSettings = FullSettings;
	InvalidSettings.FixedDeltaSeconds = 0.0;
	FSoftBodyLocalXpbdSolver InvalidSolver;
	FailureReason.Reset();
	TestFalse(TEXT("零时间步初始化被拒绝"), InvalidSolver.Initialize(Mesh, InvalidSettings, &FailureReason));
	TestTrue(TEXT("非法设置提供明确错误"), !FailureReason.IsEmpty());
	return true;
}

#endif
