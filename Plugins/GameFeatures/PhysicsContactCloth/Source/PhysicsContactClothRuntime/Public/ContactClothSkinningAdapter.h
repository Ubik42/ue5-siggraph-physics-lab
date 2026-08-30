#pragma once

#include "ContactClothChaosTopology.h"
#include "CoreMinimal.h"

class USkeletalMesh;
class USkeletalMeshComponent;

struct FContactClothSkinningStats
{
	int32 AssetCount = 0;
	int32 VertexCount = 0;
	int32 FixedTargetCount = 0;
	int32 DynamicTargetCount = 0;
	int32 TotalUsedBoneCount = 0;
	int32 TotalBoneInfluenceCount = 0;
	int32 MaxBoneInfluenceCount = 0;
	float MinimumBoneWeightSum = 0.0f;
	float MaximumBoneWeightSum = 0.0f;
};

struct FContactClothSkinnedTargets
{
	TArray<FVector3f> Positions;
	TArray<FVector3f> Normals;
	FContactClothSkinningStats Stats;
};

/** Uses UE's ClothingMeshUtils skinning path so research targets match Chaos cloth asset semantics. */
class PHYSICSCONTACTCLOTHRUNTIME_API FContactClothSkinningAdapter
{
public:
	static bool SkinReferencePose(
		const USkeletalMesh& SkeletalMesh,
		const FContactClothExplicitTopology& Topology,
		FContactClothSkinnedTargets& OutTargets,
		FString* OutFailureReason = nullptr);

	static bool SkinComponentPose(
		const USkeletalMeshComponent& SkeletalMeshComponent,
		const FContactClothExplicitTopology& Topology,
		FContactClothSkinnedTargets& OutTargets,
		FString* OutFailureReason = nullptr);

	static bool SkinRefToLocalMatrices(
		const USkeletalMesh& SkeletalMesh,
		const FContactClothExplicitTopology& Topology,
		TConstArrayView<FMatrix44f> RefToLocalMatrices,
		FContactClothSkinnedTargets& OutTargets,
		FString* OutFailureReason = nullptr);
};
