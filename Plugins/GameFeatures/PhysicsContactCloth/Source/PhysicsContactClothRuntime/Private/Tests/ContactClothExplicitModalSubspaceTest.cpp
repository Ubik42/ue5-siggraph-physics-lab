#include "ContactClothModalSubspace.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothExplicitModalSubspaceTest,
	"PhysicsResearch.ContactCloth.ExplicitTopology.ModalSubspaceReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothExplicitModalSubspaceTest::RunTest(const FString& Parameters)
{
	const TArray<FVector3f> Positions = {
		{0, 0, 0}, {40, 0, 5}, {85, 10, -2}, {5, 55, 4}, {47, 48, 13}, {92, 62, 1},
		{12, 105, -8}, {55, 112, 8}, {101, 118, 0}, {23, 162, 7}, {67, 171, -3}, {110, 180, 11}};
	const TArray<float> InverseMasses = {1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1};
	const TArray<FIntPoint> Edges = {
		{0,1},{1,2},{3,4},{4,5},{6,7},{7,8},{9,10},{10,11},
		{0,3},{1,4},{2,5},{3,6},{4,7},{5,8},{6,9},{7,10},{8,11},
		{0,4},{1,3},{1,5},{2,4},{3,7},{4,6},{4,8},{5,7},{6,10},{7,9},{7,11},{8,10}};
	FContactClothModalBasis Basis;
	TestTrue(TEXT("任意拓扑低频基构建成功"),
		FContactClothModalSubspace::BuildExplicitTopologyBasis(Positions, InverseMasses, Edges, 4, Basis));
	TestEqual(TEXT("粒子数保持真实拓扑"), Basis.ParticleCount, Positions.Num());
	TestEqual(TEXT("构建四个低频模态"), Basis.ModeCount, 4);

	double MaximumFixedSupport = 0.0;
	double MaximumOrthonormalityError = 0.0;
	for (int32 ModeA = 0; ModeA < Basis.ModeCount; ++ModeA)
	{
		for (int32 Particle = 0; Particle < Basis.ParticleCount; ++Particle)
		{
			if (InverseMasses[Particle] <= 0.0f)
				MaximumFixedSupport = FMath::Max(MaximumFixedSupport, FMath::Abs(Basis.GetValue(Particle, ModeA)));
		}
		for (int32 ModeB = 0; ModeB < Basis.ModeCount; ++ModeB)
		{
			double Dot = 0.0;
			for (int32 Particle = 0; Particle < Basis.ParticleCount; ++Particle)
				Dot += Basis.GetValue(Particle, ModeA) * Basis.GetValue(Particle, ModeB);
			MaximumOrthonormalityError = FMath::Max(MaximumOrthonormalityError,
				FMath::Abs(Dot - (ModeA == ModeB ? 1.0 : 0.0)));
		}
	}
	TestTrue(TEXT("非首位固定粒子在模态中严格为零"), MaximumFixedSupport < 1.e-12);
	TestTrue(TEXT("任意拓扑模态保持正交"), MaximumOrthonormalityError < 1.e-9);

	FContactClothReducedSystem System;
	TArray<double> InverseSystem;
	TestTrue(TEXT("任意边图缩减 SPD 系统构建成功"),
		FContactClothModalSubspace::BuildReducedExplicitTopologySystem(Basis, InverseMasses, Edges, 1.0, 0.5, System));
	TestTrue(TEXT("任意边图缩减系统可逆"), FContactClothModalSubspace::InvertReducedSystem(System, InverseSystem));
	double MaximumInverseError = 0.0;
	for (int32 Row = 0; Row < Basis.ModeCount; ++Row)
	{
		for (int32 Column = 0; Column < Basis.ModeCount; ++Column)
		{
			double Value = 0.0;
			for (int32 Inner = 0; Inner < Basis.ModeCount; ++Inner)
				Value += System.Matrix[Row * Basis.ModeCount + Inner] * InverseSystem[Inner * Basis.ModeCount + Column];
			MaximumInverseError = FMath::Max(MaximumInverseError, FMath::Abs(Value - (Row == Column ? 1.0 : 0.0)));
		}
	}
	TestTrue(TEXT("缩减系统逆矩阵恢复单位阵"), MaximumInverseError < 1.e-9);
	AddInfo(FString::Printf(TEXT("Explicit modal reference: particles=%d modes=%d fixed_support=%.3e orthogonality_error=%.3e inverse_error=%.3e"),
		Basis.ParticleCount, Basis.ModeCount, MaximumFixedSupport, MaximumOrthonormalityError, MaximumInverseError));
	return true;
}
