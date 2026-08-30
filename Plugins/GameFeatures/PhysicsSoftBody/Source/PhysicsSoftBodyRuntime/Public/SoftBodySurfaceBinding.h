#pragma once

#include "CoreMinimal.h"
#include "SoftBodyTetrahedralMesh.h"

struct FSoftBodySurfaceBindingVertex
{
	int32 CageVertexIndices[8] = { INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE };
	float Weights[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
};

struct PHYSICSSOFTBODYRUNTIME_API FSoftBodySurfaceBinding
{
	int32 CageVertexCount = 0;
	TArray<FVector3f> RestSurfacePositions;
	TArray<FSoftBodySurfaceBindingVertex> Vertices;

	bool IsValid(FString* OutFailureReason = nullptr) const;
	bool Apply(TConstArrayView<FVector3f> CagePositions, TArray<FVector3f>& OutSurfacePositions, FString* OutFailureReason = nullptr) const;
	double CalculateMaximumRestReconstructionErrorCm(TConstArrayView<FVector3f> CageRestPositions) const;
};

class PHYSICSSOFTBODYRUNTIME_API FSoftBodySurfaceBindingBuilder
{
public:
	/**
	 * 将任意表面点绑定到规则四面体网格共享的六面体顶点。
	 * 这是保守的三线性 Cage 绑定，不宣称是精确四面体重心坐标。
	 */
	static bool BuildTrilinearGridBinding(
		const FSoftBodyTetGridSettings& GridSettings,
		TConstArrayView<FVector3f> SurfaceRestPositions,
		FSoftBodySurfaceBinding& OutBinding,
		FString* OutFailureReason = nullptr);
};
