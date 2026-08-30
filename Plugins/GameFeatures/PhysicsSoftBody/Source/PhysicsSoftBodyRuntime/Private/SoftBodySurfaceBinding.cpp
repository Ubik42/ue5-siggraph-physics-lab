#include "SoftBodySurfaceBinding.h"

namespace
{
void SetBindingFailure(FString* OutFailureReason, const FString& FailureReason)
{
	if (OutFailureReason) *OutFailureReason = FailureReason;
}

bool IsFinite(const FVector3f& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
}
}

bool FSoftBodySurfaceBinding::IsValid(FString* OutFailureReason) const
{
	if (CageVertexCount <= 0 || Vertices.IsEmpty() || RestSurfacePositions.Num() != Vertices.Num())
	{
		SetBindingFailure(OutFailureReason, TEXT("表面绑定数组不完整"));
		return false;
	}
	for (int32 SurfaceVertex = 0; SurfaceVertex < Vertices.Num(); ++SurfaceVertex)
	{
		if (!IsFinite(RestSurfacePositions[SurfaceVertex]))
		{
			SetBindingFailure(OutFailureReason, TEXT("表面静止点不是有限值"));
			return false;
		}
		double WeightSum = 0.0;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const int32 CageVertex = Vertices[SurfaceVertex].CageVertexIndices[Corner];
			const float Weight = Vertices[SurfaceVertex].Weights[Corner];
			if (CageVertex < 0 || CageVertex >= CageVertexCount || !FMath::IsFinite(Weight) || Weight < 0.0f)
			{
				SetBindingFailure(OutFailureReason, TEXT("表面绑定索引或权重无效"));
				return false;
			}
			WeightSum += Weight;
		}
		if (!FMath::IsNearlyEqual(WeightSum, 1.0, 1.e-5))
		{
			SetBindingFailure(OutFailureReason, TEXT("表面绑定权重和不为 1"));
			return false;
		}
	}
	return true;
}

bool FSoftBodySurfaceBinding::Apply(
	TConstArrayView<FVector3f> CagePositions,
	TArray<FVector3f>& OutSurfacePositions,
	FString* OutFailureReason) const
{
	if (!IsValid(OutFailureReason) || CagePositions.Num() != CageVertexCount)
	{
		SetBindingFailure(OutFailureReason, TEXT("绑定与 Cage 顶点数不一致"));
		return false;
	}
	OutSurfacePositions.SetNumUninitialized(Vertices.Num());
	for (int32 SurfaceVertex = 0; SurfaceVertex < Vertices.Num(); ++SurfaceVertex)
	{
		FVector3f Position = FVector3f::ZeroVector;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			Position += Vertices[SurfaceVertex].Weights[Corner]
				* CagePositions[Vertices[SurfaceVertex].CageVertexIndices[Corner]];
		}
		if (!IsFinite(Position))
		{
			SetBindingFailure(OutFailureReason, TEXT("表面绑定产生非有限位置"));
			return false;
		}
		OutSurfacePositions[SurfaceVertex] = Position;
	}
	return true;
}

double FSoftBodySurfaceBinding::CalculateMaximumRestReconstructionErrorCm(TConstArrayView<FVector3f> CageRestPositions) const
{
	TArray<FVector3f> Reconstructed;
	if (!Apply(CageRestPositions, Reconstructed)) return TNumericLimits<double>::Max();
	double MaximumError = 0.0;
	for (int32 Vertex = 0; Vertex < Reconstructed.Num(); ++Vertex)
	{
		MaximumError = FMath::Max(MaximumError, static_cast<double>((Reconstructed[Vertex] - RestSurfacePositions[Vertex]).Length()));
	}
	return MaximumError;
}

bool FSoftBodySurfaceBindingBuilder::BuildTrilinearGridBinding(
	const FSoftBodyTetGridSettings& GridSettings,
	TConstArrayView<FVector3f> SurfaceRestPositions,
	FSoftBodySurfaceBinding& OutBinding,
	FString* OutFailureReason)
{
	OutBinding = FSoftBodySurfaceBinding();
	if (SurfaceRestPositions.IsEmpty() || GridSettings.CellCounts.X <= 0
		|| GridSettings.CellCounts.Y <= 0 || GridSettings.CellCounts.Z <= 0
		|| !IsFinite(GridSettings.OriginCm) || !IsFinite(GridSettings.CellSizeCm)
		|| GridSettings.CellSizeCm.X <= 0.0f || GridSettings.CellSizeCm.Y <= 0.0f || GridSettings.CellSizeCm.Z <= 0.0f)
	{
		SetBindingFailure(OutFailureReason, TEXT("表面绑定输入网格或顶点无效"));
		return false;
	}
	const int32 SizeX = GridSettings.CellCounts.X + 1;
	const int32 SizeY = GridSettings.CellCounts.Y + 1;
	const int32 SizeZ = GridSettings.CellCounts.Z + 1;
	const FVector3f Maximum = GridSettings.OriginCm + FVector3f(
		GridSettings.CellCounts.X * GridSettings.CellSizeCm.X,
		GridSettings.CellCounts.Y * GridSettings.CellSizeCm.Y,
		GridSettings.CellCounts.Z * GridSettings.CellSizeCm.Z);
	const float Tolerance = FMath::Max3(GridSettings.CellSizeCm.X, GridSettings.CellSizeCm.Y, GridSettings.CellSizeCm.Z) * 1.e-3f;
	const auto CageIndex = [SizeX, SizeY](int32 X, int32 Y, int32 Z)
	{
		return X + SizeX * (Y + SizeY * Z);
	};
	OutBinding.CageVertexCount = SizeX * SizeY * SizeZ;
	OutBinding.RestSurfacePositions.Append(SurfaceRestPositions.GetData(), SurfaceRestPositions.Num());
	OutBinding.Vertices.SetNumUninitialized(SurfaceRestPositions.Num());
	for (int32 SurfaceVertex = 0; SurfaceVertex < SurfaceRestPositions.Num(); ++SurfaceVertex)
	{
		const FVector3f Position = SurfaceRestPositions[SurfaceVertex];
		if (!IsFinite(Position)
			|| Position.X < GridSettings.OriginCm.X - Tolerance || Position.Y < GridSettings.OriginCm.Y - Tolerance || Position.Z < GridSettings.OriginCm.Z - Tolerance
			|| Position.X > Maximum.X + Tolerance || Position.Y > Maximum.Y + Tolerance || Position.Z > Maximum.Z + Tolerance)
		{
			SetBindingFailure(OutFailureReason, FString::Printf(TEXT("表面顶点 %d 超出 Cage 范围"), SurfaceVertex));
			return false;
		}
		const FVector3f GridCoordinate(
			FMath::Clamp((Position.X - GridSettings.OriginCm.X) / GridSettings.CellSizeCm.X, 0.0f, static_cast<float>(GridSettings.CellCounts.X)),
			FMath::Clamp((Position.Y - GridSettings.OriginCm.Y) / GridSettings.CellSizeCm.Y, 0.0f, static_cast<float>(GridSettings.CellCounts.Y)),
			FMath::Clamp((Position.Z - GridSettings.OriginCm.Z) / GridSettings.CellSizeCm.Z, 0.0f, static_cast<float>(GridSettings.CellCounts.Z)));
		const FIntVector Cell(
			FMath::Min(FMath::FloorToInt(GridCoordinate.X), GridSettings.CellCounts.X - 1),
			FMath::Min(FMath::FloorToInt(GridCoordinate.Y), GridSettings.CellCounts.Y - 1),
			FMath::Min(FMath::FloorToInt(GridCoordinate.Z), GridSettings.CellCounts.Z - 1));
		const FVector3f Local = GridCoordinate - FVector3f(
			static_cast<float>(Cell.X), static_cast<float>(Cell.Y), static_cast<float>(Cell.Z));
		FSoftBodySurfaceBindingVertex& BindingVertex = OutBinding.Vertices[SurfaceVertex];
		int32 Corner = 0;
		for (int32 Z = 0; Z <= 1; ++Z)
		{
			for (int32 Y = 0; Y <= 1; ++Y)
			{
				for (int32 X = 0; X <= 1; ++X)
				{
					BindingVertex.CageVertexIndices[Corner] = CageIndex(Cell.X + X, Cell.Y + Y, Cell.Z + Z);
					BindingVertex.Weights[Corner] = (X ? Local.X : 1.0f - Local.X)
						* (Y ? Local.Y : 1.0f - Local.Y) * (Z ? Local.Z : 1.0f - Local.Z);
					++Corner;
				}
			}
		}
	}
	return OutBinding.IsValid(OutFailureReason);
}
