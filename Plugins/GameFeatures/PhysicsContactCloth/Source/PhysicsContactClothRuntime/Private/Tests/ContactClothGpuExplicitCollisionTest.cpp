#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothChaosTopology.h"
#include "ContactClothGpuExplicitTopology.h"
#include "ContactClothPhysicsAssetAdapter.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/PhysicsAsset.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothGpuExplicitCollisionTest,
	"PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.PhysicsAssetCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothGpuExplicitCollisionTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assassin/Mesh/SKM_Assassin_Skin1.SKM_Assassin_Skin1"));
	UPhysicsAsset* PhysicsAsset = LoadObject<UPhysicsAsset>(nullptr, TEXT("/Game/Assassin/Mesh/PHYS_Assassin_Skin1.PHYS_Assassin_Skin1"));
	TestNotNull(TEXT("加载 Assassin Skeletal Mesh"), Mesh);
	TestNotNull(TEXT("加载 Assassin Physics Asset"), PhysicsAsset);
	if (!Mesh || !PhysicsAsset) return false;

	FContactClothExplicitTopology Topology;
	FString FailureReason;
	TestTrue(TEXT("提取 Assassin 显式拓扑"), FContactClothChaosTopology::ExtractLod0(*Mesh, Topology, &FailureReason));
	FContactClothPhysicsAssetPrimitives Primitives;
	TestTrue(TEXT("提取参考姿态分析碰撞体"), FContactClothPhysicsAssetAdapter::ExtractReferencePose(*Mesh, *PhysicsAsset, Primitives));
	TArray<FContactClothGpuSphere> AllSpheres;
	TArray<FContactClothGpuCapsule> AllCapsules;
	FContactClothPhysicsAssetAdapter::ConvertToGpuPrimitives(Primitives, AllSpheres, AllCapsules);
	TestEqual(TEXT("Assassin Physics Asset 包含 1 个分析球"), AllSpheres.Num(), 1);
	TestEqual(TEXT("Assassin Physics Asset 包含 16 个分析胶囊"), AllCapsules.Num(), 16);
	if (!Topology.IsValid() || AllSpheres.IsEmpty()) return false;

	TArray<FVector3f> InitialPositions = Topology.RestPositions;
	int32 ProbeParticle = INDEX_NONE;
	for (int32 Index = 0; Index < Topology.InverseMasses.Num(); ++Index)
	{
		if (Topology.InverseMasses[Index] > 0.0f)
		{
			ProbeParticle = Index;
			break;
		}
	}
	TestTrue(TEXT("找到动态探针粒子"), ProbeParticle != INDEX_NONE);
	if (ProbeParticle == INDEX_NONE) return false;
	InitialPositions[ProbeParticle] = AllSpheres[0].Center;
	const TArray<FContactClothGpuSphere> TestSpheres = {AllSpheres[0]};
	const TArray<FContactClothGpuCapsule> TestCapsules;

	FContactClothGpuExplicitTopologyRealtimeSettings Settings;
	Settings.Iterations = 32;
	Settings.Relaxation = 0.9f;
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.GravityCmPerSecondSquared = FVector3f::ZeroVector;
	Settings.VelocityDamping = 0.0f;
	Settings.ReadbackIntervalSteps = 1;
	Settings.CollisionThicknessCm = 1.0f;
	FContactClothGpuExplicitTopologySimulation Simulation;
	TestTrue(TEXT("初始化显式拓扑碰撞会话"), Simulation.Initialize(
		InitialPositions, Topology.InverseMasses, Topology.StructuralEdges, Topology.StructuralRestLengthsCm, Settings));
	TestTrue(TEXT("上传分析球碰撞快照"), Simulation.UpdateCollisionPrimitives(TestSpheres, TestCapsules));
	TestTrue(TEXT("碰撞固定步入队"), Simulation.EnqueueStep());
	Simulation.FlushAndWaitForReadbackForValidation();

	TArray<FVector3f> OutputPositions;
	int64 ReadbackStep = 0;
	const bool bConsumed = Simulation.ConsumeLatestPositions(OutputPositions, ReadbackStep);
	const FContactClothGpuExplicitTopologyRealtimeStats Stats = Simulation.GetStats();
	const double InputPenetrationCm = FContactClothGpuExplicitTopology::MeasureMaximumCollisionPenetrationCm(
		InitialPositions, Topology.InverseMasses, TestSpheres, TestCapsules, Settings.CollisionThicknessCm);
	const double OutputPenetrationCm = FContactClothGpuExplicitTopology::MeasureMaximumCollisionPenetrationCm(
		OutputPositions, Topology.InverseMasses, TestSpheres, TestCapsules, Settings.CollisionThicknessCm);
	AddInfo(FString::Printf(
		TEXT("GPU explicit Physics Asset collision: particles=%d probe=%d spheres=%d capsules=%d steps=%lld collision_updates=%lld collision_applications=%lld collision_version=%lld input_penetration_cm=%.6f output_penetration_cm=%.9f"),
		Topology.RestPositions.Num(), ProbeParticle, Stats.LatestCollisionSphereCount, Stats.LatestCollisionCapsuleCount,
		Stats.StepsExecuted, Stats.CollisionPrimitiveUpdatesAccepted, Stats.CollisionPrimitiveApplicationsExecuted,
		Stats.LatestCollisionPrimitiveVersion, InputPenetrationCm, OutputPenetrationCm));
	TestTrue(TEXT("最终 GPU 快照可消费"), bConsumed);
	TestEqual(TEXT("碰撞固定步执行一次"), Stats.StepsExecuted, static_cast<int64>(1));
	TestEqual(TEXT("碰撞快照更新一次"), Stats.CollisionPrimitiveUpdatesAccepted, static_cast<int64>(1));
	TestEqual(TEXT("碰撞快照应用一次"), Stats.CollisionPrimitiveApplicationsExecuted, static_cast<int64>(1));
	TestEqual(TEXT("最终碰撞版本为 1"), Stats.LatestCollisionPrimitiveVersion, static_cast<int64>(1));
	TestEqual(TEXT("GPU 使用一个分析球"), Stats.LatestCollisionSphereCount, 1);
	TestEqual(TEXT("GPU 不使用胶囊"), Stats.LatestCollisionCapsuleCount, 0);
	TestEqual(TEXT("最终快照对应第一步"), ReadbackStep, static_cast<int64>(1));
	TestTrue(TEXT("构造输入具有明显穿透"), InputPenetrationCm > AllSpheres[0].RadiusCm);
	TestTrue(TEXT("GPU 投影消除分析球穿透"), FMath::IsFinite(OutputPenetrationCm) && OutputPenetrationCm < 1.e-3);
	Simulation.Shutdown();
	return true;
}

#endif
