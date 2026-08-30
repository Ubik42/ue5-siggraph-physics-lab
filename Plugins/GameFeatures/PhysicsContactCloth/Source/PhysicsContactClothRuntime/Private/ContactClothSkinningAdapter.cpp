#include "ContactClothSkinningAdapter.h"

#include "ClothingAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Utils/ClothingMeshUtils.h"

namespace ContactClothSkinningAdapter
{
bool Fail(FContactClothSkinnedTargets& OutTargets, FString* OutFailureReason, const FString& Reason)
{
	OutTargets = FContactClothSkinnedTargets();
	if (OutFailureReason) *OutFailureReason = Reason;
	return false;
}
}

bool FContactClothSkinningAdapter::SkinReferencePose(
	const USkeletalMesh& SkeletalMesh,
	const FContactClothExplicitTopology& Topology,
	FContactClothSkinnedTargets& OutTargets,
	FString* OutFailureReason)
{
	TArray<FMatrix44f> IdentityMatrices;
	IdentityMatrices.Init(FMatrix44f::Identity, SkeletalMesh.GetRefSkeleton().GetNum());
	return SkinRefToLocalMatrices(SkeletalMesh, Topology, IdentityMatrices, OutTargets, OutFailureReason);
}

bool FContactClothSkinningAdapter::SkinComponentPose(
	const USkeletalMeshComponent& SkeletalMeshComponent,
	const FContactClothExplicitTopology& Topology,
	FContactClothSkinnedTargets& OutTargets,
	FString* OutFailureReason)
{
	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent.GetSkeletalMeshAsset();
	if (!SkeletalMesh)
	{
		return ContactClothSkinningAdapter::Fail(OutTargets, OutFailureReason, TEXT("Skeletal Mesh Component 未设置 Skeletal Mesh"));
	}
	TArray<FMatrix44f> RefToLocalMatrices;
	SkeletalMeshComponent.GetCurrentRefToLocalMatrices(RefToLocalMatrices, 0);
	return SkinRefToLocalMatrices(*SkeletalMesh, Topology, RefToLocalMatrices, OutTargets, OutFailureReason);
}

bool FContactClothSkinningAdapter::SkinRefToLocalMatrices(
	const USkeletalMesh& SkeletalMesh,
	const FContactClothExplicitTopology& Topology,
	TConstArrayView<FMatrix44f> RefToLocalMatrices,
	FContactClothSkinnedTargets& OutTargets,
	FString* OutFailureReason)
{
	OutTargets = FContactClothSkinnedTargets();
	if (OutFailureReason) OutFailureReason->Reset();
	if (!Topology.IsValid() || RefToLocalMatrices.Num() < SkeletalMesh.GetRefSkeleton().GetNum())
	{
		return ContactClothSkinningAdapter::Fail(OutTargets, OutFailureReason, TEXT("显式拓扑或 RefToLocal 骨骼矩阵不完整"));
	}
	const TArray<UClothingAssetBase*>& Assets = SkeletalMesh.GetMeshClothingAssets();
	if (Assets.Num() != Topology.AssetRanges.Num())
	{
		return ContactClothSkinningAdapter::Fail(OutTargets, OutFailureReason, TEXT("Clothing Asset 数量与拓扑区间不一致"));
	}

	OutTargets.Positions.Reserve(Topology.RestPositions.Num());
	OutTargets.Normals.Reserve(Topology.RestNormals.Num());
	for (int32 AssetIndex = 0; AssetIndex < Assets.Num(); ++AssetIndex)
	{
		const UClothingAssetCommon* Asset = Cast<UClothingAssetCommon>(Assets[AssetIndex]);
		const FContactClothTopologyAssetRange& Range = Topology.AssetRanges[AssetIndex];
		if (!Asset || Asset->LodData.IsEmpty() || Range.AssetName != Asset->GetName()
			|| Range.VertexOffset != OutTargets.Positions.Num())
		{
			return ContactClothSkinningAdapter::Fail(OutTargets, OutFailureReason, TEXT("Clothing Asset 顺序或拓扑区间不一致"));
		}
		const FClothPhysicalMeshData& PhysicalMesh = Asset->LodData[0].PhysicalMeshData;
		if (PhysicalMesh.Vertices.Num() != Range.VertexCount || Asset->UsedBoneIndices.Num() != Range.UsedBoneCount)
		{
			return ContactClothSkinningAdapter::Fail(OutTargets, OutFailureReason, FString::Printf(
				TEXT("Clothing Asset %s 的顶点或骨骼映射已变化"), *Asset->GetName()));
		}
		TArray<FVector3f> AssetPositions;
		TArray<FVector3f> AssetNormals;
		ClothingMeshUtils::SkinPhysicsMesh(
			Asset->UsedBoneIndices,
			PhysicalMesh,
			FTransform::Identity,
			RefToLocalMatrices.GetData(),
			RefToLocalMatrices.Num(),
			AssetPositions,
			AssetNormals);
		if (AssetPositions.Num() != Range.VertexCount || AssetNormals.Num() != Range.VertexCount)
		{
			return ContactClothSkinningAdapter::Fail(OutTargets, OutFailureReason, FString::Printf(
				TEXT("Clothing Asset %s 蒙皮输出数量不一致"), *Asset->GetName()));
		}
		OutTargets.Positions.Append(AssetPositions);
		OutTargets.Normals.Append(AssetNormals);
	}
	if (OutTargets.Positions.Num() != Topology.RestPositions.Num())
	{
		return ContactClothSkinningAdapter::Fail(OutTargets, OutFailureReason, TEXT("合并蒙皮目标未覆盖全部物理顶点"));
	}
	OutTargets.Stats.AssetCount = Topology.AssetRanges.Num();
	OutTargets.Stats.VertexCount = Topology.RestPositions.Num();
	OutTargets.Stats.FixedTargetCount = Topology.FixedVertexCount;
	OutTargets.Stats.DynamicTargetCount = Topology.DynamicVertexCount;
	for (const FContactClothTopologyAssetRange& Range : Topology.AssetRanges)
	{
		OutTargets.Stats.TotalUsedBoneCount += Range.UsedBoneCount;
	}
	OutTargets.Stats.TotalBoneInfluenceCount = Topology.TotalBoneInfluenceCount;
	OutTargets.Stats.MaxBoneInfluenceCount = Topology.MaxBoneInfluenceCount;
	OutTargets.Stats.MinimumBoneWeightSum = Topology.MinimumBoneWeightSum;
	OutTargets.Stats.MaximumBoneWeightSum = Topology.MaximumBoneWeightSum;
	return true;
}
