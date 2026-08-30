#if WITH_DEV_AUTOMATION_TESTS

#include "SoftBodyGpuTopology.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSoftBodyGpuTopologyContractTest,
	"PhysicsResearch.SoftBody.GpuTopology.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSoftBodyGpuTopologyContractTest::RunTest(const FString& Parameters)
{
	const FIntVector Tiers[] = { FIntVector(4, 2, 2), FIntVector(8, 4, 4), FIntVector(16, 8, 8) };
	const int32 ExpectedVertices[] = { 45, 225, 1377 };
	const int32 ExpectedTets[] = { 96, 768, 6144 };
	const int32 ExpectedEdges[] = { 180, 1152, 8160 };
	for (int32 Tier = 0; Tier < 3; ++Tier)
	{
		FSoftBodyTetGridSettings GridSettings;
		GridSettings.CellCounts = Tiers[Tier];
		GridSettings.CellSizeCm = FVector3f(10.0f);
		FSoftBodyTetMesh Mesh;
		FSoftBodyGpuTopology Topology;
		FString FailureReason;
		TestTrue(TEXT("生成 GPU 合同源四面体"), FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(GridSettings, Mesh, &FailureReason));
		TestTrue(TEXT("构建 GPU 边/四面体 CSR"), FSoftBodyGpuTopologyBuilder::Build(Mesh, Topology, &FailureReason));
		TestTrue(TEXT("GPU CSR 自验证"), Topology.IsValid(Mesh.RestPositions.Num(), &FailureReason));
		TestEqual(TEXT("顶点数精确"), Mesh.RestPositions.Num(), ExpectedVertices[Tier]);
		TestEqual(TEXT("边约束数精确"), Topology.EdgeVertices.Num(), ExpectedEdges[Tier]);
		TestEqual(TEXT("四面体约束数精确"), Topology.TetrahedronVertices.Num(), ExpectedTets[Tier]);
		TestEqual(TEXT("边关联恰为约束两倍"), Topology.VertexEdgeConstraintIndices.Num(), ExpectedEdges[Tier] * 2);
		TestEqual(TEXT("四面体关联恰为约束四倍"), Topology.VertexTetrahedronConstraintIndices.Num(), ExpectedTets[Tier] * 4);
		AddInfo(FString::Printf(TEXT("Soft-body GPU topology tier=%d vertices=%d edges=%d edge_incidence=%d tets=%d tet_incidence=%d"),
			Tier, Mesh.RestPositions.Num(), Topology.EdgeVertices.Num(), Topology.VertexEdgeConstraintIndices.Num(),
			Topology.TetrahedronVertices.Num(), Topology.VertexTetrahedronConstraintIndices.Num()));
	}

	FSoftBodyTetGridSettings GridSettings;
	FSoftBodyTetMesh Mesh;
	FString FailureReason;
	TestTrue(TEXT("生成破坏性测试源网格"), FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(GridSettings, Mesh, &FailureReason));
	FSoftBodyGpuTopology Broken;
	TestTrue(TEXT("构建破坏性测试 GPU 合同"), FSoftBodyGpuTopologyBuilder::Build(Mesh, Broken, &FailureReason));
	Broken.VertexTetrahedronOffsets.Last()++;
	FailureReason.Reset();
	TestFalse(TEXT("拒绝越界 CSR 末端"), Broken.IsValid(Mesh.RestPositions.Num(), &FailureReason));
	TestTrue(TEXT("非法 GPU 合同提供明确错误"), !FailureReason.IsEmpty());
	return true;
}

#endif
