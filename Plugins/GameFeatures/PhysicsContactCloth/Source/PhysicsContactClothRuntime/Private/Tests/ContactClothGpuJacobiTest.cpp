#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothGpuJacobi.h"
#include "Misc/AutomationTest.h"
#include "RHI.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuJacobiTest,
	"PhysicsResearch.ContactCloth.GpuJacobiDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuJacobiTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过实际 Compute Dispatch；D3D12 验证报告单独生成。"));
		return true;
	}

	FContactClothGpuJacobiSettings Settings;
	Settings.Columns = 16;
	Settings.Rows = 12;
	Settings.SpacingCm = 12.0f;
	Settings.Iterations = 96;
	Settings.Relaxation = 1.0f;
	Settings.SphereRadiusCm = 0.0f;
	Settings.CapsuleStart = FVector3f(0.0f, 0.0f, -45.0f);
	Settings.CapsuleEnd = FVector3f(0.0f, 0.0f, -95.0f);
	Settings.CapsuleRadiusCm = 28.0f;

	TArray<FVector3f> InputPositions;
	InputPositions.Reserve(Settings.Columns * Settings.Rows);
	for (int32 Row = 0; Row < Settings.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Settings.Columns; ++Column)
		{
			InputPositions.Emplace(Column * Settings.SpacingCm, 0.0f, -Row * Settings.SpacingCm);
		}
	}
	const int32 PerturbedIndex = (Settings.Rows / 2) * Settings.Columns + Settings.Columns / 2;
	InputPositions[PerturbedIndex].X += 60.0f;
	InputPositions[PerturbedIndex].Y += 25.0f;

	const FContactClothGpuJacobiResult Result = FContactClothGpuJacobi::RunBlocking(InputPositions, Settings);
	AddInfo(FString::Printf(
		TEXT("D3D GPU Jacobi: particles=%d iterations=%d input_max_edge_error_cm=%.6f output_max_edge_error_cm=%.6f input_capsule_penetration_cm=%.6f output_capsule_penetration_cm=%.6f blocking_wall_ms=%.3f"),
		InputPositions.Num(),
		Settings.Iterations,
		Result.InputMaximumEdgeErrorCm,
		Result.OutputMaximumEdgeErrorCm,
		Result.InputMaximumCapsulePenetrationCm,
		Result.OutputMaximumCapsulePenetrationCm,
		Result.BlockingWallMilliseconds));
	TestTrue(TEXT("Compute Shader 已派发"), Result.bDispatched);
	TestTrue(TEXT("GPU 结果成功读回"), Result.bReadbackSucceeded);
	TestTrue(TEXT("无派发失败原因"), Result.FailureReason.IsEmpty());
	TestEqual(TEXT("GPU 输出粒子数"), Result.Positions.Num(), InputPositions.Num());
	TestTrue(TEXT("输入误差为正"), Result.InputMaximumEdgeErrorCm > 1.0);
	TestTrue(TEXT("GPU Jacobi 降低结构边误差"), Result.OutputMaximumEdgeErrorCm < Result.InputMaximumEdgeErrorCm * 0.8);
	TestTrue(TEXT("输出误差为有限数"), FMath::IsFinite(Result.OutputMaximumEdgeErrorCm));
	TestTrue(TEXT("输入网格与胶囊发生穿透"), Result.InputMaximumCapsulePenetrationCm > 1.0);
	TestTrue(TEXT("GPU 胶囊投影消除剩余穿透"), Result.OutputMaximumCapsulePenetrationCm < 1.e-3);

	bool bPinnedRowPreserved = Result.Positions.Num() == InputPositions.Num();
	for (int32 Column = 0; bPinnedRowPreserved && Column < Settings.Columns; ++Column)
	{
		bPinnedRowPreserved = Result.Positions[Column].Equals(InputPositions[Column], 1.e-4f);
	}
	TestTrue(TEXT("GPU 固定顶边保持不动"), bPinnedRowPreserved);
	return true;
}

#endif
