#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothChaosOutputAdapter.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothChaosOutputAdapterTest,
	"PhysicsResearch.ContactCloth.ChaosOutputComponentSpaceMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothChaosOutputAdapterTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assassin/Mesh/SKM_Assassin_Skin1.SKM_Assassin_Skin1"));
	TestNotNull(TEXT("加载 Assassin Skeletal Mesh"), Mesh);
	if (!Mesh) return false;
	FContactClothExplicitTopology Topology;
	FString FailureReason;
	TestTrue(TEXT("提取 Assassin 显式拓扑"), FContactClothChaosTopology::ExtractLod0(*Mesh, Topology, &FailureReason));
	if (!Topology.IsValid()) return false;

	TMap<int32, FClothSimulData> SyntheticData;
	for (int32 AssetIndex = 0; AssetIndex < Topology.AssetRanges.Num(); ++AssetIndex)
	{
		const FContactClothTopologyAssetRange& Range = Topology.AssetRanges[AssetIndex];
		FClothSimulData& Data = SyntheticData.Add(AssetIndex);
		Data.LODIndex = 0;
		Data.ComponentRelativeTransform = FTransform(
			FRotator(3.0 * AssetIndex, 11.0 + 7.0 * AssetIndex, -2.0 * AssetIndex),
			FVector(9.0 * AssetIndex, -4.0 * AssetIndex, 5.0 + AssetIndex));
		const FTransform ComponentToReference = Data.ComponentRelativeTransform.Inverse();
		Data.Positions.Reserve(Range.VertexCount);
		for (int32 LocalIndex = 0; LocalIndex < Range.VertexCount; ++LocalIndex)
		{
			Data.Positions.Add(FVector3f(ComponentToReference.TransformPosition(FVector(
				Topology.RestPositions[Range.VertexOffset + LocalIndex]))));
		}
	}

	TArray<FVector3f> MergedPositions;
	FContactClothChaosOutputStats Stats;
	TestTrue(TEXT("合并 Chaos 资产输出到组件空间"), FContactClothChaosOutputAdapter::MergeComponentSpace(
		SyntheticData, Topology, MergedPositions, Stats, &FailureReason));
	double MaximumErrorCm = 0.0;
	for (int32 Index = 0; Index < MergedPositions.Num(); ++Index)
	{
		MaximumErrorCm = FMath::Max(MaximumErrorCm, static_cast<double>((MergedPositions[Index] - Topology.RestPositions[Index]).Length()));
	}
	AddInfo(FString::Printf(TEXT("Chaos output component-space merge: assets=%d vertices=%d lod_min=%d lod_max=%d maximum_error_cm=%.9f"),
		Stats.AssetCount, Stats.VertexCount, Stats.MinimumLodIndex, Stats.MaximumLodIndex, MaximumErrorCm));
	TestEqual(TEXT("合并两份 Clothing Asset"), Stats.AssetCount, 2);
	TestEqual(TEXT("合并 2011 个物理粒子"), Stats.VertexCount, 2011);
	TestEqual(TEXT("最小 LOD 为 0"), Stats.MinimumLodIndex, 0);
	TestEqual(TEXT("最大 LOD 为 0"), Stats.MaximumLodIndex, 0);
	TestTrue(TEXT("组件空间往返误差可忽略"), MaximumErrorCm < 1.e-3);

	SyntheticData.Remove(1);
	TestFalse(TEXT("缺少任一资产时拒绝伪合并"), FContactClothChaosOutputAdapter::MergeComponentSpace(
		SyntheticData, Topology, MergedPositions, Stats, &FailureReason));
	TestTrue(TEXT("缺失资产失败原因可读"), FailureReason.Contains(TEXT("资产数")));
	return true;
}

#endif
