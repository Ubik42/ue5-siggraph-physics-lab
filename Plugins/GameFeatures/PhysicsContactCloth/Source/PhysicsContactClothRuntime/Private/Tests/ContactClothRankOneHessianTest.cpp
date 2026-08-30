#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothModalSubspace.h"
#include "ContactClothGpuJacobi.h"
#include "Misc/AutomationTest.h"
#include "RHI.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothRankOneHessianTest,
	"PhysicsResearch.ContactCloth.RankOneCollisionHessian",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothRankOneHessianTest::RunTest(const FString& Parameters)
{
	constexpr int32 Columns = 5;
	constexpr int32 Rows = 4;
	constexpr int32 ModeCount = 4;
	FContactClothModalBasis Basis;
	FContactClothReducedSystem BaseSystem;
	FContactClothRankOneProjectionCache Cache;
	TestTrue(TEXT("构建固定顶边模态"), FContactClothModalSubspace::BuildPinnedGridBasis(Columns, Rows, ModeCount, Basis));
	TestTrue(TEXT("构建基础缩减 SPD 系统"), FContactClothModalSubspace::BuildReducedGridSystem(Basis, Columns, Rows, 1.0, 1.0, BaseSystem));
	TestTrue(TEXT("缓存每粒子模态外积"), FContactClothModalSubspace::BuildRankOneProjectionCache(Basis, Cache));
	TestEqual(TEXT("缓存粒子数"), Cache.ParticleCount, Columns * Rows);
	TestEqual(TEXT("缓存模态数"), Cache.ModeCount, ModeCount);

	const int32 ContactParticle = (Rows - 1) * Columns + Columns / 2;
	FContactClothModalContact Contact;
	Contact.ParticleIndex = ContactParticle;
	Contact.Normal = FVector3d(1.0, 2.0, 3.0).GetSafeNormal();
	Contact.Weight = 8.0;
	FContactClothVectorReducedSystem CollisionSystem;
	TestTrue(TEXT("装配完整 3m×3m 碰撞缩减系统"), FContactClothModalSubspace::AssembleVectorCollisionSystem(BaseSystem, Cache, MakeArrayView(&Contact, 1), CollisionSystem));
	const int32 DofCount = CollisionSystem.GetDofCount();
	TestEqual(TEXT("向量缩减自由度"), DofCount, ModeCount * 3);

	double MaximumSymmetryError = 0.0;
	for (int32 Row = 0; Row < DofCount; ++Row)
	{
		for (int32 Column = 0; Column < DofCount; ++Column)
		{
			MaximumSymmetryError = FMath::Max(MaximumSymmetryError, FMath::Abs(CollisionSystem.Matrix[Row * DofCount + Column] - CollisionSystem.Matrix[Column * DofCount + Row]));
		}
	}
	TestTrue(TEXT("动态碰撞系统保持对称"), MaximumSymmetryError < 1.e-12);
	const int32 XMode = 0;
	const int32 YMode = ModeCount;
	const double ExpectedCrossAxis = Contact.Weight * Contact.Normal.X * Contact.Normal.Y * Cache.GetValue(ContactParticle, 0, 0);
	const double ActualCrossAxis = CollisionSystem.Matrix[XMode * DofCount + YMode];
	TestTrue(TEXT("接触法线产生非零 XY 交叉 Hessian"), FMath::Abs(ActualCrossAxis) > 1.e-8);
	TestTrue(TEXT("缓存 rank-one 项与直接公式一致"), FMath::Abs(ActualCrossAxis - ExpectedCrossAxis) < 1.e-12);

	TArray<double> KnownSolution;
	KnownSolution.SetNumUninitialized(DofCount);
	for (int32 Index = 0; Index < DofCount; ++Index) { KnownSolution[Index] = 0.01 * (Index + 1); }
	TArray<double> Rhs;
	Rhs.SetNumZeroed(DofCount);
	for (int32 Row = 0; Row < DofCount; ++Row)
	{
		for (int32 Column = 0; Column < DofCount; ++Column) { Rhs[Row] += CollisionSystem.Matrix[Row * DofCount + Column] * KnownSolution[Column]; }
	}
	TArray<double> Recovered;
	TestTrue(TEXT("Cholesky 求解动态向量缩减系统"), FContactClothModalSubspace::SolveVectorReducedSystem(CollisionSystem, Rhs, Recovered));
	double MaximumCoordinateError = 0.0;
	for (int32 Index = 0; Index < DofCount; ++Index) { MaximumCoordinateError = FMath::Max(MaximumCoordinateError, FMath::Abs(Recovered[Index] - KnownSolution[Index])); }
	TestTrue(TEXT("动态系统恢复已知解"), MaximumCoordinateError < 1.e-10);

	TArray<float> GpuBaseSystem;
	GpuBaseSystem.Reserve(BaseSystem.Matrix.Num());
	for (double Value : BaseSystem.Matrix) { GpuBaseSystem.Add(static_cast<float>(Value)); }
	TArray<float> GpuOuterProducts;
	GpuOuterProducts.Reserve(Cache.Values.Num());
	for (double Value : Cache.Values) { GpuOuterProducts.Add(static_cast<float>(Value)); }
	FContactClothGpuModalContact GpuContact;
	GpuContact.ParticleIndex = Contact.ParticleIndex;
	GpuContact.Normal = FVector3f(Contact.Normal);
	GpuContact.Weight = static_cast<float>(Contact.Weight);
	const FContactClothGpuRankOneAssemblyResult GpuResult = FContactClothGpuRankOneAssembly::RunBlocking(
		Basis.ParticleCount, Basis.ModeCount, GpuBaseSystem, GpuOuterProducts, MakeArrayView(&GpuContact, 1));
	TestTrue(TEXT("GPU rank-one 矩阵装配已调度"), GpuResult.bDispatched);
	TestTrue(TEXT("GPU rank-one 矩阵读回成功"), GpuResult.bReadbackSucceeded);
	TestEqual(TEXT("GPU 完整向量矩阵尺寸"), GpuResult.Matrix.Num(), DofCount * DofCount);
	double MaximumGpuCpuError = 0.0;
	if (GpuResult.Matrix.Num() == CollisionSystem.Matrix.Num())
	{
		for (int32 Index = 0; Index < GpuResult.Matrix.Num(); ++Index)
		{
			MaximumGpuCpuError = FMath::Max(MaximumGpuCpuError, FMath::Abs(static_cast<double>(GpuResult.Matrix[Index]) - CollisionSystem.Matrix[Index]));
		}
	}
	else
	{
		MaximumGpuCpuError = TNumericLimits<double>::Max();
	}
	TestTrue(TEXT("GPU 动态装配与 CPU 双精度真值一致"), MaximumGpuCpuError < 1.e-5);

	FContactClothModalContact InvalidContact = Contact;
	InvalidContact.ParticleIndex = Basis.ParticleCount;
	FContactClothVectorReducedSystem RejectedSystem;
	TestFalse(TEXT("越界接触不能污染缓存装配"), FContactClothModalSubspace::AssembleVectorCollisionSystem(BaseSystem, Cache, MakeArrayView(&InvalidContact, 1), RejectedSystem));
	AddInfo(FString::Printf(
		TEXT("Rank-one collision Hessian: particles=%d modes=%d vector_dofs=%d cache_values=%d xy_coupling=%.12f symmetry_error=%.3e solve_error=%.3e gpu_cpu_error=%.3e"),
		Basis.ParticleCount, Basis.ModeCount, DofCount, Cache.Values.Num(), ActualCrossAxis, MaximumSymmetryError, MaximumCoordinateError, MaximumGpuCpuError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuRealtimeRankOneSystemTest,
	"PhysicsResearch.ContactCloth.GpuRealtimeRankOneCollisionSystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuRealtimeRankOneSystemTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过实时 rank-one 碰撞系统；D3D12 报告单独生成。"));
		return true;
	}
	constexpr int32 Columns = 5;
	constexpr int32 Rows = 4;
	constexpr int32 ModeCount = 4;
	FContactClothModalBasis Basis;
	FContactClothReducedSystem ReducedSystem;
	FContactClothRankOneProjectionCache Cache;
	TArray<double> InverseSystem;
	TestTrue(TEXT("实时门构建模态基"), FContactClothModalSubspace::BuildPinnedGridBasis(Columns, Rows, ModeCount, Basis));
	TestTrue(TEXT("实时门构建基础系统"), FContactClothModalSubspace::BuildReducedGridSystem(Basis, Columns, Rows, 1.0, 1.0, ReducedSystem));
	TestTrue(TEXT("实时门构建外积缓存"), FContactClothModalSubspace::BuildRankOneProjectionCache(Basis, Cache));
	TestTrue(TEXT("实时门构建结构逆矩阵"), FContactClothModalSubspace::InvertReducedSystem(ReducedSystem, InverseSystem));

	FContactClothGpuRealtimeSettings Settings;
	Settings.Columns = Columns;
	Settings.Rows = Rows;
	Settings.SpacingCm = 10.0f;
	Settings.Iterations = 1;
	Settings.Relaxation = 0.0f;
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.GravityCmPerSecondSquared = FVector3f::ZeroVector;
	Settings.VelocityDamping = 0.0f;
	Settings.ReadbackIntervalSteps = 1;
	Settings.ModalModeCount = ModeCount;
	Settings.ModalCorrectionStrength = 1.0f;
	for (double Value : Basis.Values) Settings.ModalBasisValues.Add(static_cast<float>(Value));
	for (double Value : InverseSystem) Settings.ModalInverseSystemValues.Add(static_cast<float>(Value));
	for (double Value : ReducedSystem.Matrix) Settings.ModalSystemValues.Add(static_cast<float>(Value));
	for (double Value : Cache.Values) Settings.ModalParticleOuterProductValues.Add(static_cast<float>(Value));
	Settings.bEnableNonDistanceBarrier = true;
	Settings.NdbBaseWeight = 0.05f;
	Settings.NdbActivationMultiplier = 2.0f;
	Settings.NdbMaximumWeight = 1024.0f;
	Settings.CollisionPrimitiveCapacity = 1;
	Settings.CollisionThicknessCm = 0.0f;
	Settings.RankOneMaximumCorrectionCm = 4.0f;
	const FVector3f SphereCenter(17.0f, -2.0f, -2.0f);
	constexpr float SphereRadius = 8.0f;
	Settings.CollisionSpheres.Add({SphereCenter, SphereRadius});

	TArray<FVector3f> InitialPositions;
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			InitialPositions.Emplace(Column * Settings.SpacingCm, 0.0f, (Rows - 1 - Row) * Settings.SpacingCm);
		}
	}
	const int32 ProbeIndex = (Rows - 1) * Columns + Columns / 2;
	auto RunOneStep = [&InitialPositions](const FContactClothGpuRealtimeSettings& SessionSettings, TArray<FVector3f>& OutPositions, FContactClothGpuRealtimeStats& OutStats)
	{
		FContactClothGpuRealtimeSimulation Simulation;
		if (!Simulation.Initialize(InitialPositions, SessionSettings) || !Simulation.EnqueueStep()) return false;
		Simulation.FlushAndWaitForReadbackForValidation();
		int64 Step = 0;
		const bool bRead = Simulation.ConsumeLatestPositions(OutPositions, Step);
		OutStats = Simulation.GetStats();
		Simulation.Shutdown();
		return bRead && Step == 1;
	};
	TArray<FVector3f> BaselinePositions;
	FContactClothGpuRealtimeStats BaselineStats;
	Settings.bEnableRankOneCollisionHessian = false;
	TestTrue(TEXT("局部 NDB 基线完成"), RunOneStep(Settings, BaselinePositions, BaselineStats));
	TArray<FVector3f> RankOnePositions;
	FContactClothGpuRealtimeStats RankOneStats;
	Settings.bEnableRankOneCollisionHessian = true;
	TestTrue(TEXT("实时 rank-one 会话完成"), RunOneStep(Settings, RankOnePositions, RankOneStats));
	const float BaselinePenetration = BaselinePositions.IsValidIndex(ProbeIndex)
		? FMath::Max(0.0f, SphereRadius - (BaselinePositions[ProbeIndex] - SphereCenter).Length()) : TNumericLimits<float>::Max();
	const float RankOnePenetration = RankOnePositions.IsValidIndex(ProbeIndex)
		? FMath::Max(0.0f, SphereRadius - (RankOnePositions[ProbeIndex] - SphereCenter).Length()) : TNumericLimits<float>::Max();
	TestEqual(TEXT("关闭时不执行 rank-one 系统"), BaselineStats.RankOneCollisionSystemsExecuted, static_cast<int64>(0));
	TestEqual(TEXT("开启时每固定步装配并求解一次"), RankOneStats.RankOneCollisionSystemsExecuted, static_cast<int64>(1));
	TestEqual(TEXT("实时门同步更新 NDB 激活状态"), RankOneStats.NdbStateUpdatesExecuted, static_cast<int64>(1));
	TestTrue(TEXT("GPU 激活 Buffer 回读到活跃接触对"), RankOneStats.LatestActiveContactPairs > 0);
	TestEqual(TEXT("单步接触的最大连续激活次数为一"), RankOneStats.MaximumNdbActivationCount, 1);
	TestTrue(TEXT("固定步记录有限渲染提交耗时"), FMath::IsFinite(RankOneStats.LatestRenderSubmissionMilliseconds)
		&& RankOneStats.LatestRenderSubmissionMilliseconds > 0.0);
	TestTrue(TEXT("rank-one 全局接触步进一步降低穿透"), RankOnePenetration < BaselinePenetration - 1.e-4f);
	TestTrue(TEXT("rank-one 输出保持有限"), RankOnePositions.IsValidIndex(ProbeIndex)
		&& FMath::IsFinite(RankOnePositions[ProbeIndex].X) && FMath::IsFinite(RankOnePositions[ProbeIndex].Y) && FMath::IsFinite(RankOnePositions[ProbeIndex].Z));
	AddInfo(FString::Printf(TEXT("Realtime rank-one system: baseline_penetration_cm=%.9f rank_one_penetration_cm=%.9f systems=%lld active_pairs=%d max_activation=%d render_submission_ms=%.6f"),
		BaselinePenetration, RankOnePenetration, RankOneStats.RankOneCollisionSystemsExecuted,
		RankOneStats.LatestActiveContactPairs, RankOneStats.MaximumNdbActivationCount, RankOneStats.LatestRenderSubmissionMilliseconds));
	return true;
}

#endif
