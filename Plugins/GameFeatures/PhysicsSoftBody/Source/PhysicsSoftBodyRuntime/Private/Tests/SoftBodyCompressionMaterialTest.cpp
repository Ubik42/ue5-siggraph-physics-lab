#if WITH_DEV_AUTOMATION_TESTS

#include "SoftBodyLocalXpbdSolver.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoftBodyCompressionMaterialTest,
	"PhysicsResearch.SoftBody.LocalXpbd.CompressionMaterials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSoftBodyCompressionMaterialTest::RunTest(const FString& Parameters)
{
	struct FProfileResult
	{
		FSoftBodyLocalXpbdStats Stats;
		int32 KinematicVertexCount = 0;
	};
	const auto RunProfile = [this](bool bSplitMaterial, uint8 UniformMaterialId, FProfileResult& OutResult)
	{
		FSoftBodyTetGridSettings GridSettings;
		GridSettings.CellCounts = FIntVector(4, 2, 2);
		GridSettings.CellSizeCm = FVector3f(10.0f);
		GridSettings.bSplitMaterialAtHalfX = bSplitMaterial;
		GridSettings.UniformMaterialId = UniformMaterialId;
		FSoftBodyTetMesh Mesh;
		FString FailureReason;
		if (!FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(GridSettings, Mesh, &FailureReason))
		{
			AddError(FailureReason);
			return false;
		}
		FSoftBodyLocalXpbdSettings SolverSettings;
		SolverSettings.GravityCmPerSecondSquared = FVector3d::ZeroVector;
		SolverSettings.VelocityDamping = 0.0;
		SolverSettings.SolverIterations = 64;
		SolverSettings.SoftEdgeCompliance = 2.e-3;
		SolverSettings.StiffEdgeCompliance = 2.e-5;
		SolverSettings.SoftVolumeCompliance = 20.0;
		SolverSettings.StiffVolumeCompliance = 0.2;
		FSoftBodyLocalXpbdSolver Solver;
		if (!Solver.Initialize(Mesh, SolverSettings, &FailureReason))
		{
			AddError(FailureReason);
			return false;
		}
		double MaximumX = -TNumericLimits<double>::Max();
		for (const FVector3f& Position : Mesh.RestPositions)
		{
			MaximumX = FMath::Max(MaximumX, static_cast<double>(Position.X));
		}
		for (int32 Vertex = 0; Vertex < Mesh.RestPositions.Num(); ++Vertex)
		{
			const FVector3d RestPosition(Mesh.RestPositions[Vertex]);
			if (FMath::IsNearlyEqual(RestPosition.X, MaximumX))
			{
				FVector3d Target = RestPosition;
				Target.X -= 8.0;
				if (!Solver.SetKinematicTarget(Vertex, Target, &FailureReason))
				{
					AddError(FailureReason);
					return false;
				}
				++OutResult.KinematicVertexCount;
			}
		}
		if (!Solver.Step(OutResult.Stats, &FailureReason))
		{
			AddError(FailureReason);
			return false;
		}
		return true;
	};

	FProfileResult Soft;
	FProfileResult Stiff;
	FProfileResult Split;
	TestTrue(TEXT("均匀软材料压缩运行"), RunProfile(false, 0, Soft));
	TestTrue(TEXT("均匀硬材料压缩运行"), RunProfile(false, 1, Stiff));
	TestTrue(TEXT("软硬分区材料压缩运行"), RunProfile(true, 0, Split));
	AddInfo(FString::Printf(
		TEXT("Local XPBD compression materials: compression_cm=8.000000 kinematic_vertices=%d soft_volume_mean=%.9f soft_volume_ratio=%.9f soft_residual=%.9f stiff_volume_mean=%.9f stiff_volume_ratio=%.9f stiff_residual=%.9f split_soft_volume_mean=%.9f split_stiff_volume_mean=%.9f split_volume_ratio=%.9f split_residual=%.9f soft_target_error_cm=%.12f stiff_target_error_cm=%.12f split_target_error_cm=%.12f"),
		Soft.KinematicVertexCount, Soft.Stats.MeanVolumeRelativeError, Soft.Stats.TotalVolumeRatio, Soft.Stats.ResidualRms,
		Stiff.Stats.MeanVolumeRelativeError, Stiff.Stats.TotalVolumeRatio, Stiff.Stats.ResidualRms,
		Split.Stats.MeanSoftMaterialVolumeRelativeError, Split.Stats.MeanStiffMaterialVolumeRelativeError,
		Split.Stats.TotalVolumeRatio, Split.Stats.ResidualRms, Soft.Stats.MaximumKinematicTargetErrorCm,
		Stiff.Stats.MaximumKinematicTargetErrorCm, Split.Stats.MaximumKinematicTargetErrorCm));
	TestEqual(TEXT("最大 X 面包含 9 个运动学顶点"), Soft.KinematicVertexCount, 9);
	TestEqual(TEXT("三种材料使用相同运动学顶点数"), Stiff.KinematicVertexCount, Soft.KinematicVertexCount);
	TestEqual(TEXT("分区材料使用相同运动学顶点数"), Split.KinematicVertexCount, Soft.KinematicVertexCount);
	TestTrue(TEXT("全部运动学压缩目标精确满足"), Soft.Stats.MaximumKinematicTargetErrorCm < 1.e-9
		&& Stiff.Stats.MaximumKinematicTargetErrorCm < 1.e-9 && Split.Stats.MaximumKinematicTargetErrorCm < 1.e-9);
	TestTrue(TEXT("硬材料比软材料更好保持体积"), Stiff.Stats.MeanVolumeRelativeError < Soft.Stats.MeanVolumeRelativeError);
	TestTrue(TEXT("分区材料总体积响应位于均匀软硬材料之间"),
		Split.Stats.TotalVolumeRatio > FMath::Min(Soft.Stats.TotalVolumeRatio, Stiff.Stats.TotalVolumeRatio)
		&& Split.Stats.TotalVolumeRatio < FMath::Max(Soft.Stats.TotalVolumeRatio, Stiff.Stats.TotalVolumeRatio));
	TestTrue(TEXT("三种压缩结果保持正总体积"), Soft.Stats.TotalVolumeRatio > 0.5
		&& Stiff.Stats.TotalVolumeRatio > 0.5 && Split.Stats.TotalVolumeRatio > 0.5);
	return true;
}

#endif
