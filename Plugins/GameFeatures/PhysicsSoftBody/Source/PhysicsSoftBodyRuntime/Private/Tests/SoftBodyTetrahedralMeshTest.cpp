#if WITH_DEV_AUTOMATION_TESTS

#include "SoftBodyTetrahedralMesh.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoftBodyTetrahedralGridContractTest,
	"PhysicsResearch.SoftBody.TetrahedralGrid.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSoftBodyTetrahedralGridContractTest::RunTest(const FString& Parameters)
{
	struct FTier
	{
		const TCHAR* Name;
		FIntVector Cells;
		int32 ExpectedVertices;
		int32 ExpectedTetrahedra;
		int32 ExpectedSurfaceTriangles;
	};
	const FTier Tiers[] = {
		{ TEXT("低"), FIntVector(4, 2, 2), 45, 96, 80 },
		{ TEXT("中"), FIntVector(8, 4, 4), 225, 768, 320 },
		{ TEXT("高"), FIntVector(16, 8, 8), 1377, 6144, 1280 }
	};
	for (const FTier& Tier : Tiers)
	{
		FSoftBodyTetGridSettings Settings;
		Settings.CellCounts = Tier.Cells;
		Settings.CellSizeCm = FVector3f(10.0f, 10.0f, 10.0f);
		FSoftBodyTetMesh Mesh;
		FString FailureReason;
		TestTrue(FString::Printf(TEXT("%s分辨率四面体网格生成"), Tier.Name),
			FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(Settings, Mesh, &FailureReason));
		if (!FailureReason.IsEmpty()) AddError(FailureReason);
		TestTrue(FString::Printf(TEXT("%s分辨率合同有效"), Tier.Name), Mesh.IsValid(&FailureReason));
		TestEqual(FString::Printf(TEXT("%s分辨率顶点数"), Tier.Name), Mesh.RestPositions.Num(), Tier.ExpectedVertices);
		TestEqual(FString::Printf(TEXT("%s分辨率四面体数"), Tier.Name), Mesh.Tetrahedra.Num(), Tier.ExpectedTetrahedra);
		TestEqual(FString::Printf(TEXT("%s分辨率边界三角形数"), Tier.Name), Mesh.SurfaceTriangles.Num(), Tier.ExpectedSurfaceTriangles);
		TestEqual(FString::Printf(TEXT("%s分辨率固定面顶点数"), Tier.Name), Mesh.CountFixedVertices(),
			(Tier.Cells.Y + 1) * (Tier.Cells.Z + 1));
		const double ExpectedVolume = static_cast<double>(Tier.Cells.X * Tier.Cells.Y * Tier.Cells.Z) * 1000.0;
		TestTrue(FString::Printf(TEXT("%s分辨率总体积守恒"), Tier.Name),
			FMath::IsNearlyEqual(Mesh.CalculateTotalRestVolumeCm3(), ExpectedVolume, ExpectedVolume * 1.e-6));
		TestTrue(FString::Printf(TEXT("%s分辨率双材料均存在"), Tier.Name),
			Mesh.MaterialIds.Contains(0) && Mesh.MaterialIds.Contains(1));
		float MinimumVolume = TNumericLimits<float>::Max();
		float MaximumVolume = 0.0f;
		for (float Volume : Mesh.RestVolumesCm3)
		{
			MinimumVolume = FMath::Min(MinimumVolume, Volume);
			MaximumVolume = FMath::Max(MaximumVolume, Volume);
		}
		TestTrue(FString::Printf(TEXT("%s分辨率单元体积一致且为正"), Tier.Name),
			MinimumVolume > 0.0f && FMath::IsNearlyEqual(MinimumVolume, MaximumVolume, 1.e-5f));
		AddInfo(FString::Printf(
			TEXT("Soft-body tet tier=%s cells=%dx%dx%d vertices=%d tets=%d surface_triangles=%d fixed=%d total_volume_cm3=%.6f tet_volume_cm3=%.6f materials=2"),
			Tier.Name, Tier.Cells.X, Tier.Cells.Y, Tier.Cells.Z, Mesh.RestPositions.Num(), Mesh.Tetrahedra.Num(),
			Mesh.SurfaceTriangles.Num(), Mesh.CountFixedVertices(), Mesh.CalculateTotalRestVolumeCm3(), MinimumVolume));
	}

	FSoftBodyTetGridSettings InvalidSettings;
	InvalidSettings.CellCounts.X = 0;
	FSoftBodyTetMesh InvalidMesh;
	FString FailureReason;
	TestFalse(TEXT("零单元维度被拒绝"),
		FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(InvalidSettings, InvalidMesh, &FailureReason));
	TestTrue(TEXT("无效输入提供明确错误"), !FailureReason.IsEmpty());
	InvalidSettings = FSoftBodyTetGridSettings();
	InvalidSettings.bSplitMaterialAtHalfX = false;
	InvalidSettings.UniformMaterialId = 2;
	FailureReason.Reset();
	TestFalse(TEXT("标准基准拒绝未知材料编号"),
		FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(InvalidSettings, InvalidMesh, &FailureReason));
	TestTrue(TEXT("未知材料编号提供明确错误"), !FailureReason.IsEmpty());
	return true;
}

#endif
