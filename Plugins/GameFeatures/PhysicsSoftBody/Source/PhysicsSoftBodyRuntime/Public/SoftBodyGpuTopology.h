#pragma once

#include "CoreMinimal.h"
#include "SoftBodyTetrahedralMesh.h"

/** GPU 约束求解使用的确定性结构化 Buffer/CSR 输入；不包含任何 RHI 资源。 */
struct FSoftBodyGpuTopology
{
	TArray<FIntPoint> EdgeVertices;
	TArray<float> EdgeRestLengthsCm;
	TArray<uint32> EdgeMaterialIds;

	TArray<uint32> VertexEdgeOffsets;
	TArray<uint32> VertexEdgeConstraintIndices;
	TArray<uint32> VertexEdgeCorners;

	TArray<FIntVector4> TetrahedronVertices;
	TArray<float> TetrahedronRestVolumesCm3;
	TArray<uint32> TetrahedronMaterialIds;

	TArray<uint32> VertexTetrahedronOffsets;
	TArray<uint32> VertexTetrahedronConstraintIndices;
	TArray<uint32> VertexTetrahedronCorners;

	bool IsValid(int32 VertexCount, FString* OutFailureReason = nullptr) const;
};

class PHYSICSSOFTBODYRUNTIME_API FSoftBodyGpuTopologyBuilder
{
public:
	static bool Build(const FSoftBodyTetMesh& Mesh, FSoftBodyGpuTopology& OutTopology, FString* OutFailureReason = nullptr);
};
