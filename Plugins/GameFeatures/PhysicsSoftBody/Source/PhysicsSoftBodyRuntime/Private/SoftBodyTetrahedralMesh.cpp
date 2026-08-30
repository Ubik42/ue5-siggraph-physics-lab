#include "SoftBodyTetrahedralMesh.h"

namespace
{
struct FSoftBodyTriangleKey
{
	int32 A = INDEX_NONE;
	int32 B = INDEX_NONE;
	int32 C = INDEX_NONE;

	explicit FSoftBodyTriangleKey(const FIntVector& Triangle)
	{
		int32 Values[3] = { Triangle.X, Triangle.Y, Triangle.Z };
		if (Values[0] > Values[1]) Swap(Values[0], Values[1]);
		if (Values[1] > Values[2]) Swap(Values[1], Values[2]);
		if (Values[0] > Values[1]) Swap(Values[0], Values[1]);
		A = Values[0];
		B = Values[1];
		C = Values[2];
	}

	bool operator==(const FSoftBodyTriangleKey& Other) const
	{
		return A == Other.A && B == Other.B && C == Other.C;
	}

	friend uint32 GetTypeHash(const FSoftBodyTriangleKey& Key)
	{
		return HashCombine(HashCombine(GetTypeHash(Key.A), GetTypeHash(Key.B)), GetTypeHash(Key.C));
	}
};

struct FSoftBodyTriangleRecord
{
	FIntVector Triangle = FIntVector::ZeroValue;
	int32 Count = 0;
};

double SignedVolumeCm3(
	const FVector3f& A,
	const FVector3f& B,
	const FVector3f& C,
	const FVector3f& D)
{
	const FVector3d AB = FVector3d(B - A);
	const FVector3d AC = FVector3d(C - A);
	const FVector3d AD = FVector3d(D - A);
	return FVector3d::DotProduct(AB, FVector3d::CrossProduct(AC, AD)) / 6.0;
}

void SetFailure(FString* OutFailureReason, const FString& FailureReason)
{
	if (OutFailureReason)
	{
		*OutFailureReason = FailureReason;
	}
}
}

double FSoftBodyTetMesh::CalculateTotalRestVolumeCm3() const
{
	double Total = 0.0;
	for (float Volume : RestVolumesCm3)
	{
		Total += static_cast<double>(Volume);
	}
	return Total;
}

int32 FSoftBodyTetMesh::CountFixedVertices() const
{
	int32 Count = 0;
	for (float InverseMass : InverseMasses)
	{
		Count += InverseMass <= 0.0f ? 1 : 0;
	}
	return Count;
}

bool FSoftBodyTetMesh::IsValid(FString* OutFailureReason) const
{
	if (RestPositions.Num() < 4 || Tetrahedra.IsEmpty())
	{
		SetFailure(OutFailureReason, TEXT("四面体网格没有足够的顶点或单元"));
		return false;
	}
	if (RestVolumesCm3.Num() != Tetrahedra.Num() || MaterialIds.Num() != Tetrahedra.Num())
	{
		SetFailure(OutFailureReason, TEXT("四面体体积或材料数组与单元数不一致"));
		return false;
	}
	if (InverseMasses.Num() != RestPositions.Num() || SurfaceTriangles.IsEmpty())
	{
		SetFailure(OutFailureReason, TEXT("逆质量或边界表面合同不完整"));
		return false;
	}
	for (int32 TetIndex = 0; TetIndex < Tetrahedra.Num(); ++TetIndex)
	{
		const FSoftBodyTetrahedron& Tet = Tetrahedra[TetIndex];
		if (Tet.A < 0 || Tet.B < 0 || Tet.C < 0 || Tet.D < 0
			|| Tet.A >= RestPositions.Num() || Tet.B >= RestPositions.Num()
			|| Tet.C >= RestPositions.Num() || Tet.D >= RestPositions.Num()
			|| Tet.A == Tet.B || Tet.A == Tet.C || Tet.A == Tet.D
			|| Tet.B == Tet.C || Tet.B == Tet.D || Tet.C == Tet.D)
		{
			SetFailure(OutFailureReason, FString::Printf(TEXT("四面体 %d 的索引无效或重复"), TetIndex));
			return false;
		}
		if (!FMath::IsFinite(RestVolumesCm3[TetIndex]) || RestVolumesCm3[TetIndex] <= 0.0f)
		{
			SetFailure(OutFailureReason, FString::Printf(TEXT("四面体 %d 的静止体积不是正有限值"), TetIndex));
			return false;
		}
	}
	for (int32 VertexIndex = 0; VertexIndex < RestPositions.Num(); ++VertexIndex)
	{
		if (RestPositions[VertexIndex].ContainsNaN()
			|| !FMath::IsFinite(InverseMasses[VertexIndex]) || InverseMasses[VertexIndex] < 0.0f)
		{
			SetFailure(OutFailureReason, FString::Printf(TEXT("顶点 %d 的位置或逆质量无效"), VertexIndex));
			return false;
		}
	}
	return true;
}

bool FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(
	const FSoftBodyTetGridSettings& Settings,
	FSoftBodyTetMesh& OutMesh,
	FString* OutFailureReason)
{
	OutMesh = FSoftBodyTetMesh();
	if (Settings.CellCounts.X <= 0 || Settings.CellCounts.Y <= 0 || Settings.CellCounts.Z <= 0)
	{
		SetFailure(OutFailureReason, TEXT("三轴单元数必须大于零"));
		return false;
	}
	if (Settings.CellSizeCm.ContainsNaN() || Settings.CellSizeCm.X <= 0.0f
		|| Settings.CellSizeCm.Y <= 0.0f || Settings.CellSizeCm.Z <= 0.0f)
	{
		SetFailure(OutFailureReason, TEXT("三轴单元尺寸必须为正有限值"));
		return false;
	}
	if (Settings.OriginCm.ContainsNaN())
	{
		SetFailure(OutFailureReason, TEXT("网格原点必须为有限值"));
		return false;
	}
	if (!FMath::IsFinite(Settings.DensityKgPerCubicCm) || Settings.DensityKgPerCubicCm <= 0.0f)
	{
		SetFailure(OutFailureReason, TEXT("密度必须为正有限值"));
		return false;
	}
	if (Settings.UniformMaterialId > 1)
	{
		SetFailure(OutFailureReason, TEXT("当前标准基准只接受材料编号 0 或 1"));
		return false;
	}

	const int32 SizeX = Settings.CellCounts.X + 1;
	const int32 SizeY = Settings.CellCounts.Y + 1;
	const int32 SizeZ = Settings.CellCounts.Z + 1;
	const int32 VertexCount = SizeX * SizeY * SizeZ;
	OutMesh.RestPositions.Reserve(VertexCount);
	for (int32 Z = 0; Z < SizeZ; ++Z)
	{
		for (int32 Y = 0; Y < SizeY; ++Y)
		{
			for (int32 X = 0; X < SizeX; ++X)
			{
				OutMesh.RestPositions.Add(Settings.OriginCm + FVector3f(
					X * Settings.CellSizeCm.X,
					Y * Settings.CellSizeCm.Y,
					Z * Settings.CellSizeCm.Z));
			}
		}
	}
	const auto VertexIndex = [SizeX, SizeY](int32 X, int32 Y, int32 Z)
	{
		return X + SizeX * (Y + SizeY * Z);
	};
	const float SplitX = Settings.CellCounts.X * Settings.CellSizeCm.X * 0.5f;
	const auto AddTet = [&OutMesh, &Settings, SplitX](int32 A, int32 B, int32 C, int32 D)
	{
		FSoftBodyTetrahedron Tet{ A, B, C, D };
		double Volume = SignedVolumeCm3(
			OutMesh.RestPositions[Tet.A], OutMesh.RestPositions[Tet.B],
			OutMesh.RestPositions[Tet.C], OutMesh.RestPositions[Tet.D]);
		if (Volume < 0.0)
		{
			Swap(Tet.B, Tet.C);
			Volume = -Volume;
		}
		OutMesh.Tetrahedra.Add(Tet);
		OutMesh.RestVolumesCm3.Add(static_cast<float>(Volume));
		const float CentroidX = 0.25f * (OutMesh.RestPositions[Tet.A].X + OutMesh.RestPositions[Tet.B].X
			+ OutMesh.RestPositions[Tet.C].X + OutMesh.RestPositions[Tet.D].X);
		OutMesh.MaterialIds.Add(Settings.bSplitMaterialAtHalfX
			? (CentroidX >= SplitX ? 1 : 0)
			: Settings.UniformMaterialId);
	};

	const int32 TetrahedronCount = Settings.CellCounts.X * Settings.CellCounts.Y * Settings.CellCounts.Z * 6;
	OutMesh.Tetrahedra.Reserve(TetrahedronCount);
	OutMesh.RestVolumesCm3.Reserve(TetrahedronCount);
	OutMesh.MaterialIds.Reserve(TetrahedronCount);
	for (int32 Z = 0; Z < Settings.CellCounts.Z; ++Z)
	{
		for (int32 Y = 0; Y < Settings.CellCounts.Y; ++Y)
		{
			for (int32 X = 0; X < Settings.CellCounts.X; ++X)
			{
				const int32 V000 = VertexIndex(X, Y, Z);
				const int32 V100 = VertexIndex(X + 1, Y, Z);
				const int32 V010 = VertexIndex(X, Y + 1, Z);
				const int32 V110 = VertexIndex(X + 1, Y + 1, Z);
				const int32 V001 = VertexIndex(X, Y, Z + 1);
				const int32 V101 = VertexIndex(X + 1, Y, Z + 1);
				const int32 V011 = VertexIndex(X, Y + 1, Z + 1);
				const int32 V111 = VertexIndex(X + 1, Y + 1, Z + 1);
				AddTet(V000, V100, V110, V111);
				AddTet(V000, V110, V010, V111);
				AddTet(V000, V010, V011, V111);
				AddTet(V000, V011, V001, V111);
				AddTet(V000, V001, V101, V111);
				AddTet(V000, V101, V100, V111);
			}
		}
	}

	TMap<FSoftBodyTriangleKey, FSoftBodyTriangleRecord> FaceRecords;
	const auto AddFace = [&FaceRecords](const FIntVector& Face)
	{
		FSoftBodyTriangleRecord& Record = FaceRecords.FindOrAdd(FSoftBodyTriangleKey(Face));
		if (Record.Count == 0)
		{
			Record.Triangle = Face;
		}
		++Record.Count;
	};
	for (const FSoftBodyTetrahedron& Tet : OutMesh.Tetrahedra)
	{
		AddFace(FIntVector(Tet.A, Tet.C, Tet.B));
		AddFace(FIntVector(Tet.A, Tet.B, Tet.D));
		AddFace(FIntVector(Tet.B, Tet.C, Tet.D));
		AddFace(FIntVector(Tet.C, Tet.A, Tet.D));
	}
	for (const TPair<FSoftBodyTriangleKey, FSoftBodyTriangleRecord>& Pair : FaceRecords)
	{
		if (Pair.Value.Count == 1)
		{
			OutMesh.SurfaceTriangles.Add(Pair.Value.Triangle);
		}
		else if (Pair.Value.Count != 2)
		{
			SetFailure(OutFailureReason, TEXT("检测到非流形四面体面"));
			OutMesh = FSoftBodyTetMesh();
			return false;
		}
	}

	TArray<double> VertexMasses;
	VertexMasses.Init(0.0, OutMesh.RestPositions.Num());
	for (int32 TetIndex = 0; TetIndex < OutMesh.Tetrahedra.Num(); ++TetIndex)
	{
		const double QuarterMass = static_cast<double>(OutMesh.RestVolumesCm3[TetIndex])
			* Settings.DensityKgPerCubicCm * 0.25;
		const FSoftBodyTetrahedron& Tet = OutMesh.Tetrahedra[TetIndex];
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			VertexMasses[Tet[Corner]] += QuarterMass;
		}
	}
	OutMesh.InverseMasses.SetNumUninitialized(OutMesh.RestPositions.Num());
	for (int32 Vertex = 0; Vertex < OutMesh.RestPositions.Num(); ++Vertex)
	{
		const bool bFixed = Settings.bFixMinimumXFace
			&& FMath::IsNearlyEqual(OutMesh.RestPositions[Vertex].X, Settings.OriginCm.X);
		OutMesh.InverseMasses[Vertex] = bFixed ? 0.0f : static_cast<float>(1.0 / VertexMasses[Vertex]);
	}
	return OutMesh.IsValid(OutFailureReason);
}
