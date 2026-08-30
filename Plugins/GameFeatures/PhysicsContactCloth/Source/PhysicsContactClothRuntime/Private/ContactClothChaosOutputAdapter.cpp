#include "ContactClothChaosOutputAdapter.h"

namespace ContactClothChaosOutputAdapter
{
bool Fail(TArray<FVector3f>& OutPositions, FContactClothChaosOutputStats& OutStats, FString* OutFailureReason, const FString& Reason)
{
	OutPositions.Reset();
	OutStats = FContactClothChaosOutputStats();
	if (OutFailureReason) *OutFailureReason = Reason;
	return false;
}
}

bool FContactClothChaosOutputAdapter::MergeComponentSpace(
	const TMap<int32, FClothSimulData>& ClothData,
	const FContactClothExplicitTopology& Topology,
	TArray<FVector3f>& OutPositions,
	FContactClothChaosOutputStats& OutStats,
	FString* OutFailureReason)
{
	OutPositions.Reset();
	OutStats = FContactClothChaosOutputStats();
	if (OutFailureReason) OutFailureReason->Reset();
	if (!Topology.IsValid() || ClothData.Num() != Topology.AssetRanges.Num())
	{
		return ContactClothChaosOutputAdapter::Fail(OutPositions, OutStats, OutFailureReason,
			TEXT("Chaos Cloth 输出资产数与显式拓扑合同不一致"));
	}
	OutPositions.Reserve(Topology.RestPositions.Num());
	for (int32 AssetIndex = 0; AssetIndex < Topology.AssetRanges.Num(); ++AssetIndex)
	{
		const FContactClothTopologyAssetRange& Range = Topology.AssetRanges[AssetIndex];
		const FClothSimulData* Data = ClothData.Find(AssetIndex);
		if (!Data || Data->LODIndex != 0 || Data->Positions.Num() != Range.VertexCount
			|| Range.VertexOffset != OutPositions.Num() || !Data->ComponentRelativeTransform.IsValid())
		{
			return ContactClothChaosOutputAdapter::Fail(OutPositions, OutStats, OutFailureReason, FString::Printf(
				TEXT("Chaos Cloth 资产 %d 的 LOD、顶点数或组件变换不符合拓扑区间"), AssetIndex));
		}
		for (const FVector3f Position : Data->Positions)
		{
			const FVector ComponentPosition = Data->ComponentRelativeTransform.TransformPosition(FVector(Position));
			if (ComponentPosition.ContainsNaN())
			{
				return ContactClothChaosOutputAdapter::Fail(OutPositions, OutStats, OutFailureReason,
					TEXT("Chaos Cloth 输出包含非有限组件空间位置"));
			}
			OutPositions.Add(FVector3f(ComponentPosition));
		}
		OutStats.MinimumLodIndex = OutStats.MinimumLodIndex == INDEX_NONE ? Data->LODIndex : FMath::Min(OutStats.MinimumLodIndex, Data->LODIndex);
		OutStats.MaximumLodIndex = OutStats.MaximumLodIndex == INDEX_NONE ? Data->LODIndex : FMath::Max(OutStats.MaximumLodIndex, Data->LODIndex);
		OutStats.AssetCount++;
	}
	OutStats.VertexCount = OutPositions.Num();
	if (OutPositions.Num() != Topology.RestPositions.Num())
	{
		return ContactClothChaosOutputAdapter::Fail(OutPositions, OutStats, OutFailureReason,
			TEXT("Chaos Cloth 合并输出未覆盖全部显式拓扑粒子"));
	}
	return true;
}
