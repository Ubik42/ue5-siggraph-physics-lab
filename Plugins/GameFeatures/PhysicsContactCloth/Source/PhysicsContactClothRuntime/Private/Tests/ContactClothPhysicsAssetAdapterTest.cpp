#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothPhysicsAssetAdapter.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/PhysicsAsset.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothPhysicsAssetAdapterTest,
	"PhysicsResearch.ContactCloth.AssassinPhysicsAssetAdapter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothPhysicsAssetAdapterTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assassin/Mesh/SKM_Assassin_Skin1.SKM_Assassin_Skin1"));
	UPhysicsAsset* PhysicsAsset = LoadObject<UPhysicsAsset>(nullptr, TEXT("/Game/Assassin/Mesh/PHYS_Assassin_Skin1.PHYS_Assassin_Skin1"));
	TestNotNull(TEXT("Assassin Skeletal Mesh 可加载"), SkeletalMesh);
	TestNotNull(TEXT("Assassin Physics Asset 可加载"), PhysicsAsset);
	if (!SkeletalMesh || !PhysicsAsset)
	{
		return false;
	}

	FContactClothPhysicsAssetPrimitives Primitives;
	TestTrue(TEXT("参考姿态 Physics Asset 可转换"), FContactClothPhysicsAssetAdapter::ExtractReferencePose(*SkeletalMesh, *PhysicsAsset, Primitives));
	TestTrue(TEXT("Physics Asset 包含刚体"), Primitives.BodyCount > 0);
	TestEqual(TEXT("所有刚体骨骼均可在网格中解析"), Primitives.SkippedMissingBoneBodies, 0);
	TestTrue(TEXT("至少存在一个球或胶囊解析体"), Primitives.Spheres.Num() + Primitives.Capsules.Num() > 0);

	bool bAllFiniteAndPositive = true;
	for (const FContactClothPhysicsSphere& Sphere : Primitives.Spheres)
	{
		bAllFiniteAndPositive &= !Sphere.BoneName.IsNone() && !Sphere.Center.ContainsNaN() && FMath::IsFinite(Sphere.RadiusCm) && Sphere.RadiusCm > 0.0;
	}
	for (const FContactClothPhysicsCapsule& Capsule : Primitives.Capsules)
	{
		bAllFiniteAndPositive &= !Capsule.BoneName.IsNone() && !Capsule.Start.ContainsNaN() && !Capsule.End.ContainsNaN()
			&& FMath::IsFinite(Capsule.RadiusCm) && Capsule.RadiusCm > 0.0 && !Capsule.Start.Equals(Capsule.End, 1.e-6);
	}
	TestTrue(TEXT("全部解析体具有有限正尺寸"), bAllFiniteAndPositive);
	AddInfo(FString::Printf(
		TEXT("Assassin Physics Asset adapter: bodies=%d spheres=%d capsules=%d skipped_missing_bones=%d"),
		Primitives.BodyCount,
		Primitives.Spheres.Num(),
		Primitives.Capsules.Num(),
		Primitives.SkippedMissingBoneBodies));
	return true;
}

#endif
