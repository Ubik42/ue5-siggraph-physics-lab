#if WITH_DEV_AUTOMATION_TESTS

#include "FlowMapScalarTransport2D.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlowMapScalarTransportAblationTest,
	"PhysicsResearch.FlowMap.EvolvingScalarTransportAblation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowMapScalarTransportAblationTest::RunTest(const FString& Parameters)
{
	FFlowMapScalarTransportSettings2D Settings;
	Settings.Grid.CellCounts = FIntPoint(32, 32);
	Settings.Grid.CellSizeCm = 5.0f;
	Settings.ParticlesPerCellAxis = 2;
	Settings.UniformVelocityCmPerSecond = FVector2f(20.0f, 10.0f);
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.LongMapResetSteps = 20;
	Settings.FieldCyclesX = 2;
	Settings.FieldCyclesY = 2;
	FString FailureReason;
	FFlowMapScalarTransportSimulation2D Pic;
	FFlowMapScalarTransportSimulation2D LongMapPic;
	FFlowMapScalarTransportSimulation2D LongMap;
	TestTrue(TEXT("短程 PIC 标量会话初始化"), Pic.Initialize(Settings, EFlowMapScalarTransportMode2D::ShortPic, &FailureReason));
	TestTrue(TEXT("长 Map 无梯度标量会话初始化"), LongMapPic.Initialize(Settings, EFlowMapScalarTransportMode2D::LongMapPic, &FailureReason));
	TestTrue(TEXT("长 Map 梯度标量会话初始化"), LongMap.Initialize(Settings, EFlowMapScalarTransportMode2D::LongMapGradient, &FailureReason));
	for (int32 Step = 0; Step < 120; ++Step)
	{
		TestTrue(TEXT("短程 PIC 固定步"), Pic.Step(&FailureReason));
		TestTrue(TEXT("长 Map 无梯度固定步"), LongMapPic.Step(&FailureReason));
		TestTrue(TEXT("长 Map 梯度固定步"), LongMap.Step(&FailureReason));
	}
	const FFlowMapScalarTransportMetrics2D& PicMetrics = Pic.GetMetrics();
	const FFlowMapScalarTransportMetrics2D& LongPicMetrics = LongMapPic.GetMetrics();
	const FFlowMapScalarTransportMetrics2D& LongMetrics = LongMap.GetMetrics();
	TestEqual(TEXT("两路径执行相同固定步"), PicMetrics.StepsExecuted, 120);
	TestEqual(TEXT("长 Map 路径执行相同固定步"), LongMetrics.StepsExecuted, 120);
	TestEqual(TEXT("长 Map 无梯度路径执行相同固定步"), LongPicMetrics.StepsExecuted, 120);
	TestEqual(TEXT("两路径粒子数相同"), PicMetrics.ParticleCount, LongMetrics.ParticleCount);
	TestEqual(TEXT("无梯度长 Map 粒子数相同"), LongPicMetrics.ParticleCount, LongMetrics.ParticleCount);
	TestEqual(TEXT("短程 PIC 每步重采样"), PicMetrics.MapResets, 120);
	TestEqual(TEXT("长 Map 每 20 步重置"), LongMetrics.MapResets, 6);
	TestEqual(TEXT("无梯度长 Map 使用相同重置周期"), LongPicMetrics.MapResets, 6);
	TestTrue(TEXT("长 Map 梯度路径 RMS 误差至少降低 50%"), LongMetrics.RmsError < PicMetrics.RmsError * 0.5);
	TestTrue(TEXT("相同长 Map 周期下梯度项继续降低 RMS"), LongMetrics.RmsError < LongPicMetrics.RmsError * 0.75);
	TestTrue(TEXT("长 Map 梯度路径保留更多标量幅值"), LongMetrics.ScalarAmplitude > PicMetrics.ScalarAmplitude + 0.1f);
	TestTrue(TEXT("长 Map 梯度路径相对 RMS 误差低于 20%"), LongMetrics.RelativeRmsError < 0.2);
	AddInfo(FString::Printf(TEXT("FlowMap evolving scalar steps=120 cells=32x32 particles=%d dt=%.9g velocity_cm_s=20,10 field_cycles=2,2 pic_resets=%d long_resets=%d pic_rms=%.9g long_pic_rms=%.9g long_gradient_rms=%.9g long_gradient_to_pic_rms=%.9g long_gradient_to_long_pic_rms=%.9g pic_relative_rms=%.9g long_gradient_relative_rms=%.9g pic_amplitude=%.9g long_pic_amplitude=%.9g long_gradient_amplitude=%.9g exact_amplitude=%.9g pic_energy=%.9g long_pic_energy=%.9g long_gradient_energy=%.9g exact_energy=%.9g"),
		PicMetrics.ParticleCount, Settings.FixedDeltaSeconds, PicMetrics.MapResets, LongMetrics.MapResets,
		PicMetrics.RmsError, LongPicMetrics.RmsError, LongMetrics.RmsError, LongMetrics.RmsError / PicMetrics.RmsError, LongMetrics.RmsError / LongPicMetrics.RmsError,
		PicMetrics.RelativeRmsError, LongMetrics.RelativeRmsError, PicMetrics.ScalarAmplitude,
		LongPicMetrics.ScalarAmplitude, LongMetrics.ScalarAmplitude, LongMetrics.ExactAmplitude, PicMetrics.ScalarEnergyProxy,
		LongPicMetrics.ScalarEnergyProxy, LongMetrics.ScalarEnergyProxy, LongMetrics.ExactEnergyProxy));
	return true;
}

#endif
