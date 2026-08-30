#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothModalSubspace.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothModalSubspaceTest,
	"PhysicsResearch.ContactCloth.ModalSubspaceReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothModalSubspaceTest::RunTest(const FString& Parameters)
{
	constexpr int32 Columns = 20;
	constexpr int32 Rows = 15;
	constexpr int32 ModeCount = 12;
	FContactClothModalBasis Basis;
	TestTrue(TEXT("固定顶边低频模态基构建成功"), FContactClothModalSubspace::BuildPinnedGridBasis(Columns, Rows, ModeCount, Basis));
	TestEqual(TEXT("模态数量符合预算"), Basis.ModeCount, ModeCount);
	TestEqual(TEXT("模态粒子数量符合网格"), Basis.ParticleCount, Columns * Rows);

	double MaximumOrthonormalityError = 0.0;
	for (int32 ModeA = 0; ModeA < Basis.ModeCount; ++ModeA)
	{
		for (int32 ModeB = 0; ModeB < Basis.ModeCount; ++ModeB)
		{
			double Dot = 0.0;
			for (int32 Particle = 0; Particle < Basis.ParticleCount; ++Particle)
			{
				Dot += Basis.GetValue(Particle, ModeA) * Basis.GetValue(Particle, ModeB);
			}
			const double Expected = ModeA == ModeB ? 1.0 : 0.0;
			MaximumOrthonormalityError = FMath::Max(MaximumOrthonormalityError, FMath::Abs(Dot - Expected));
		}
	}
	TestTrue(TEXT("模态基满足数值正交归一"), MaximumOrthonormalityError < 1.e-10);

	bool bPinnedRowZero = true;
	for (int32 Mode = 0; Mode < Basis.ModeCount; ++Mode)
	{
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			bPinnedRowZero &= FMath::Abs(Basis.GetValue(Column, Mode)) < 1.e-12;
		}
	}
	TestTrue(TEXT("所有低频模态严格保持顶边固定"), bPinnedRowZero);

	TArray<double> SmoothSignal;
	TArray<double> CheckerboardSignal;
	SmoothSignal.SetNumUninitialized(Basis.ParticleCount);
	CheckerboardSignal.SetNumUninitialized(Basis.ParticleCount);
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			const int32 Index = Row * Columns + Column;
			SmoothSignal[Index] = FMath::Sin(0.5 * PI * Row / Rows) * FMath::Cos(PI * (Column + 0.5) / Columns);
			CheckerboardSignal[Index] = Row == 0 ? 0.0 : ((Row + Column) % 2 == 0 ? 1.0 : -1.0);
		}
	}

	TArray<double> SmoothCoordinates;
	TArray<double> SmoothReconstruction;
	TArray<double> CheckerCoordinates;
	TArray<double> CheckerReconstruction;
	TestTrue(TEXT("平滑位移投影成功"), FContactClothModalSubspace::Project(Basis, SmoothSignal, SmoothCoordinates));
	TestTrue(TEXT("平滑位移重建成功"), FContactClothModalSubspace::Reconstruct(Basis, SmoothCoordinates, SmoothReconstruction));
	TestTrue(TEXT("高频位移投影成功"), FContactClothModalSubspace::Project(Basis, CheckerboardSignal, CheckerCoordinates));
	TestTrue(TEXT("高频位移重建成功"), FContactClothModalSubspace::Reconstruct(Basis, CheckerCoordinates, CheckerReconstruction));
	const double SmoothRetention = FContactClothModalSubspace::MeasureRelativeRetainedEnergy(SmoothSignal, SmoothReconstruction);
	const double CheckerRetention = FContactClothModalSubspace::MeasureRelativeRetainedEnergy(CheckerboardSignal, CheckerReconstruction);
	TestTrue(TEXT("低频子空间保留平滑形变"), SmoothRetention > 0.99);
	TestTrue(TEXT("低频子空间拒绝棋盘格高频形变"), CheckerRetention < 0.05);
	TestTrue(TEXT("低频与高频能量选择性显著"), SmoothRetention > CheckerRetention + 0.9);

	FContactClothReducedSystem ReducedSystem;
	TestTrue(TEXT("缩减全局 SPD 系统组装成功"), FContactClothModalSubspace::BuildReducedGridSystem(Basis, Columns, Rows, 1.0, 18.0, ReducedSystem));
	bool bSymmetric = true;
	for (int32 Row = 0; Row < ReducedSystem.ModeCount; ++Row)
	{
		for (int32 Column = 0; Column < ReducedSystem.ModeCount; ++Column)
		{
			bSymmetric &= FMath::Abs(ReducedSystem.Matrix[Row * ReducedSystem.ModeCount + Column]
				- ReducedSystem.Matrix[Column * ReducedSystem.ModeCount + Row]) < 1.e-12;
		}
	}
	TestTrue(TEXT("缩减全局矩阵严格对称"), bSymmetric);

	TArray<double> KnownCoordinates;
	KnownCoordinates.SetNumUninitialized(Basis.ModeCount);
	for (int32 Mode = 0; Mode < Basis.ModeCount; ++Mode)
	{
		KnownCoordinates[Mode] = FMath::Sin(0.37 * (Mode + 1)) / (Mode + 1);
	}
	TArray<double> ReducedRhs;
	ReducedRhs.SetNumZeroed(Basis.ModeCount);
	for (int32 Row = 0; Row < Basis.ModeCount; ++Row)
	{
		for (int32 Column = 0; Column < Basis.ModeCount; ++Column)
		{
			ReducedRhs[Row] += ReducedSystem.Matrix[Row * Basis.ModeCount + Column] * KnownCoordinates[Column];
		}
	}
	TArray<double> ParticleRhs;
	TestTrue(TEXT("缩减右端项回到粒子空间"), FContactClothModalSubspace::Reconstruct(Basis, ReducedRhs, ParticleRhs));
	TArray<double> SolvedCoordinates;
	TArray<double> SolvedParticles;
	TestTrue(TEXT("缩减全局系统 Cholesky 求解成功"), FContactClothModalSubspace::SolveReducedSystem(Basis, ReducedSystem, ParticleRhs, SolvedCoordinates, SolvedParticles));
	double MaximumCoordinateError = 0.0;
	for (int32 Mode = 0; Mode < Basis.ModeCount; ++Mode)
	{
		MaximumCoordinateError = FMath::Max(MaximumCoordinateError, FMath::Abs(SolvedCoordinates[Mode] - KnownCoordinates[Mode]));
	}
	TestTrue(TEXT("缩减系统恢复已知模态解"), MaximumCoordinateError < 1.e-10);
	TArray<double> InverseMatrix;
	TestTrue(TEXT("缩减系统逆矩阵预计算成功"), FContactClothModalSubspace::InvertReducedSystem(ReducedSystem, InverseMatrix));
	double MaximumInverseError = 0.0;
	for (int32 Row = 0; Row < Basis.ModeCount; ++Row)
	{
		for (int32 Column = 0; Column < Basis.ModeCount; ++Column)
		{
			double Product = 0.0;
			for (int32 Inner = 0; Inner < Basis.ModeCount; ++Inner)
			{
				Product += ReducedSystem.Matrix[Row * Basis.ModeCount + Inner] * InverseMatrix[Inner * Basis.ModeCount + Column];
			}
			MaximumInverseError = FMath::Max(MaximumInverseError, FMath::Abs(Product - (Row == Column ? 1.0 : 0.0)));
		}
	}
	TestTrue(TEXT("预计算逆矩阵满足 A·A^-1=I"), MaximumInverseError < 1.e-10);
	AddInfo(FString::Printf(
		TEXT("Modal subspace reference: particles=%d modes=%d orthonormality_error=%.12g smooth_retention=%.9f checker_retention=%.9f reduced_coordinate_error=%.12g inverse_error=%.12g"),
		Basis.ParticleCount,
		Basis.ModeCount,
		MaximumOrthonormalityError,
		SmoothRetention,
		CheckerRetention,
		MaximumCoordinateError,
		MaximumInverseError));
	return true;
}

#endif
