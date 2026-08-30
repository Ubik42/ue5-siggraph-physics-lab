#pragma once

#include "CoreMinimal.h"

struct FSoftBodyTetrahedron
{
	int32 A = INDEX_NONE;
	int32 B = INDEX_NONE;
	int32 C = INDEX_NONE;
	int32 D = INDEX_NONE;

	int32 operator[](int32 Index) const
	{
		check(Index >= 0 && Index < 4);
		return Index == 0 ? A : Index == 1 ? B : Index == 2 ? C : D;
	}
};

struct FSoftBodyTetGridSettings
{
	FIntVector CellCounts = FIntVector(4, 2, 2);
	FVector3f CellSizeCm = FVector3f(10.0f, 10.0f, 10.0f);
	/** 规则网格的世界/局部最小角，不是中心点。 */
	FVector3f OriginCm = FVector3f::ZeroVector;
	float DensityKgPerCubicCm = 0.001f;
	bool bFixMinimumXFace = true;
	bool bSplitMaterialAtHalfX = true;
	uint8 UniformMaterialId = 0;
};

struct FSoftBodyTetMesh
{
	TArray<FVector3f> RestPositions;
	TArray<FSoftBodyTetrahedron> Tetrahedra;
	TArray<float> RestVolumesCm3;
	TArray<float> InverseMasses;
	TArray<uint8> MaterialIds;
	TArray<FIntVector> SurfaceTriangles;

	double CalculateTotalRestVolumeCm3() const;
	int32 CountFixedVertices() const;
	bool IsValid(FString* OutFailureReason = nullptr) const;
};

class PHYSICSSOFTBODYRUNTIME_API FSoftBodyTetrahedralMeshBuilder
{
public:
	static bool BuildRegularBlock(
		const FSoftBodyTetGridSettings& Settings,
		FSoftBodyTetMesh& OutMesh,
		FString* OutFailureReason = nullptr);
};
