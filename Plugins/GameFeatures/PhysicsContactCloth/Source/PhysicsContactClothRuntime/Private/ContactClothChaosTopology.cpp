#include "ContactClothChaosTopology.h"

#include "ClothingAsset.h"
#include "ClothPhysicalMeshData.h"
#include "Engine/SkeletalMesh.h"
#include "PointWeightMap.h"

namespace ContactClothChaosTopology
{
bool Fail(FString* OutFailureReason, const FString& Reason)
{
	if (OutFailureReason) *OutFailureReason = Reason;
	return false;
}
}

bool FContactClothChaosTopology::ExtractLod0(
	const USkeletalMesh& SkeletalMesh,
	FContactClothExplicitTopology& OutTopology,
	FString* OutFailureReason)
{
	OutTopology = FContactClothExplicitTopology();
	OutTopology.MinimumBoneWeightSum = TNumericLimits<float>::Max();
	if (OutFailureReason) OutFailureReason->Reset();
	const TArray<UClothingAssetBase*>& Assets = SkeletalMesh.GetMeshClothingAssets();
	if (Assets.IsEmpty()) return ContactClothChaosTopology::Fail(OutFailureReason, TEXT("Skeletal Mesh 未绑定 Clothing Asset"));
	for (const UClothingAssetBase* BaseAsset : Assets)
	{
		const UClothingAssetCommon* Asset = Cast<UClothingAssetCommon>(BaseAsset);
		if (!Asset || Asset->LodData.IsEmpty())
		{
			return ContactClothChaosTopology::Fail(OutFailureReason, TEXT("Clothing Asset 缺少可读取的 Common LOD0"));
		}
		const FClothPhysicalMeshData& Mesh = Asset->LodData[0].PhysicalMeshData;
		const int32 VertexCount = Mesh.Vertices.Num();
		const FPointWeightMap* MaxDistance = Mesh.FindWeightMap(EWeightMapTargetCommon::MaxDistance);
		if (VertexCount <= 0 || Mesh.Normals.Num() != VertexCount || Mesh.InverseMasses.Num() != VertexCount
			|| Mesh.BoneData.Num() != VertexCount || Mesh.Indices.IsEmpty() || Mesh.Indices.Num() % 3 != 0
			|| !MaxDistance || MaxDistance->Num() != VertexCount || Asset->UsedBoneIndices.IsEmpty())
		{
			return ContactClothChaosTopology::Fail(OutFailureReason, FString::Printf(TEXT("Clothing Asset %s 的物理网格数组不完整"), *Asset->GetName()));
		}
		FContactClothTopologyAssetRange Range;
		Range.AssetName = Asset->GetName();
		Range.VertexOffset = OutTopology.RestPositions.Num();
		Range.VertexCount = VertexCount;
		Range.TriangleOffset = OutTopology.TriangleIndices.Num() / 3;
		Range.TriangleCount = Mesh.Indices.Num() / 3;
		Range.EdgeOffset = OutTopology.StructuralEdges.Num();
		Range.UsedBoneCount = Asset->UsedBoneIndices.Num();
		Range.ReferenceBoneIndex = Asset->ReferenceBoneIndex;
		Range.MaxBoneInfluences = Mesh.MaxBoneWeights;
		OutTopology.RestPositions.Append(Mesh.Vertices);
		OutTopology.RestNormals.Append(Mesh.Normals);
		OutTopology.InverseMasses.Append(Mesh.InverseMasses);
		for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
		{
			const FVector3f& Position = Mesh.Vertices[VertexIndex];
			const FVector3f& Normal = Mesh.Normals[VertexIndex];
			const float InverseMass = Mesh.InverseMasses[VertexIndex];
			const float Distance = MaxDistance->GetValue(VertexIndex);
			const FClothVertBoneData& BoneData = Mesh.BoneData[VertexIndex];
			if (!FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y) || !FMath::IsFinite(Position.Z)
				|| !FMath::IsFinite(Normal.X) || !FMath::IsFinite(Normal.Y) || !FMath::IsFinite(Normal.Z)
				|| !FMath::IsFinite(InverseMass) || InverseMass < 0.0f || !FMath::IsFinite(Distance) || Distance < 0.0f)
			{
				return ContactClothChaosTopology::Fail(OutFailureReason, FString::Printf(TEXT("Clothing Asset %s 包含非有限或负的物理数据"), *Asset->GetName()));
			}
			if (BoneData.NumInfluences <= 0 || BoneData.NumInfluences > FClothVertBoneData::MaxTotalInfluences)
			{
				return ContactClothChaosTopology::Fail(OutFailureReason, FString::Printf(TEXT("Clothing Asset %s 包含无效骨骼影响数量"), *Asset->GetName()));
			}
			float BoneWeightSum = 0.0f;
			for (int32 InfluenceIndex = 0; InfluenceIndex < BoneData.NumInfluences; ++InfluenceIndex)
			{
				const int32 LocalBoneIndex = BoneData.BoneIndices[InfluenceIndex];
				const float BoneWeight = BoneData.BoneWeights[InfluenceIndex];
				if (!Asset->UsedBoneIndices.IsValidIndex(LocalBoneIndex)
					|| !SkeletalMesh.GetRefSkeleton().IsValidIndex(Asset->UsedBoneIndices[LocalBoneIndex])
					|| !FMath::IsFinite(BoneWeight) || BoneWeight < 0.0f)
				{
					return ContactClothChaosTopology::Fail(OutFailureReason, FString::Printf(TEXT("Clothing Asset %s 包含无效骨骼映射或权重"), *Asset->GetName()));
				}
				BoneWeightSum += BoneWeight;
			}
			if (!FMath::IsFinite(BoneWeightSum) || BoneWeightSum <= 0.0f)
			{
				return ContactClothChaosTopology::Fail(OutFailureReason, FString::Printf(TEXT("Clothing Asset %s 包含空骨骼权重"), *Asset->GetName()));
			}
			OutTopology.TotalBoneInfluenceCount += BoneData.NumInfluences;
			OutTopology.MaxBoneInfluenceCount = FMath::Max(OutTopology.MaxBoneInfluenceCount, BoneData.NumInfluences);
			OutTopology.MinimumBoneWeightSum = FMath::Min(OutTopology.MinimumBoneWeightSum, BoneWeightSum);
			OutTopology.MaximumBoneWeightSum = FMath::Max(OutTopology.MaximumBoneWeightSum, BoneWeightSum);
			OutTopology.MaxDistances.Add(Distance);
			if (InverseMass > 0.0f) OutTopology.DynamicVertexCount++; else OutTopology.FixedVertexCount++;
		}
		TSet<uint64> UniqueEdgeSet;
		for (int32 IndexOffset = 0; IndexOffset < Mesh.Indices.Num(); IndexOffset += 3)
		{
			const uint32 Triangle[3] = {Mesh.Indices[IndexOffset], Mesh.Indices[IndexOffset + 1], Mesh.Indices[IndexOffset + 2]};
			for (uint32 Index : Triangle)
			{
				if (Index >= static_cast<uint32>(VertexCount))
				{
					return ContactClothChaosTopology::Fail(OutFailureReason, FString::Printf(TEXT("Clothing Asset %s 包含越界三角形索引"), *Asset->GetName()));
				}
				OutTopology.TriangleIndices.Add(Range.VertexOffset + Index);
			}
			for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
			{
				const uint32 A = FMath::Min(Triangle[EdgeIndex], Triangle[(EdgeIndex + 1) % 3]);
				const uint32 B = FMath::Max(Triangle[EdgeIndex], Triangle[(EdgeIndex + 1) % 3]);
				UniqueEdgeSet.Add((static_cast<uint64>(A) << 32) | B);
			}
		}
		TArray<uint64> SortedEdges = UniqueEdgeSet.Array();
		SortedEdges.Sort();
		for (uint64 EdgeKey : SortedEdges)
		{
			const int32 LocalA = static_cast<int32>(EdgeKey >> 32);
			const int32 LocalB = static_cast<int32>(EdgeKey & 0xffffffffu);
			OutTopology.StructuralEdges.Emplace(Range.VertexOffset + LocalA, Range.VertexOffset + LocalB);
			OutTopology.StructuralRestLengthsCm.Add((Mesh.Vertices[LocalA] - Mesh.Vertices[LocalB]).Length());
		}
		Range.EdgeCount = OutTopology.StructuralEdges.Num() - Range.EdgeOffset;
		OutTopology.AssetRanges.Add(MoveTemp(Range));
	}
	if (!OutTopology.IsValid()) return ContactClothChaosTopology::Fail(OutFailureReason, TEXT("合并后的 Clothing 拓扑未通过完整性检查"));
	return true;
}
