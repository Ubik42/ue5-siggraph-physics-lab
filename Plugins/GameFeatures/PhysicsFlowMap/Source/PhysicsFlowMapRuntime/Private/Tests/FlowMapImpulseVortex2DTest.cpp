#if WITH_DEV_AUTOMATION_TESTS

#include "FlowMapImpulseVortex2D.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlowMapImpulseVortexAblationTest,
	"PhysicsResearch.FlowMap.ImpulseVortexClosedLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowMapImpulseVortexAblationTest::RunTest(const FString& Parameters)
{
	FFlowMapImpulseVortexSettings2D Settings;
	Settings.Grid.CellCounts = FIntPoint(32, 32);
	Settings.Grid.CellSizeCm = 5.0f;
	Settings.ParticlesPerCellAxis = 2;
	Settings.FixedDeltaSeconds = 1.0f / 240.0f;
	Settings.LongMapResetSteps = 20;
	Settings.PressureIterations = 128;
	Settings.VortexAmplitudeCmPerSecond = 20.0f;
	FString FailureReason;
	FFlowMapImpulseVortexSimulation2D ShortPic;
	FFlowMapImpulseVortexSimulation2D LongMapPic;
	FFlowMapImpulseVortexSimulation2D LongMapGradient;
	TestTrue(TEXT("Taylor-Green Short PIC 初始化"), ShortPic.Initialize(Settings, EFlowMapImpulseMode2D::ShortPic, &FailureReason));
	TestTrue(TEXT("Taylor-Green Long Map PIC 初始化"), LongMapPic.Initialize(Settings, EFlowMapImpulseMode2D::LongMapPic, &FailureReason));
	TestTrue(TEXT("Taylor-Green Long Map Gradient 初始化"), LongMapGradient.Initialize(Settings, EFlowMapImpulseMode2D::LongMapGradient, &FailureReason));
	for (int32 Step = 0; Step < 120; ++Step)
	{
		TestTrue(TEXT("Taylor-Green Short PIC 固定步"), ShortPic.Step(&FailureReason));
		TestTrue(TEXT("Taylor-Green Long Map PIC 固定步"), LongMapPic.Step(&FailureReason));
		TestTrue(TEXT("Taylor-Green Long Map Gradient 固定步"), LongMapGradient.Step(&FailureReason));
	}
	const FFlowMapImpulseVortexMetrics2D& Short = ShortPic.GetMetrics();
	const FFlowMapImpulseVortexMetrics2D& LongPic = LongMapPic.GetMetrics();
	const FFlowMapImpulseVortexMetrics2D& LongGradient = LongMapGradient.GetMetrics();
	TestEqual(TEXT("两路径固定步相同"), Short.StepsExecuted, 120);
	TestEqual(TEXT("三路径粒子数相同"), Short.ParticleCount, LongPic.ParticleCount);
	TestEqual(TEXT("梯度路径粒子数相同"), Short.ParticleCount, LongGradient.ParticleCount);
	TestEqual(TEXT("两路径每步压力投影"), Short.PressureProjections, 120);
	TestEqual(TEXT("Long Map PIC 每 20 步重置"), LongPic.MapResets, 6);
	TestEqual(TEXT("Long Map Gradient 每 20 步重置"), LongGradient.MapResets, 6);
	TestEqual(TEXT("Short PIC 每步重置"), Short.MapResets, 120);
	TestTrue(TEXT("三路径最终散度 RMS 低于 1e-3"), Short.DivergenceRms < 1.e-3 && LongPic.DivergenceRms < 1.e-3 && LongGradient.DivergenceRms < 1.e-3);
	TestTrue(TEXT("Long Map PIC 稳态速度误差低于 Short PIC"), LongPic.VelocityRmsErrorCmPerSecond < Short.VelocityRmsErrorCmPerSecond);
	TestTrue(TEXT("Long Map Gradient 误差低于同周期 PIC"), LongGradient.VelocityRmsErrorCmPerSecond < LongPic.VelocityRmsErrorCmPerSecond);
	TestTrue(TEXT("Long Map Gradient 保留更多旋涡动能"), LongGradient.KineticEnergyProxy > LongPic.KineticEnergyProxy);
	TestTrue(TEXT("Long Map Gradient 相对速度 RMS 低于 10%"), LongGradient.RelativeVelocityRmsError < 0.1);
	AddInfo(FString::Printf(TEXT("FlowMap impulse vortex steps=120 cells=32x32 particles=%d dt=%.9g pressure_iterations=%d short_resets=%d long_resets=%d short_velocity_rms=%.9g long_pic_velocity_rms=%.9g long_gradient_velocity_rms=%.9g long_pic_to_short_rms=%.9g long_gradient_to_long_pic_rms=%.9g short_relative_rms=%.9g long_pic_relative_rms=%.9g long_gradient_relative_rms=%.9g short_divergence_rms=%.9g long_pic_divergence_rms=%.9g long_gradient_divergence_rms=%.9g short_max_divergence=%.9g long_pic_max_divergence=%.9g long_gradient_max_divergence=%.9g initial_energy=%.9g short_energy=%.9g long_pic_energy=%.9g long_gradient_energy=%.9g long_gradient_jacobian_det_error_max=%.9g"),
		Short.ParticleCount, Settings.FixedDeltaSeconds, Settings.PressureIterations, Short.MapResets, LongGradient.MapResets,
		Short.VelocityRmsErrorCmPerSecond, LongPic.VelocityRmsErrorCmPerSecond, LongGradient.VelocityRmsErrorCmPerSecond,
		LongPic.VelocityRmsErrorCmPerSecond / Short.VelocityRmsErrorCmPerSecond, LongGradient.VelocityRmsErrorCmPerSecond / LongPic.VelocityRmsErrorCmPerSecond,
		Short.RelativeVelocityRmsError, LongPic.RelativeVelocityRmsError, LongGradient.RelativeVelocityRmsError,
		Short.DivergenceRms, LongPic.DivergenceRms, LongGradient.DivergenceRms, Short.MaximumAbsoluteDivergence,
		LongPic.MaximumAbsoluteDivergence, LongGradient.MaximumAbsoluteDivergence, Short.InitialKineticEnergyProxy,
		Short.KineticEnergyProxy, LongPic.KineticEnergyProxy, LongGradient.KineticEnergyProxy, LongGradient.MaximumJacobianDeterminantError));
	return true;
}

#endif
