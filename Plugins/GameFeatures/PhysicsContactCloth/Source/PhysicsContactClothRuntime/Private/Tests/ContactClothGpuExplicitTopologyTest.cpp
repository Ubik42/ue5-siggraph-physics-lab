#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothChaosTopology.h"
#include "ContactClothGpuExplicitTopology.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuExplicitTopologyTest,
	"PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.Blocking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuExplicitTopologyTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assassin/Mesh/SKM_Assassin_Skin1.SKM_Assassin_Skin1"));
	TestNotNull(TEXT("加载 Assassin Skeletal Mesh"), Mesh);
	if (!Mesh) return false;

	FContactClothExplicitTopology Topology;
	FString ExtractionFailure;
	TestTrue(TEXT("提取正式 Chaos LOD0 显式拓扑"), FContactClothChaosTopology::ExtractLod0(*Mesh, Topology, &ExtractionFailure));
	if (!Topology.IsValid())
	{
		AddError(ExtractionFailure);
		return false;
	}

	TArray<FVector3f> PerturbedPositions = Topology.RestPositions;
	for (int32 ParticleIndex = 0; ParticleIndex < PerturbedPositions.Num(); ++ParticleIndex)
	{
		if (Topology.InverseMasses[ParticleIndex] > 0.0f)
		{
			const float Phase = static_cast<float>((ParticleIndex * 37) % 101) / 100.0f;
			PerturbedPositions[ParticleIndex] += FVector3f(
				FMath::Sin(Phase * 2.0f * PI) * 4.0f,
				FMath::Cos(Phase * 2.0f * PI) * 3.0f,
				(Phase - 0.5f) * 10.0f);
		}
	}

	FContactClothGpuExplicitTopologySettings Settings;
	Settings.Iterations = 128;
	Settings.Relaxation = 0.9f;
	const FContactClothGpuExplicitTopologyResult Result = FContactClothGpuExplicitTopology::RunBlocking(
		PerturbedPositions,
		Topology.InverseMasses,
		Topology.StructuralEdges,
		Topology.StructuralRestLengthsCm,
		Settings);
	AddInfo(FString::Printf(
		TEXT("GPU explicit Chaos topology: particles=%d edges=%d iterations=%d input_edge_error_cm=%.6f output_edge_error_cm=%.6f fixed_drift_cm=%.9f blocking_wall_ms=%.3f"),
		Result.ParticleCount, Result.EdgeCount, Settings.Iterations, Result.InputMaximumEdgeErrorCm,
		Result.OutputMaximumEdgeErrorCm, Result.MaximumFixedParticleDriftCm, Result.BlockingWallMilliseconds));
	if (!Result.FailureReason.IsEmpty()) AddError(Result.FailureReason);
	TestTrue(TEXT("显式拓扑 GPU Dispatch 已执行"), Result.bDispatched);
	TestTrue(TEXT("显式拓扑 GPU Readback 成功"), Result.bReadbackSucceeded);
	TestEqual(TEXT("GPU 实际消费全部 Chaos 粒子"), Result.ParticleCount, 2011);
	TestEqual(TEXT("GPU CSR 实际消费全部 Chaos 唯一边"), Result.EdgeCount, 5714);
	TestEqual(TEXT("GPU 输出粒子数匹配"), Result.Positions.Num(), Topology.RestPositions.Num());
	TestTrue(TEXT("输入扰动产生可测结构误差"), Result.InputMaximumEdgeErrorCm > 0.1);
	TestTrue(TEXT("任意拓扑 Jacobi 至少降低 20% 最大边误差"), Result.OutputMaximumEdgeErrorCm < Result.InputMaximumEdgeErrorCm * 0.8);
	TestTrue(TEXT("1307 个固定粒子保持不动"), Result.MaximumFixedParticleDriftCm < 1.e-5);
	return true;
}

#endif
