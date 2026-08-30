#if WITH_DEV_AUTOMATION_TESTS

#include "SoftBodySurfaceBinding.h"

#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "StaticMeshResources.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoftBodySurfaceBindingTest,
	"PhysicsResearch.SoftBody.SurfaceBinding.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSoftBodySurfaceBindingTest::RunTest(const FString& Parameters)
{
	FSoftBodyTetGridSettings SyntheticSettings;
	SyntheticSettings.CellCounts = FIntVector(2, 2, 2);
	SyntheticSettings.CellSizeCm = FVector3f(10.0f);
	SyntheticSettings.OriginCm = FVector3f(-10.0f);
	FSoftBodyTetMesh SyntheticCage;
	FString FailureReason;
	TestTrue(TEXT("生成带原点的合成 Cage"), FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(SyntheticSettings, SyntheticCage, &FailureReason));
	const TArray<FVector3f> SyntheticSurface = {
		FVector3f(-10.0f, -10.0f, -10.0f), FVector3f(10.0f, 10.0f, 10.0f),
		FVector3f(0.0f, 0.0f, 0.0f), FVector3f(3.25f, -4.5f, 7.75f) };
	FSoftBodySurfaceBinding SyntheticBinding;
	TestTrue(TEXT("建立合成三线性表面绑定"), FSoftBodySurfaceBindingBuilder::BuildTrilinearGridBinding(
		SyntheticSettings, SyntheticSurface, SyntheticBinding, &FailureReason));
	TestTrue(TEXT("合成表面静止重建误差近零"),
		SyntheticBinding.CalculateMaximumRestReconstructionErrorCm(SyntheticCage.RestPositions) < 1.e-4);
	FSoftBodySurfaceBinding RejectedBinding;
	TestFalse(TEXT("拒绝超出 Cage 的表面点"), FSoftBodySurfaceBindingBuilder::BuildTrilinearGridBinding(
		SyntheticSettings, TArray<FVector3f>{ FVector3f(100.0f, 0.0f, 0.0f) }, RejectedBinding, &FailureReason));

	UStaticMesh* DragonCageAsset = LoadObject<UStaticMesh>(nullptr,
		TEXT("/PhysicsSoftBody/DragonDerived/SM_TerrorBringer_SoftBodyCage.SM_TerrorBringer_SoftBodyCage"));
	if (!DragonCageAsset || !DragonCageAsset->GetRenderData() || DragonCageAsset->GetRenderData()->LODResources.IsEmpty())
	{
		AddError(TEXT("缺少可读的 TerrorBringer 派生封闭外壳；请先运行 import_case02_dragon_cage.py"));
		return false;
	}
	const FPositionVertexBuffer& PositionBuffer = DragonCageAsset->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer;
	TArray<FVector3f> DragonSurface;
	DragonSurface.Reserve(PositionBuffer.GetNumVertices());
	FVector3f Minimum(TNumericLimits<float>::Max());
	FVector3f Maximum(-TNumericLimits<float>::Max());
	for (uint32 Vertex = 0; Vertex < PositionBuffer.GetNumVertices(); ++Vertex)
	{
		const FVector3f Position = PositionBuffer.VertexPosition(Vertex);
		DragonSurface.Add(Position);
		Minimum.X = FMath::Min(Minimum.X, Position.X); Minimum.Y = FMath::Min(Minimum.Y, Position.Y); Minimum.Z = FMath::Min(Minimum.Z, Position.Z);
		Maximum.X = FMath::Max(Maximum.X, Position.X); Maximum.Y = FMath::Max(Maximum.Y, Position.Y); Maximum.Z = FMath::Max(Maximum.Z, Position.Z);
	}
	const FVector3f Padding(0.1f);
	Minimum -= Padding;
	Maximum += Padding;
	FSoftBodyTetGridSettings DragonSettings;
	DragonSettings.CellCounts = FIntVector(8, 8, 8);
	DragonSettings.OriginCm = Minimum;
	DragonSettings.CellSizeCm = FVector3f(
		(Maximum.X - Minimum.X) / DragonSettings.CellCounts.X,
		(Maximum.Y - Minimum.Y) / DragonSettings.CellCounts.Y,
		(Maximum.Z - Minimum.Z) / DragonSettings.CellCounts.Z);
	DragonSettings.bFixMinimumXFace = true;
	FSoftBodyTetMesh DragonVolumeCage;
	TestTrue(TEXT("生成 Dragon 规则体积 Cage"), FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(
		DragonSettings, DragonVolumeCage, &FailureReason));
	FSoftBodySurfaceBinding DragonBinding;
	TestTrue(TEXT("绑定 Dragon 派生封闭表面"), FSoftBodySurfaceBindingBuilder::BuildTrilinearGridBinding(
		DragonSettings, DragonSurface, DragonBinding, &FailureReason));
	const double RestError = DragonBinding.CalculateMaximumRestReconstructionErrorCm(DragonVolumeCage.RestPositions);
	TestTrue(TEXT("Dragon 表面静止重建误差小于 0.01 cm"), RestError < 0.01);

	TArray<FVector3f> DeformedCage = DragonVolumeCage.RestPositions;
	const float ExtentX = Maximum.X - Minimum.X;
	for (int32 Vertex = 0; Vertex < DeformedCage.Num(); ++Vertex)
	{
		if (DragonVolumeCage.InverseMasses[Vertex] <= 0.0f) continue;
		const float T = FMath::Clamp((DragonVolumeCage.RestPositions[Vertex].X - Minimum.X) / ExtentX, 0.0f, 1.0f);
		DeformedCage[Vertex].Z += 40.0f * FMath::Sin(PI * T);
	}
	TArray<FVector3f> DeformedSurface;
	TestTrue(TEXT("应用 Dragon 表面 Cage 变形"), DragonBinding.Apply(DeformedCage, DeformedSurface, &FailureReason));
	double MaximumDisplacement = 0.0;
	for (int32 Vertex = 0; Vertex < DeformedSurface.Num(); ++Vertex)
	{
		MaximumDisplacement = FMath::Max(MaximumDisplacement,
			static_cast<double>((DeformedSurface[Vertex] - DragonSurface[Vertex]).Length()));
	}
	TestTrue(TEXT("Dragon 表面产生可观测 Cage 形变"), MaximumDisplacement > 20.0);
	AddInfo(FString::Printf(
		TEXT("Dragon surface binding vertices=%d cage_vertices=%d cage_tets=%d rest_reconstruction_error_cm=%.9f maximum_deformation_cm=%.9f fixed_cage_vertices=%d"),
		DragonSurface.Num(), DragonVolumeCage.RestPositions.Num(), DragonVolumeCage.Tetrahedra.Num(),
		RestError, MaximumDisplacement, DragonVolumeCage.CountFixedVertices()));
	return true;
}

#endif
