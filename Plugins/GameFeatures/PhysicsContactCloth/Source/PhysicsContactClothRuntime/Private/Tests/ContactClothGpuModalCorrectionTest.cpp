#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothGpuJacobi.h"
#include "ContactClothModalSubspace.h"
#include "Misc/AutomationTest.h"
#include "RHI.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuModalCorrectionTest,
	"PhysicsResearch.ContactCloth.GpuReducedModalCorrection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuModalCorrectionTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过 GPU 缩减模态校正；D3D12 报告单独生成。"));
		return true;
	}

	constexpr int32 Columns = 20;
	constexpr int32 Rows = 15;
	constexpr int32 ModeCount = 12;
	constexpr int32 Steps = 90;
	TArray<FVector3f> InitialPositions;
	InitialPositions.Reserve(Columns * Rows);
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			InitialPositions.Emplace(Column * 12.0f, 0.0f, 220.0f - Row * 12.0f);
		}
	}

	FContactClothModalBasis Basis;
	FContactClothReducedSystem ReducedSystem;
	TArray<double> InverseSystem;
	TestTrue(TEXT("GPU 门使用的低频基构建成功"), FContactClothModalSubspace::BuildPinnedGridBasis(Columns, Rows, ModeCount, Basis));
	TestTrue(TEXT("GPU 门使用的缩减系统构建成功"), FContactClothModalSubspace::BuildReducedGridSystem(Basis, Columns, Rows, 1.0, 1.0, ReducedSystem));
	TestTrue(TEXT("GPU 门使用的缩减逆矩阵构建成功"), FContactClothModalSubspace::InvertReducedSystem(ReducedSystem, InverseSystem));

	auto RunSession = [&](bool bEnableModal, TArray<FVector3f>& OutPositions, FContactClothGpuRealtimeStats& OutStats)
	{
		FContactClothGpuRealtimeSettings Settings;
		Settings.Columns = Columns;
		Settings.Rows = Rows;
		Settings.SpacingCm = 12.0f;
		Settings.Iterations = 4;
		Settings.Relaxation = 0.9f;
		Settings.FixedDeltaSeconds = 1.0f / 60.0f;
		Settings.GravityCmPerSecondSquared = FVector3f(0.0f, 0.0f, -980.0f);
		Settings.VelocityDamping = 0.995f;
		Settings.ReadbackIntervalSteps = Steps;
		Settings.CollisionThicknessCm = 0.0f;
		if (bEnableModal)
		{
			Settings.ModalModeCount = ModeCount;
			Settings.ModalCorrectionStrength = 1.0f;
			Settings.ModalBasisValues.Reserve(Basis.Values.Num());
			for (double Value : Basis.Values)
			{
				Settings.ModalBasisValues.Add(static_cast<float>(Value));
			}
			Settings.ModalInverseSystemValues.Reserve(InverseSystem.Num());
			for (double Value : InverseSystem)
			{
				Settings.ModalInverseSystemValues.Add(static_cast<float>(Value));
			}
		}

		FContactClothGpuRealtimeSimulation Simulation;
		if (!Simulation.Initialize(InitialPositions, Settings))
		{
			return false;
		}
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			if (!Simulation.EnqueueStep())
			{
				Simulation.Shutdown();
				return false;
			}
		}
		Simulation.FlushAndWaitForReadbackForValidation();
		int64 ReadbackStep = 0;
		const bool bReadback = Simulation.ConsumeLatestPositions(OutPositions, ReadbackStep);
		OutStats = Simulation.GetStats();
		Simulation.Shutdown();
		return bReadback && ReadbackStep == Steps && OutPositions.Num() == InitialPositions.Num();
	};

	TArray<FVector3f> BaselinePositions;
	TArray<FVector3f> ModalPositions;
	FContactClothGpuRealtimeStats BaselineStats;
	FContactClothGpuRealtimeStats ModalStats;
	TestTrue(TEXT("无模态消融会话完成"), RunSession(false, BaselinePositions, BaselineStats));
	TestTrue(TEXT("缩减模态会话完成"), RunSession(true, ModalPositions, ModalStats));
	TestEqual(TEXT("无模态会话未执行模态 Pass"), BaselineStats.ModalCorrectionsExecuted, static_cast<int64>(0));
	TestEqual(TEXT("模态会话逐固定步执行缩减校正"), ModalStats.ModalCorrectionsExecuted, static_cast<int64>(Steps));

	TArray<double> BaselineVerticalDisplacement;
	TArray<double> ModalVerticalDisplacement;
	BaselineVerticalDisplacement.SetNumZeroed(InitialPositions.Num());
	ModalVerticalDisplacement.SetNumZeroed(InitialPositions.Num());
	for (int32 Index = 0; Index < InitialPositions.Num(); ++Index)
	{
		BaselineVerticalDisplacement[Index] = BaselinePositions.IsValidIndex(Index) ? BaselinePositions[Index].Z - InitialPositions[Index].Z : 0.0;
		ModalVerticalDisplacement[Index] = ModalPositions.IsValidIndex(Index) ? ModalPositions[Index].Z - InitialPositions[Index].Z : 0.0;
	}
	TArray<double> BaselineCoordinates;
	TArray<double> ModalCoordinates;
	TestTrue(TEXT("无模态结果可投影到低频空间"), FContactClothModalSubspace::Project(Basis, BaselineVerticalDisplacement, BaselineCoordinates));
	TestTrue(TEXT("模态结果可投影到低频空间"), FContactClothModalSubspace::Project(Basis, ModalVerticalDisplacement, ModalCoordinates));
	double BaselineLowFrequencyEnergy = 0.0;
	double ModalLowFrequencyEnergy = 0.0;
	for (int32 Mode = 0; Mode < ModeCount; ++Mode)
	{
		BaselineLowFrequencyEnergy += FMath::Square(BaselineCoordinates[Mode]);
		ModalLowFrequencyEnergy += FMath::Square(ModalCoordinates[Mode]);
	}
	const int32 BottomCenter = (Rows - 1) * Columns + Columns / 2;
	const double BaselineBottomDrop = InitialPositions[BottomCenter].Z - BaselinePositions[BottomCenter].Z;
	const double ModalBottomDrop = InitialPositions[BottomCenter].Z - ModalPositions[BottomCenter].Z;
	TestTrue(TEXT("缩减模态全局步降低低频结构位移能量"), ModalLowFrequencyEnergy < BaselineLowFrequencyEnergy * 0.8);
	TestTrue(TEXT("缩减模态全局步降低整体下垂"), ModalBottomDrop < BaselineBottomDrop * 0.9);
	TestTrue(TEXT("缩减模态全局步保留显著重力响应"), ModalBottomDrop > BaselineBottomDrop * 0.1);
	AddInfo(FString::Printf(
		TEXT("GPU reduced modal correction: particles=%d modes=%d steps=%d baseline_low_energy=%.9f modal_low_energy=%.9f baseline_bottom_drop_cm=%.6f modal_bottom_drop_cm=%.6f modal_passes=%lld"),
		InitialPositions.Num(),
		ModeCount,
		Steps,
		BaselineLowFrequencyEnergy,
		ModalLowFrequencyEnergy,
		BaselineBottomDrop,
		ModalBottomDrop,
		ModalStats.ModalCorrectionsExecuted));
	return true;
}

#endif
