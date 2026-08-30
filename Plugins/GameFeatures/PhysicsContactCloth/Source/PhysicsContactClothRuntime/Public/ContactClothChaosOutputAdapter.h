#pragma once

#include "ClothingSystemRuntimeTypes.h"
#include "ContactClothChaosTopology.h"
#include "CoreMinimal.h"

struct FContactClothChaosOutputStats
{
	int32 AssetCount = 0;
	int32 VertexCount = 0;
	int32 MinimumLodIndex = INDEX_NONE;
	int32 MaximumLodIndex = INDEX_NONE;
};

/** Maps Chaos Cloth per-asset reference-space output back into the topology contract's component space. */
class PHYSICSCONTACTCLOTHRUNTIME_API FContactClothChaosOutputAdapter
{
public:
	static bool MergeComponentSpace(
		const TMap<int32, FClothSimulData>& ClothData,
		const FContactClothExplicitTopology& Topology,
		TArray<FVector3f>& OutPositions,
		FContactClothChaosOutputStats& OutStats,
		FString* OutFailureReason = nullptr);
};
