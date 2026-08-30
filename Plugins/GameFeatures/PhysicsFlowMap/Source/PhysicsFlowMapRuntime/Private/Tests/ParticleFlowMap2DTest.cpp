#if WITH_DEV_AUTOMATION_TESTS

#include "ParticleFlowMap2D.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParticleFlowMapGridContractTest,
	"PhysicsResearch.FlowMap.GridAndParticleContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParticleFlowMapGridContractTest::RunTest(const FString& Parameters)
{
	FFlowMapGridSettings2D GridSettings;
	GridSettings.CellCounts = FIntPoint(16, 8);
	GridSettings.CellSizeCm = 10.0f;
	FFlowMapVelocityGrid2D Grid;
	FString FailureReason;
	TestTrue(TEXT("16x8 MAC 网格生成"), Grid.Initialize(GridSettings, &FailureReason));
	TestEqual(TEXT("U 面数量"), Grid.FaceU.Num(), 17 * 8);
	TestEqual(TEXT("V 面数量"), Grid.FaceV.Num(), 16 * 9);
	Grid.SetUniformVelocity(FVector2f(20.0f, 0.0f));
	TestTrue(TEXT("均匀直槽散度为零"), Grid.CalculateDivergenceRms() < 1.e-8);

	FParticleFlowMapSettings2D ParticleSettings;
	ParticleSettings.ParticlesPerCellAxis = 2;
	ParticleSettings.ShortMapResetSteps = 3;
	ParticleSettings.LongMapResetSteps = 12;
	FParticleFlowMapSimulation2D Simulation;
	TestTrue(TEXT("粒子 Flow Map 会话初始化"), Simulation.Initialize(Grid, ParticleSettings, &FailureReason));
	TestEqual(TEXT("每格四粒子"), Simulation.GetParticles().Num(), 16 * 8 * 4);
	const FVector2f FirstStart = Simulation.GetParticles()[0].PositionCm;
	for (int32 Step = 0; Step < 6; ++Step) TestTrue(TEXT("直槽固定步"), Simulation.Step(1.0f / 60.0f, &FailureReason));
	const FVector2f FirstEnd = Simulation.GetParticles()[0].PositionCm;
	TestTrue(TEXT("均匀速度 RK2 位移精确"), FMath::IsNearlyEqual(FirstEnd.X - FirstStart.X, 2.0f, 1.e-4f));
	TestTrue(TEXT("直槽横向位置不漂移"), FMath::IsNearlyEqual(FirstEnd.Y, FirstStart.Y, 1.e-5f));
	TestEqual(TEXT("短 Map 按 3 步重置"), Simulation.GetMetrics().ShortMapResets, 2);
	TestEqual(TEXT("长 Map 六步内不重置"), Simulation.GetMetrics().LongMapResets, 0);
	TestEqual(TEXT("粒子没有离开直槽域"), Simulation.GetMetrics().LostParticles, 0);
	AddInfo(FString::Printf(TEXT("FlowMap straight cells=16x8 particles=%d steps=%d divergence_rms=%.9g displacement_cm=%.6f short_resets=%d long_resets=%d lost=%d"),
		Simulation.GetMetrics().ParticleCount, Simulation.GetMetrics().StepsExecuted, Grid.CalculateDivergenceRms(), FirstEnd.X - FirstStart.X,
		Simulation.GetMetrics().ShortMapResets, Simulation.GetMetrics().LongMapResets, Simulation.GetMetrics().LostParticles));

	FFlowMapGridSettings2D InvalidSettings = GridSettings;
	InvalidSettings.CellCounts.X = 1;
	TestFalse(TEXT("过小网格被拒绝"), Grid.Initialize(InvalidSettings, &FailureReason));
	TestTrue(TEXT("无效网格有明确错误"), !FailureReason.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParticleFlowMapVortexTest,
	"PhysicsResearch.FlowMap.SolidBodyVortex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParticleFlowMapVortexTest::RunTest(const FString& Parameters)
{
	FFlowMapGridSettings2D GridSettings;
	GridSettings.CellCounts = FIntPoint(32, 32);
	GridSettings.CellSizeCm = 5.0f;
	FFlowMapVelocityGrid2D Grid;
	FString FailureReason;
	TestTrue(TEXT("旋涡 MAC 网格生成"), Grid.Initialize(GridSettings, &FailureReason));
	const FVector2f Center(80.0f, 80.0f);
	Grid.SetSolidBodyVortex(Center, 0.5f);
	TestTrue(TEXT("解析刚体旋涡离散散度为零"), Grid.CalculateDivergenceRms() < 1.e-7);

	FParticleFlowMapSettings2D ParticleSettings;
	ParticleSettings.ParticlesPerCellAxis = 1;
	ParticleSettings.ShortMapResetSteps = 4;
	ParticleSettings.LongMapResetSteps = 20;
	ParticleSettings.MaximumMapDistortion = 1.0f;
	ParticleSettings.BoundaryMode = EFlowMapBoundaryMode2D::Periodic;
	FParticleFlowMapSimulation2D Simulation;
	TestTrue(TEXT("旋涡 Flow Map 会话初始化"), Simulation.Initialize(Grid, ParticleSettings, &FailureReason));
	const int32 ProbeIndex = 16 * 32 + 24;
	const float InitialRadius = FVector2f::Distance(Simulation.GetParticles()[ProbeIndex].PositionCm, Center);
	for (int32 Step = 0; Step < 60; ++Step) TestTrue(TEXT("旋涡固定步"), Simulation.Step(1.0f / 120.0f, &FailureReason));
	const float FinalRadius = FVector2f::Distance(Simulation.GetParticles()[ProbeIndex].PositionCm, Center);
	const float RelativeRadiusError = FMath::Abs(FinalRadius - InitialRadius) / InitialRadius;
	TestTrue(TEXT("半秒旋涡轨道半径误差低于千分之一"), RelativeRadiusError < 1.e-3f);
	TestEqual(TEXT("旋涡长 Map 按 20 步重置"), Simulation.GetMetrics().LongMapResets, 3);
	TestEqual(TEXT("旋涡短 Map 按 4 步重置"), Simulation.GetMetrics().ShortMapResets, 15);
	TestEqual(TEXT("周期旋涡粒子不丢失"), Simulation.GetMetrics().LostParticles, 0);
	TestTrue(TEXT("周期边界回卷被独立统计"), Simulation.GetMetrics().BoundaryWraps > 0);
	AddInfo(FString::Printf(TEXT("FlowMap vortex cells=32x32 particles=%d steps=%d boundary=periodic divergence_rms=%.9g radius_error=%.9g jacobian_det_error_max=%.9g short_resets=%d long_resets=%d lost=%d wraps=%d"),
		Simulation.GetMetrics().ParticleCount, Simulation.GetMetrics().StepsExecuted, Grid.CalculateDivergenceRms(), RelativeRadiusError,
		Simulation.GetMetrics().MaximumJacobianDeterminantError, Simulation.GetMetrics().ShortMapResets,
		Simulation.GetMetrics().LongMapResets, Simulation.GetMetrics().LostParticles, Simulation.GetMetrics().BoundaryWraps));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParticleFlowMapPicProjectionTest,
	"PhysicsResearch.FlowMap.PicTransferAndPressureProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParticleFlowMapPicProjectionTest::RunTest(const FString& Parameters)
{
	FFlowMapGridSettings2D Settings;
	Settings.CellCounts = FIntPoint(32, 16);
	Settings.CellSizeCm = 5.0f;
	FFlowMapVelocityGrid2D UniformGrid;
	FString FailureReason;
	TestTrue(TEXT("PIC 输入网格生成"), UniformGrid.Initialize(Settings, &FailureReason));
	UniformGrid.SetUniformVelocity(FVector2f(30.0f, -7.0f));
	FParticleFlowMapSettings2D ParticleSettings;
	ParticleSettings.ParticlesPerCellAxis = 2;
	FParticleFlowMapSimulation2D Particles;
	TestTrue(TEXT("PIC 输入粒子生成"), Particles.Initialize(UniformGrid, ParticleSettings, &FailureReason));
	FFlowMapVelocityGrid2D Reconstructed;
	TestTrue(TEXT("粒子到 MAC 网格归一化传输"), FFlowMapParticleGridTransfer2D::TransferPicToGrid(Particles.GetParticles(), Settings, Reconstructed, &FailureReason));
	float MaximumVelocityError = 0.0f;
	for (float Value : Reconstructed.FaceU) MaximumVelocityError = FMath::Max(MaximumVelocityError, FMath::Abs(Value - 30.0f));
	for (float Value : Reconstructed.FaceV) MaximumVelocityError = FMath::Max(MaximumVelocityError, FMath::Abs(Value + 7.0f));
	TestTrue(TEXT("均匀场 PIC 重建误差低于 1e-5"), MaximumVelocityError < 1.e-5f);
	TestTrue(TEXT("PIC 重建保持零散度"), Reconstructed.CalculateDivergenceRms() < 1.e-8);

	FFlowMapVelocityGrid2D CompressionGrid;
	TestTrue(TEXT("压力输入网格生成"), CompressionGrid.Initialize(Settings, &FailureReason));
	CompressionGrid.SetPeriodicCompressionWave(50.0f);
	const double EnergyBefore = CompressionGrid.CalculateKineticEnergyProxy();
	FFlowMapPressureProjectionMetrics2D Projection;
	TestTrue(TEXT("周期压力投影执行"), FFlowMapPressureProjector2D::ProjectPeriodic(CompressionGrid, 1024, Projection, &FailureReason));
	const double EnergyAfter = CompressionGrid.CalculateKineticEnergyProxy();
	TestTrue(TEXT("压力投影前存在显著散度"), Projection.DivergenceRmsBefore > 0.1);
	TestTrue(TEXT("压力投影将散度 RMS 降低至少 99.9%"), Projection.DivergenceRmsAfter < Projection.DivergenceRmsBefore * 1.e-3);
	TestTrue(TEXT("压力投影将最大散度降低至少 99.9%"), Projection.MaximumDivergenceAfter < Projection.MaximumDivergenceBefore * 1.e-3f);
	TestTrue(TEXT("纯压缩速度能量被压力投影移除"), EnergyAfter < EnergyBefore * 1.e-4);
	AddInfo(FString::Printf(TEXT("FlowMap PIC+projection cells=32x16 particles=%d pic_max_velocity_error=%.9g projection_iterations=%d divergence_rms_before=%.9g divergence_rms_after=%.9g divergence_ratio=%.9g max_divergence_before=%.9g max_divergence_after=%.9g energy_before=%.9g energy_after=%.9g"),
		Particles.GetMetrics().ParticleCount, MaximumVelocityError, Projection.Iterations, Projection.DivergenceRmsBefore,
		Projection.DivergenceRmsAfter, Projection.DivergenceRmsAfter / Projection.DivergenceRmsBefore,
		Projection.MaximumDivergenceBefore, Projection.MaximumDivergenceAfter, EnergyBefore, EnergyAfter));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FParticleFlowMapGradientTransferTest,
	"PhysicsResearch.FlowMap.GradientEnhancedTransfer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParticleFlowMapGradientTransferTest::RunTest(const FString& Parameters)
{
	FFlowMapGridSettings2D Settings;
	Settings.CellCounts = FIntPoint(32, 32);
	Settings.CellSizeCm = 5.0f;
	const float Lx = Settings.CellCounts.X * Settings.CellSizeCm;
	const float Ly = Settings.CellCounts.Y * Settings.CellSizeCm;
	const float Kx = 4.0f * PI / Lx;
	const float Ky = 4.0f * PI / Ly;
	TArray<FFlowMapScalarParticle2D> Particles;
	Particles.Reserve(32 * 32);
	for (int32 Y = 0; Y < 32; ++Y)
		for (int32 X = 0; X < 32; ++X)
			{
					const float OffsetX = 0.15f + 0.70f * static_cast<float>((X * 17 + Y * 13 + 7) % 101) / 100.0f;
					const float OffsetY = 0.15f + 0.70f * static_cast<float>((X * 29 + Y * 11 + 19) % 103) / 102.0f;
					FFlowMapScalarParticle2D Particle;
					Particle.PositionCm = FVector2f((X + OffsetX) * Settings.CellSizeCm, (Y + OffsetY) * Settings.CellSizeCm);
					const float XPhase = Kx * Particle.PositionCm.X;
					const float YPhase = Ky * Particle.PositionCm.Y;
					Particle.Value = FMath::Sin(XPhase) * FMath::Cos(YPhase);
					Particle.GradientPerCm = FVector2f(Kx * FMath::Cos(XPhase) * FMath::Cos(YPhase), -Ky * FMath::Sin(XPhase) * FMath::Sin(YPhase));
					Particles.Add(Particle);
			}
	TArray<float> PicValues;
	TArray<float> GradientValues;
	FString FailureReason;
	TestTrue(TEXT("标量 PIC 周期传输"), FFlowMapGradientTransfer2D::TransferScalarToPeriodicCellGrid(Particles, Settings, false, PicValues, &FailureReason));
	TestTrue(TEXT("标量梯度增强周期传输"), FFlowMapGradientTransfer2D::TransferScalarToPeriodicCellGrid(Particles, Settings, true, GradientValues, &FailureReason));
	double PicSquares = 0.0;
	double GradientSquares = 0.0;
	for (int32 Y = 0; Y < 32; ++Y)
		for (int32 X = 0; X < 32; ++X)
		{
			const FVector2f Center((X + 0.5f) * Settings.CellSizeCm, (Y + 0.5f) * Settings.CellSizeCm);
			const double Exact = FMath::Sin(Kx * Center.X) * FMath::Cos(Ky * Center.Y);
			const int32 Index = Y * 32 + X;
			PicSquares += FMath::Square(static_cast<double>(PicValues[Index]) - Exact);
			GradientSquares += FMath::Square(static_cast<double>(GradientValues[Index]) - Exact);
		}
	const double PicRms = FMath::Sqrt(PicSquares / 1024.0);
	const double GradientRms = FMath::Sqrt(GradientSquares / 1024.0);
	TestTrue(TEXT("梯度项把畸变粒子标量重建误差至少降低 25%"), GradientRms < PicRms * 0.75);
	TestTrue(TEXT("梯度增强 RMS 低于 0.02"), GradientRms < 0.02);
	AddInfo(FString::Printf(TEXT("FlowMap gradient transfer cells=32x32 particles=%d distribution=deterministic_jittered field=sin2x_cos2y pic_rms=%.9g gradient_rms=%.9g gradient_to_pic_ratio=%.9g"),
		Particles.Num(), PicRms, GradientRms, GradientRms / PicRms));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFlowMapSolidBoundaryRasterTest,
	"PhysicsResearch.FlowMap.KinematicSolidBoundaryRaster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFlowMapSolidBoundaryRasterTest::RunTest(const FString& Parameters)
{
	const FTransform Previous(FRotator(0.0, 0.0, 0.0), FVector(-2.0, 0.0, 0.0));
	const FTransform Current(FRotator(0.0, 6.0, 0.0), FVector(0.0, 0.0, 0.0));
	FFlowMapKinematicCircle2D Circle;
	FString FailureReason;
	TestTrue(TEXT("UE/Chaos 刚体前后 Transform 转换为运动圆"), FFlowMapSolidBoundaryRasterizer2D::BuildCircleFromRigidBodyTransforms(Previous, Current, 12.0f, 0.1f, Circle, &FailureReason));
	TestEqual(TEXT("运动圆中心取当前刚体位置"), Circle.CenterCm, FVector2f::ZeroVector);
	TestTrue(TEXT("刚体 X 线速度由固定步差分得到"), FMath::IsNearlyEqual(Circle.LinearVelocityCmPerSecond.X, 20.0f, 1.e-5f));
	TestTrue(TEXT("刚体 Y 线速度为零"), FMath::IsNearlyZero(Circle.LinearVelocityCmPerSecond.Y, 1.e-6f));
	TestTrue(TEXT("刚体逆时针角速度按 UE Z/Yaw 正方向得到"), FMath::IsNearlyEqual(Circle.AngularVelocityRadiansPerSecond, PI / 3.0f, 1.e-5f));
	FFlowMapKinematicCircle2D InvalidCircle;
	TestFalse(TEXT("零固定步会在栅格化前被拒绝"), FFlowMapSolidBoundaryRasterizer2D::BuildCircleFromRigidBodyTransforms(Previous, Current, 12.0f, 0.0f, InvalidCircle, &FailureReason));
	FFlowMapGridSettings2D Grid;
	Grid.CellCounts = FIntPoint(16, 16);
	Grid.CellSizeCm = 5.0f;
	Grid.OriginCm = FVector2f(-40.0f, -40.0f);
	FFlowMapSolidBoundaryRaster2D Raster;
	TestTrue(TEXT("运动圆栅格化到 staggered MAC 边界"), FFlowMapSolidBoundaryRasterizer2D::RasterizeCircle(Grid, Circle, Raster, &FailureReason));
	TestEqual(TEXT("Cell 固体掩码尺寸精确"), Raster.SolidCellMask.Num(), 16 * 16);
	TestEqual(TEXT("U 面固体掩码尺寸精确"), Raster.SolidFaceUMask.Num(), 17 * 16);
	TestEqual(TEXT("V 面固体掩码尺寸精确"), Raster.SolidFaceVMask.Num(), 16 * 17);
	TestEqual(TEXT("半径 12 cm 的居中圆覆盖 16 个 Cell 中心"), Raster.CountSolidCells(), 16);
	const int32 UIndex = 8 * 17 + 8;
	const int32 VIndex = 8 * 16 + 8;
	TestEqual(TEXT("圆内 U 面被标记为固体"), Raster.SolidFaceUMask[UIndex], 1u);
	TestEqual(TEXT("圆内 V 面被标记为固体"), Raster.SolidFaceVMask[VIndex], 1u);
	TestTrue(TEXT("U 面无滑移速度含平移和角速度叉乘"), FMath::IsNearlyEqual(Raster.SolidFaceVelocityU[UIndex], 20.0f - (PI / 3.0f) * 2.5f, 1.e-5f));
	TestTrue(TEXT("V 面无滑移速度含角速度叉乘"), FMath::IsNearlyEqual(Raster.SolidFaceVelocityV[VIndex], (PI / 3.0f) * 2.5f, 1.e-5f));
	float MaximumNoSlipResidual = 0.0f;
	int32 SolidUFaces = 0, SolidVFaces = 0;
	for (int32 Y = 0; Y < 16; ++Y) for (int32 X = 0; X <= 16; ++X)
	{
		const int32 Index = Y * 17 + X;
		if (!Raster.SolidFaceUMask[Index]) continue;
		++SolidUFaces;
		const FVector2f Position = Grid.OriginCm + FVector2f(X * 5.0f, (Y + 0.5f) * 5.0f);
		const float Expected = Circle.LinearVelocityCmPerSecond.X - Circle.AngularVelocityRadiansPerSecond * Position.Y;
		MaximumNoSlipResidual = FMath::Max(MaximumNoSlipResidual, FMath::Abs(Raster.SolidFaceVelocityU[Index] - Expected));
	}
	for (int32 Y = 0; Y <= 16; ++Y) for (int32 X = 0; X < 16; ++X)
	{
		const int32 Index = Y * 16 + X;
		if (!Raster.SolidFaceVMask[Index]) continue;
		++SolidVFaces;
		const FVector2f Position = Grid.OriginCm + FVector2f((X + 0.5f) * 5.0f, Y * 5.0f);
		const float Expected = Circle.LinearVelocityCmPerSecond.Y + Circle.AngularVelocityRadiansPerSecond * Position.X;
		MaximumNoSlipResidual = FMath::Max(MaximumNoSlipResidual, FMath::Abs(Raster.SolidFaceVelocityV[Index] - Expected));
	}
	TestTrue(TEXT("所有固体面速度满足解析无滑移运动"), MaximumNoSlipResidual < 1.e-6f);
	AddInfo(FString::Printf(TEXT("FlowMap kinematic solid raster cells=16x16 radius_cm=%.9g center=(%.9g,%.9g) linear_velocity=(%.9g,%.9g) angular_velocity=%.9g solid_cells=%d solid_u_faces=%d solid_v_faces=%d max_no_slip_residual=%.9g source=ue_chaos_rigid_body_transforms"),
		Circle.RadiusCm, Circle.CenterCm.X, Circle.CenterCm.Y, Circle.LinearVelocityCmPerSecond.X, Circle.LinearVelocityCmPerSecond.Y,
		Circle.AngularVelocityRadiansPerSecond, Raster.CountSolidCells(), SolidUFaces, SolidVFaces, MaximumNoSlipResidual));
	return true;
}

#endif
