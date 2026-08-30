#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothChaosTopology.h"
#include "Algo/AllOf.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothChaosTopologyParityTest,
	"PhysicsResearch.ContactCloth.ChaosClothTopologyExtraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothChaosTopologyParityTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assassin/Mesh/SKM_Assassin_Skin1.SKM_Assassin_Skin1"));
	TestNotNull(TEXT("加载 Assassin Skeletal Mesh"), Mesh);
	if (!Mesh) return false;
	FContactClothExplicitTopology Topology;
	FString FailureReason;
	const bool bExtracted = FContactClothChaosTopology::ExtractLod0(*Mesh, Topology, &FailureReason);
	TestTrue(TEXT("正式运行时提取器构建 Chaos LOD0 拓扑"), bExtracted);
	if (!bExtracted)
	{
		AddError(FailureReason);
		return false;
	}
	TestTrue(TEXT("合并拓扑通过完整性合同"), Topology.IsValid());
	TestEqual(TEXT("Assassin 具有两份 Chaos Clothing Asset"), Topology.AssetRanges.Num(), 2);
	int32 MaxDistanceMovable = 0;
	for (float MaxDistance : Topology.MaxDistances) MaxDistanceMovable += MaxDistance > 0.0f ? 1 : 0;
	TestEqual(TEXT("MaxDistance 可动点与逆质量动态点一致"), MaxDistanceMovable, Topology.DynamicVertexCount);
	TestEqual(TEXT("资产区间完整覆盖全部顶点"), Topology.AssetRanges[0].VertexCount + Topology.AssetRanges[1].VertexCount, Topology.RestPositions.Num());
	TestEqual(TEXT("资产区间完整覆盖全部边"), Topology.AssetRanges[0].EdgeCount + Topology.AssetRanges[1].EdgeCount, Topology.StructuralEdges.Num());
	TestTrue(TEXT("所有结构边静止长度为有限正值"), Algo::AllOf(Topology.StructuralRestLengthsCm, [](float Length) { return FMath::IsFinite(Length) && Length > 0.0f; }));
	AddInfo(FString::Printf(TEXT("Chaos cloth topology: assets=%d vertices=%d triangles=%d unique_edges=%d fixed_vertices=%d dynamic_vertices=%d max_distance_movable=%d"),
		Topology.AssetRanges.Num(), Topology.RestPositions.Num(), Topology.TriangleIndices.Num() / 3, Topology.StructuralEdges.Num(),
		Topology.FixedVertexCount, Topology.DynamicVertexCount, MaxDistanceMovable));
	return true;
}

#endif
