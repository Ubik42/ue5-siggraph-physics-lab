#include "SoftBodyGpuTopology.h"

namespace
{
struct FEdgeKey
{
	int32 A = INDEX_NONE;
	int32 B = INDEX_NONE;

	FEdgeKey(int32 InA, int32 InB)
		: A(FMath::Min(InA, InB)), B(FMath::Max(InA, InB))
	{
	}

	bool operator==(const FEdgeKey& Other) const { return A == Other.A && B == Other.B; }
	friend uint32 GetTypeHash(const FEdgeKey& Key)
	{
		return HashCombine(GetTypeHash(Key.A), GetTypeHash(Key.B));
	}
};

void SetGpuTopologyFailure(FString* OutFailureReason, const TCHAR* Message)
{
	if (OutFailureReason) *OutFailureReason = Message;
}
}

bool FSoftBodyGpuTopology::IsValid(int32 VertexCount, FString* OutFailureReason) const
{
	const bool bCountsMatch = VertexCount > 0
		&& EdgeVertices.Num() == EdgeRestLengthsCm.Num()
		&& EdgeVertices.Num() == EdgeMaterialIds.Num()
		&& VertexEdgeOffsets.Num() == VertexCount + 1
		&& VertexEdgeConstraintIndices.Num() == EdgeVertices.Num() * 2
		&& VertexEdgeCorners.Num() == VertexEdgeConstraintIndices.Num()
		&& TetrahedronVertices.Num() == TetrahedronRestVolumesCm3.Num()
		&& TetrahedronVertices.Num() == TetrahedronMaterialIds.Num()
		&& VertexTetrahedronOffsets.Num() == VertexCount + 1
		&& VertexTetrahedronConstraintIndices.Num() == TetrahedronVertices.Num() * 4
		&& VertexTetrahedronCorners.Num() == VertexTetrahedronConstraintIndices.Num();
	if (!bCountsMatch || VertexEdgeOffsets.IsEmpty() || VertexTetrahedronOffsets.IsEmpty()
		|| VertexEdgeOffsets[0] != 0 || VertexTetrahedronOffsets[0] != 0
		|| VertexEdgeOffsets.Last() != static_cast<uint32>(VertexEdgeConstraintIndices.Num())
		|| VertexTetrahedronOffsets.Last() != static_cast<uint32>(VertexTetrahedronConstraintIndices.Num()))
	{
		SetGpuTopologyFailure(OutFailureReason, TEXT("GPU 拓扑数组数量或 CSR 边界不一致"));
		return false;
	}
	for (int32 Vertex = 0; Vertex < VertexCount; ++Vertex)
	{
		if (VertexEdgeOffsets[Vertex] > VertexEdgeOffsets[Vertex + 1]
			|| VertexTetrahedronOffsets[Vertex] > VertexTetrahedronOffsets[Vertex + 1])
		{
			SetGpuTopologyFailure(OutFailureReason, TEXT("GPU 拓扑 CSR Offset 非单调"));
			return false;
		}
	}
	for (int32 EdgeIndex = 0; EdgeIndex < EdgeVertices.Num(); ++EdgeIndex)
	{
		const FIntPoint Edge = EdgeVertices[EdgeIndex];
		if (Edge.X < 0 || Edge.Y < 0 || Edge.X >= VertexCount || Edge.Y >= VertexCount || Edge.X == Edge.Y
			|| !FMath::IsFinite(EdgeRestLengthsCm[EdgeIndex]) || EdgeRestLengthsCm[EdgeIndex] <= 0.0f
			|| EdgeMaterialIds[EdgeIndex] > 1u)
		{
			SetGpuTopologyFailure(OutFailureReason, TEXT("GPU 拓扑包含非法边约束"));
			return false;
		}
	}
	for (int32 Incidence = 0; Incidence < VertexEdgeConstraintIndices.Num(); ++Incidence)
	{
		if (VertexEdgeConstraintIndices[Incidence] >= static_cast<uint32>(EdgeVertices.Num())
			|| VertexEdgeCorners[Incidence] > 1u)
		{
			SetGpuTopologyFailure(OutFailureReason, TEXT("GPU 拓扑包含非法边关联"));
			return false;
		}
	}
	for (int32 TetIndex = 0; TetIndex < TetrahedronVertices.Num(); ++TetIndex)
	{
		const FIntVector4 Tet = TetrahedronVertices[TetIndex];
		if (Tet.X < 0 || Tet.Y < 0 || Tet.Z < 0 || Tet.W < 0
			|| Tet.X >= VertexCount || Tet.Y >= VertexCount || Tet.Z >= VertexCount || Tet.W >= VertexCount
			|| !FMath::IsFinite(TetrahedronRestVolumesCm3[TetIndex]) || TetrahedronRestVolumesCm3[TetIndex] <= 0.0f
			|| TetrahedronMaterialIds[TetIndex] > 1u)
		{
			SetGpuTopologyFailure(OutFailureReason, TEXT("GPU 拓扑包含非法四面体约束"));
			return false;
		}
	}
	for (int32 Incidence = 0; Incidence < VertexTetrahedronConstraintIndices.Num(); ++Incidence)
	{
		if (VertexTetrahedronConstraintIndices[Incidence] >= static_cast<uint32>(TetrahedronVertices.Num())
			|| VertexTetrahedronCorners[Incidence] > 3u)
		{
			SetGpuTopologyFailure(OutFailureReason, TEXT("GPU 拓扑包含非法四面体关联"));
			return false;
		}
	}
	return true;
}

bool FSoftBodyGpuTopologyBuilder::Build(
	const FSoftBodyTetMesh& Mesh,
	FSoftBodyGpuTopology& OutTopology,
	FString* OutFailureReason)
{
	OutTopology = FSoftBodyGpuTopology();
	if (!Mesh.IsValid(OutFailureReason)) return false;

	TMap<FEdgeKey, uint32> EdgeMaterials;
	for (int32 TetIndex = 0; TetIndex < Mesh.Tetrahedra.Num(); ++TetIndex)
	{
		const FSoftBodyTetrahedron Tet = Mesh.Tetrahedra[TetIndex];
		const int32 Vertices[4] = { Tet.A, Tet.B, Tet.C, Tet.D };
		for (int32 I = 0; I < 4; ++I)
		{
			for (int32 J = I + 1; J < 4; ++J)
			{
				uint32& Material = EdgeMaterials.FindOrAdd(FEdgeKey(Vertices[I], Vertices[J]));
				Material = FMath::Max(Material, static_cast<uint32>(Mesh.MaterialIds[TetIndex]));
			}
		}
	}
	TArray<FEdgeKey> EdgeKeys;
	EdgeMaterials.GenerateKeyArray(EdgeKeys);
	EdgeKeys.Sort([](const FEdgeKey& Left, const FEdgeKey& Right)
	{
		return Left.A != Right.A ? Left.A < Right.A : Left.B < Right.B;
	});
	TArray<uint32> EdgeDegrees;
	TArray<uint32> TetDegrees;
	EdgeDegrees.Init(0, Mesh.RestPositions.Num());
	TetDegrees.Init(0, Mesh.RestPositions.Num());
	for (const FEdgeKey& Edge : EdgeKeys)
	{
		OutTopology.EdgeVertices.Emplace(Edge.A, Edge.B);
		OutTopology.EdgeRestLengthsCm.Add((Mesh.RestPositions[Edge.B] - Mesh.RestPositions[Edge.A]).Length());
		OutTopology.EdgeMaterialIds.Add(EdgeMaterials[Edge]);
		++EdgeDegrees[Edge.A];
		++EdgeDegrees[Edge.B];
	}
	for (int32 TetIndex = 0; TetIndex < Mesh.Tetrahedra.Num(); ++TetIndex)
	{
		const FSoftBodyTetrahedron Tet = Mesh.Tetrahedra[TetIndex];
		OutTopology.TetrahedronVertices.Emplace(Tet.A, Tet.B, Tet.C, Tet.D);
		OutTopology.TetrahedronRestVolumesCm3.Add(Mesh.RestVolumesCm3[TetIndex]);
		OutTopology.TetrahedronMaterialIds.Add(Mesh.MaterialIds[TetIndex]);
		++TetDegrees[Tet.A]; ++TetDegrees[Tet.B]; ++TetDegrees[Tet.C]; ++TetDegrees[Tet.D];
	}

	const int32 VertexCount = Mesh.RestPositions.Num();
	OutTopology.VertexEdgeOffsets.SetNumZeroed(VertexCount + 1);
	OutTopology.VertexTetrahedronOffsets.SetNumZeroed(VertexCount + 1);
	for (int32 Vertex = 0; Vertex < VertexCount; ++Vertex)
	{
		OutTopology.VertexEdgeOffsets[Vertex + 1] = OutTopology.VertexEdgeOffsets[Vertex] + EdgeDegrees[Vertex];
		OutTopology.VertexTetrahedronOffsets[Vertex + 1] = OutTopology.VertexTetrahedronOffsets[Vertex] + TetDegrees[Vertex];
	}
	OutTopology.VertexEdgeConstraintIndices.SetNumUninitialized(EdgeKeys.Num() * 2);
	OutTopology.VertexEdgeCorners.SetNumUninitialized(EdgeKeys.Num() * 2);
	OutTopology.VertexTetrahedronConstraintIndices.SetNumUninitialized(Mesh.Tetrahedra.Num() * 4);
	OutTopology.VertexTetrahedronCorners.SetNumUninitialized(Mesh.Tetrahedra.Num() * 4);
	TArray<uint32> EdgeWriteOffsets = OutTopology.VertexEdgeOffsets;
	TArray<uint32> TetWriteOffsets = OutTopology.VertexTetrahedronOffsets;
	for (int32 EdgeIndex = 0; EdgeIndex < EdgeKeys.Num(); ++EdgeIndex)
	{
		const FEdgeKey Edge = EdgeKeys[EdgeIndex];
		const uint32 AOffset = EdgeWriteOffsets[Edge.A]++;
		const uint32 BOffset = EdgeWriteOffsets[Edge.B]++;
		OutTopology.VertexEdgeConstraintIndices[AOffset] = EdgeIndex;
		OutTopology.VertexEdgeCorners[AOffset] = 0;
		OutTopology.VertexEdgeConstraintIndices[BOffset] = EdgeIndex;
		OutTopology.VertexEdgeCorners[BOffset] = 1;
	}
	for (int32 TetIndex = 0; TetIndex < Mesh.Tetrahedra.Num(); ++TetIndex)
	{
		const FSoftBodyTetrahedron Tet = Mesh.Tetrahedra[TetIndex];
		const int32 Vertices[4] = { Tet.A, Tet.B, Tet.C, Tet.D };
		for (uint32 Corner = 0; Corner < 4; ++Corner)
		{
			const uint32 Offset = TetWriteOffsets[Vertices[Corner]]++;
			OutTopology.VertexTetrahedronConstraintIndices[Offset] = TetIndex;
			OutTopology.VertexTetrahedronCorners[Offset] = Corner;
		}
	}
	return OutTopology.IsValid(VertexCount, OutFailureReason);
}
