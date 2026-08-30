#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

struct FContactClothTopologyAssetRange
{
	FString AssetName;
	int32 VertexOffset = 0;
	int32 VertexCount = 0;
	int32 TriangleOffset = 0;
	int32 TriangleCount = 0;
	int32 EdgeOffset = 0;
	int32 EdgeCount = 0;
	int32 UsedBoneCount = 0;
	int32 ReferenceBoneIndex = INDEX_NONE;
	int32 MaxBoneInfluences = 0;
};

/** Solver-neutral copy of the exact LOD0 topology used by an existing Chaos Clothing Asset. */
struct FContactClothExplicitTopology
{
	TArray<FVector3f> RestPositions;
	TArray<FVector3f> RestNormals;
	TArray<float> InverseMasses;
	TArray<float> MaxDistances;
	TArray<uint32> TriangleIndices;
	TArray<FIntPoint> StructuralEdges;
	TArray<float> StructuralRestLengthsCm;
	TArray<FContactClothTopologyAssetRange> AssetRanges;
	int32 FixedVertexCount = 0;
	int32 DynamicVertexCount = 0;
	int32 TotalBoneInfluenceCount = 0;
	int32 MaxBoneInfluenceCount = 0;
	float MinimumBoneWeightSum = 0.0f;
	float MaximumBoneWeightSum = 0.0f;

	bool IsValid() const
	{
		return !RestPositions.IsEmpty()
			&& RestNormals.Num() == RestPositions.Num()
			&& InverseMasses.Num() == RestPositions.Num()
			&& MaxDistances.Num() == RestPositions.Num()
			&& TriangleIndices.Num() > 0 && TriangleIndices.Num() % 3 == 0
			&& StructuralEdges.Num() > 0 && StructuralRestLengthsCm.Num() == StructuralEdges.Num()
			&& FixedVertexCount + DynamicVertexCount == RestPositions.Num()
			&& TotalBoneInfluenceCount >= RestPositions.Num()
			&& MaxBoneInfluenceCount > 0 && MaxBoneInfluenceCount <= 12
			&& FMath::IsFinite(MinimumBoneWeightSum) && MinimumBoneWeightSum > 0.0f
			&& FMath::IsFinite(MaximumBoneWeightSum) && MaximumBoneWeightSum >= MinimumBoneWeightSum
			&& !AssetRanges.IsEmpty();
	}
};

class PHYSICSCONTACTCLOTHRUNTIME_API FContactClothChaosTopology
{
public:
	static bool ExtractLod0(const USkeletalMesh& SkeletalMesh, FContactClothExplicitTopology& OutTopology, FString* OutFailureReason = nullptr);
};
