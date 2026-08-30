#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothCpuSolver.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothCpuSolverTest,
	"PhysicsResearch.ContactCloth.CpuProjectiveDynamics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothCpuSolverTest::RunTest(const FString& Parameters)
{
	FContactClothCpuSettings Settings;
	Settings.Columns = 6;
	Settings.Rows = 5;
	Settings.SpacingCm = 20.0;
	Settings.LocalGlobalIterations = 6;
	Settings.GlobalGaussSeidelSweeps = 12;

	FContactClothCpuSolver SolverA;
	FContactClothCpuSolver SolverB;
	const FVector3d Origin(0.0, 0.0, 150.0);
	SolverA.InitializeGrid(Settings, Origin);
	SolverB.InitializeGrid(Settings, Origin);

	TestEqual(TEXT("规则网格粒子数"), SolverA.GetParticles().Num(), Settings.Columns * Settings.Rows);
	TestTrue(TEXT("已创建拉伸、剪切和弯曲约束"), SolverA.GetConstraints().Num() > Settings.Columns * Settings.Rows);
	const FVector3d PinnedBefore = SolverA.GetParticles()[SolverA.GetParticleIndex(0, 0)].Position;
	const double FreeParticleHeightBefore = SolverA.GetParticles()[SolverA.GetParticleIndex(0, Settings.Rows - 1)].Position.Z;

	for (int32 StepIndex = 0; StepIndex < 10; ++StepIndex)
	{
		SolverA.Step(1.0 / 60.0, TConstArrayView<FContactClothSphereCollider>());
		SolverB.Step(1.0 / 60.0, TConstArrayView<FContactClothSphereCollider>());
	}
	const bool bMovedUnderGravity = SolverA.GetParticles()[SolverA.GetParticleIndex(0, Settings.Rows - 1)].Position.Z < FreeParticleHeightBefore;

	const FContactClothSphereCollider Sphere{FVector3d(0.0, 0.0, 70.0), 58.0};
	FContactClothCpuStepStats LastStats;
	bool bCollisionTriggered = false;
	for (int32 StepIndex = 0; StepIndex < 45; ++StepIndex)
	{
		LastStats = SolverA.Step(1.0 / 60.0, MakeArrayView(&Sphere, 1));
		SolverB.Step(1.0 / 60.0, MakeArrayView(&Sphere, 1));
		bCollisionTriggered |= LastStats.CollisionProjectionCount > 0 || LastStats.MaximumCollisionCorrectionCm > 0.0;
	}

	TestTrue(TEXT("固定点保持不动"), SolverA.GetParticles()[SolverA.GetParticleIndex(0, 0)].Position.Equals(PinnedBefore, 1.e-9));
	TestTrue(TEXT("自由粒子受重力下降"), bMovedUnderGravity);
	TestTrue(TEXT("球接触投影被触发"), bCollisionTriggered);
	TestTrue(TEXT("最终球穿透受控"), LastStats.MaximumRemainingPenetrationCm < 1.e-6);
	TestTrue(TEXT("拉伸误差为有限数"), FMath::IsFinite(LastStats.MaximumStretchRatioError));

	bool bDeterministic = SolverA.GetParticles().Num() == SolverB.GetParticles().Num();
	for (int32 Index = 0; bDeterministic && Index < SolverA.GetParticles().Num(); ++Index)
	{
		bDeterministic = SolverA.GetParticles()[Index].Position.Equals(SolverB.GetParticles()[Index].Position, 1.e-9);
	}
	TestTrue(TEXT("相同输入产生确定性结果"), bDeterministic);

	SolverA.Reset();
	TestTrue(TEXT("重置回静止形状"), SolverA.GetParticles()[SolverA.GetParticleIndex(0, Settings.Rows - 1)].Position.Equals(FVector3d(-50.0, 0.0, 70.0), 1.e-9));
	return true;
}

#endif
