#if WITH_DEV_AUTOMATION_TESTS

#include "SoftBodyLocalXpbdSolver.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoftBodyLowFrequencyCantileverAblationTest,
	"PhysicsResearch.SoftBody.LowFrequency.CantileverAblation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSoftBodyLowFrequencyCantileverAblationTest::RunTest(const FString& Parameters)
{
	FSoftBodyTetGridSettings GridSettings;
	GridSettings.CellCounts = FIntVector(4, 2, 2);
	GridSettings.CellSizeCm = FVector3f(10.0f);
	FSoftBodyTetMesh Mesh;
	FString FailureReason;
	if (!FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(GridSettings, Mesh, &FailureReason))
	{
		AddError(FailureReason);
		return false;
	}

	FSoftBodyLocalXpbdSettings LocalSettings;
	LocalSettings.SolverIterations = 32;
	FSoftBodyLocalXpbdSettings GlobalSettings = LocalSettings;
	GlobalSettings.bEnableLowFrequencyCorrection = true;
	GlobalSettings.LowFrequencyCorrectionStrength = 0.35;
	GlobalSettings.LowFrequencyCorrectionInterval = 1;

	FSoftBodyLocalXpbdSolver LocalSolver;
	FSoftBodyLocalXpbdSolver GlobalSolver;
	if (!LocalSolver.Initialize(Mesh, LocalSettings, &FailureReason)
		|| !GlobalSolver.Initialize(Mesh, GlobalSettings, &FailureReason))
	{
		AddError(FailureReason);
		return false;
	}

	FSoftBodyLocalXpbdStats LocalFirstStep;
	FSoftBodyLocalXpbdStats GlobalFirstStep;
	if (!LocalSolver.Step(LocalFirstStep, &FailureReason)
		|| !GlobalSolver.Step(GlobalFirstStep, &FailureReason))
	{
		AddError(FailureReason);
		return false;
	}
	FSoftBodyLocalXpbdStats LocalFinal = LocalFirstStep;
	FSoftBodyLocalXpbdStats GlobalFinal = GlobalFirstStep;
	for (int32 Step = 1; Step < 120; ++Step)
	{
		if (!LocalSolver.Step(LocalFinal, &FailureReason) || !GlobalSolver.Step(GlobalFinal, &FailureReason))
		{
			AddError(FailureReason);
			return false;
		}
	}

	AddInfo(FString::Printf(
		TEXT("Low-frequency cantilever ablation: steps=120 iterations=32 strength=%.6f interval=%d modes=%d passes_last_step=%d max_global_correction_cm=%.9f local_first_residual=%.9f global_first_residual=%.9f local_final_residual=%.9f global_final_residual=%.9f residual_ratio=%.9f local_edge_max=%.9f global_edge_max=%.9f local_volume_mean=%.9f global_volume_mean=%.9f local_tip_cm=%.6f global_tip_cm=%.6f local_fixed_drift=%.12f global_fixed_drift=%.12f"),
		GlobalSettings.LowFrequencyCorrectionStrength, GlobalSettings.LowFrequencyCorrectionInterval,
		GlobalFinal.LowFrequencyModeCount, GlobalFinal.LowFrequencyCorrectionPasses,
		GlobalFinal.MaximumLowFrequencyCorrectionCm, LocalFirstStep.ResidualRms, GlobalFirstStep.ResidualRms,
		LocalFinal.ResidualRms, GlobalFinal.ResidualRms, GlobalFinal.ResidualRms / LocalFinal.ResidualRms,
		LocalFinal.MaximumEdgeRelativeError, GlobalFinal.MaximumEdgeRelativeError,
		LocalFinal.MeanVolumeRelativeError, GlobalFinal.MeanVolumeRelativeError,
		LocalFinal.TipDisplacementCm, GlobalFinal.TipDisplacementCm,
		LocalFinal.FixedVertexDriftCm, GlobalFinal.FixedVertexDriftCm));

	TestEqual(TEXT("局部与全局路径使用相同固定步"), GlobalFinal.StepsExecuted, LocalFinal.StepsExecuted);
	TestEqual(TEXT("两条路径使用相同迭代预算"), GlobalFinal.IterationsExecuted, LocalFinal.IterationsExecuted);
	TestEqual(TEXT("工程子集明确使用两个低频模态"), GlobalFinal.LowFrequencyModeCount, 2);
	TestEqual(TEXT("每次迭代执行一次全局投影"), GlobalFinal.LowFrequencyCorrectionPasses, 32);
	TestEqual(TEXT("默认局部路径不执行全局投影"), LocalFinal.LowFrequencyCorrectionPasses, 0);
	TestTrue(TEXT("全局校正在同预算下明显降低最终残差"), GlobalFinal.ResidualRms < LocalFinal.ResidualRms * 0.95);
	TestTrue(TEXT("全局校正保持平均体积误差有界"), GlobalFinal.MeanVolumeRelativeError < 0.15);
	TestTrue(TEXT("全局校正保持结构边误差有界"), GlobalFinal.MaximumEdgeRelativeError < 0.35);
	TestTrue(TEXT("固定端在两条路径中均严格不漂移"),
		LocalFinal.FixedVertexDriftCm < 1.e-9 && GlobalFinal.FixedVertexDriftCm < 1.e-9);
	return true;
}

#endif
