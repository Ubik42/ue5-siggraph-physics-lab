#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothGpuJacobi.h"
#include "Misc/AutomationTest.h"
#include "RHI.h"

namespace ContactClothGpuBatchCollisionTest
{
double MeasureSpherePenetration(
	TConstArrayView<FVector3f> Positions,
	const FContactClothGpuSphere& Sphere,
	float ThicknessCm)
{
	double MaximumPenetration = 0.0;
	const float Radius = Sphere.RadiusCm + ThicknessCm;
	for (const FVector3f& Position : Positions)
	{
		MaximumPenetration = FMath::Max(
			MaximumPenetration,
			static_cast<double>(Radius - (Position - Sphere.Center).Length()));
	}
	return FMath::Max(MaximumPenetration, 0.0);
}

double MeasureAllPenetration(
	TConstArrayView<FVector3f> Positions,
	TConstArrayView<FContactClothGpuSphere> Spheres,
	TConstArrayView<FContactClothGpuCapsule> Capsules,
	float ThicknessCm)
{
	double MaximumPenetration = 0.0;
	for (const FContactClothGpuSphere& Sphere : Spheres)
	{
		MaximumPenetration = FMath::Max(MaximumPenetration, MeasureSpherePenetration(Positions, Sphere, ThicknessCm));
	}
	for (const FContactClothGpuCapsule& Capsule : Capsules)
	{
		MaximumPenetration = FMath::Max(
			MaximumPenetration,
			FContactClothGpuJacobi::MeasureMaximumCapsulePenetrationCm(
				Positions,
				Capsule.Start,
				Capsule.End,
				Capsule.RadiusCm + ThicknessCm));
	}
	return MaximumPenetration;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuBatchCollisionTest,
	"PhysicsResearch.ContactCloth.GpuBatchCollisionBuffers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuBatchCollisionTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过批量碰撞 Buffer 验证；D3D12 报告单独生成。"));
		return true;
	}

	FContactClothGpuJacobiSettings Settings;
	Settings.Columns = 12;
	Settings.Rows = 9;
	Settings.SpacingCm = 10.0f;
	Settings.Iterations = 1;
	Settings.Relaxation = 0.0f;
	Settings.CollisionThicknessCm = 1.5f;
	Settings.SphereRadiusCm = 0.0f;
	Settings.CapsuleRadiusCm = 0.0f;
	Settings.CollisionSpheres = {
		{FVector3f(25.0f, 0.0f, -25.0f), 11.0f}
	};
	Settings.CollisionCapsules = {
		{FVector3f(62.0f, 0.0f, -22.0f), FVector3f(62.0f, 0.0f, -48.0f), 9.0f},
		{FVector3f(96.0f, 0.0f, -54.0f), FVector3f(96.0f, 0.0f, -76.0f), 8.0f}
	};

	TArray<FVector3f> InputPositions;
	InputPositions.Reserve(Settings.Columns * Settings.Rows);
	for (int32 Row = 0; Row < Settings.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Settings.Columns; ++Column)
		{
			InputPositions.Emplace(Column * Settings.SpacingCm, 2.0f, -Row * Settings.SpacingCm);
		}
	}

	const double InputPenetration = ContactClothGpuBatchCollisionTest::MeasureAllPenetration(
		InputPositions,
		Settings.CollisionSpheres,
		Settings.CollisionCapsules,
		Settings.CollisionThicknessCm);
	const FContactClothGpuJacobiResult Result = FContactClothGpuJacobi::RunBlocking(InputPositions, Settings);
	const double OutputPenetration = ContactClothGpuBatchCollisionTest::MeasureAllPenetration(
		Result.Positions,
		Settings.CollisionSpheres,
		Settings.CollisionCapsules,
		Settings.CollisionThicknessCm);

	AddInfo(FString::Printf(
		TEXT("GPU batch collision: particles=%d spheres=%d capsules=%d input_max_penetration_cm=%.6f output_max_penetration_cm=%.6f blocking_wall_ms=%.3f"),
		InputPositions.Num(),
		Settings.CollisionSpheres.Num(),
		Settings.CollisionCapsules.Num(),
		InputPenetration,
		OutputPenetration,
		Result.BlockingWallMilliseconds));
	TestTrue(TEXT("批量碰撞 Compute Shader 已派发"), Result.bDispatched);
	TestTrue(TEXT("批量碰撞 GPU 结果成功读回"), Result.bReadbackSucceeded);
	TestTrue(TEXT("批量碰撞无派发失败原因"), Result.FailureReason.IsEmpty());
	TestEqual(TEXT("批量碰撞输出粒子数"), Result.Positions.Num(), InputPositions.Num());
	TestTrue(TEXT("输入同时覆盖多个碰撞区域"), InputPenetration > 1.0);
	TestTrue(TEXT("仅依靠批量 Structured Buffer 消除穿透"), OutputPenetration < 1.e-3);

	FContactClothGpuRealtimeSettings RealtimeSettings;
	RealtimeSettings.Columns = Settings.Columns;
	RealtimeSettings.Rows = Settings.Rows;
	RealtimeSettings.SpacingCm = Settings.SpacingCm;
	RealtimeSettings.Iterations = 1;
	RealtimeSettings.Relaxation = 0.0f;
	RealtimeSettings.CollisionThicknessCm = Settings.CollisionThicknessCm;
	RealtimeSettings.SphereRadiusCm = 0.0f;
	RealtimeSettings.CapsuleRadiusCm = 0.0f;
	RealtimeSettings.GravityCmPerSecondSquared = FVector3f::ZeroVector;
	RealtimeSettings.VelocityDamping = 1.0f;
	RealtimeSettings.ReadbackIntervalSteps = 1;
	FContactClothGpuRealtimeSimulation RealtimeSimulation;
	TestTrue(TEXT("实时会话在无碰撞体状态下初始化"), RealtimeSimulation.Initialize(InputPositions, RealtimeSettings));
	RealtimeSimulation.UpdateCollisionPrimitives(Settings.CollisionSpheres, Settings.CollisionCapsules);
	TestTrue(TEXT("姿态碰撞体更新后的固定步成功入队"), RealtimeSimulation.EnqueueStep());
	RealtimeSimulation.FlushAndWaitForReadbackForValidation();
	TArray<FVector3f> RealtimePositions;
	int64 RealtimeStep = 0;
	const bool bRealtimeReadback = RealtimeSimulation.ConsumeLatestPositions(RealtimePositions, RealtimeStep);
	const double RealtimeOutputPenetration = ContactClothGpuBatchCollisionTest::MeasureAllPenetration(
		RealtimePositions,
		Settings.CollisionSpheres,
		Settings.CollisionCapsules,
		Settings.CollisionThicknessCm);
	TestTrue(TEXT("实时姿态碰撞结果成功异步读回"), bRealtimeReadback);
	TestEqual(TEXT("实时姿态碰撞在下一固定步生效"), RealtimeStep, static_cast<int64>(1));
	TestTrue(TEXT("实时会话批量碰撞消除穿透"), RealtimeOutputPenetration < 1.e-3);
	AddInfo(FString::Printf(
		TEXT("GPU dynamic collision update: step=%lld output_max_penetration_cm=%.6f"),
		RealtimeStep,
		RealtimeOutputPenetration));
	RealtimeSimulation.Shutdown();
	return true;
}

#endif
