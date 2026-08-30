#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothGpuJacobi.h"
#include "ContactClothPhysicsAssetAdapter.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "RHI.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothAssassinGpuBridgeTest,
	"PhysicsResearch.ContactCloth.AssassinGpuCollisionBridge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothAssassinGpuBridgeTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI 下跳过 Assassin GPU 碰撞桥验证；D3D12 报告单独生成。"));
		return true;
	}

	USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assassin/Mesh/SKM_Assassin_Skin1.SKM_Assassin_Skin1"));
	UPhysicsAsset* PhysicsAsset = LoadObject<UPhysicsAsset>(nullptr, TEXT("/Game/Assassin/Mesh/PHYS_Assassin_Skin1.PHYS_Assassin_Skin1"));
	TestNotNull(TEXT("Assassin Skeletal Mesh 可供 GPU 桥加载"), SkeletalMesh);
	TestNotNull(TEXT("Assassin Physics Asset 可供 GPU 桥加载"), PhysicsAsset);
	if (!SkeletalMesh || !PhysicsAsset)
	{
		return false;
	}

	FContactClothPhysicsAssetPrimitives PhysicsPrimitives;
	TestTrue(TEXT("Assassin 参考姿态可解析"), FContactClothPhysicsAssetAdapter::ExtractReferencePose(*SkeletalMesh, *PhysicsAsset, PhysicsPrimitives));
	TArray<FContactClothGpuSphere> GpuSpheres;
	TArray<FContactClothGpuCapsule> GpuCapsules;
	FContactClothPhysicsAssetAdapter::ConvertToGpuPrimitives(PhysicsPrimitives, GpuSpheres, GpuCapsules);
	TestEqual(TEXT("完整 Assassin 球集合进入 GPU 合同"), GpuSpheres.Num(), PhysicsPrimitives.Spheres.Num());
	TestEqual(TEXT("完整 Assassin 胶囊集合进入 GPU 合同"), GpuCapsules.Num(), PhysicsPrimitives.Capsules.Num());
	if (GpuSpheres.IsEmpty())
	{
		return false;
	}

	FContactClothGpuJacobiSettings Settings;
	Settings.Columns = 4;
	Settings.Rows = 4;
	Settings.SpacingCm = 10.0f;
	Settings.Iterations = 1;
	Settings.Relaxation = 0.0f;
	Settings.CollisionThicknessCm = 1.0f;
	Settings.SphereRadiusCm = 0.0f;
	Settings.CapsuleRadiusCm = 0.0f;
	Settings.CollisionSpheres = GpuSpheres;
	Settings.CollisionCapsules = GpuCapsules;

	TArray<FVector3f> InputPositions;
	for (int32 Row = 0; Row < Settings.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Settings.Columns; ++Column)
		{
			InputPositions.Emplace(10000.0f + Column * Settings.SpacingCm, 10000.0f, 10000.0f - Row * Settings.SpacingCm);
		}
	}
	const int32 ProbeIndex = Settings.Columns;
	InputPositions[ProbeIndex] = GpuSpheres[0].Center;
	const FContactClothGpuJacobiResult Result = FContactClothGpuJacobi::RunBlocking(InputPositions, Settings);
	const double ProbeDisplacementCm = Result.Positions.IsValidIndex(ProbeIndex)
		? static_cast<double>((Result.Positions[ProbeIndex] - InputPositions[ProbeIndex]).Length())
		: 0.0;

	TestTrue(TEXT("Assassin GPU 桥 Compute Shader 已派发"), Result.bDispatched);
	TestTrue(TEXT("Assassin GPU 桥结果成功读回"), Result.bReadbackSucceeded);
	TestEqual(TEXT("Assassin GPU 桥输出粒子数"), Result.Positions.Num(), InputPositions.Num());
	TestTrue(TEXT("真实 Physics Asset 球体把探针粒子推出"), ProbeDisplacementCm > GpuSpheres[0].RadiusCm * 0.5f);
	AddInfo(FString::Printf(
		TEXT("Assassin GPU collision bridge: bodies=%d spheres=%d capsules=%d probe_displacement_cm=%.6f"),
		PhysicsPrimitives.BodyCount,
		GpuSpheres.Num(),
		GpuCapsules.Num(),
		ProbeDisplacementCm));
	return true;
}

#endif
