#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothContinuousCollision.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothContinuousCollisionTest,
	"PhysicsResearch.ContactCloth.CompleteCcdBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothContinuousCollisionTest::RunTest(const FString& Parameters)
{
	const FVector3d Start(-100.0, 0.0, 0.0);
	const FVector3d End(100.0, 0.0, 0.0);
	FContactClothCcdHit SphereHit;
	TestTrue(TEXT("完整 CCD 捕获离散端点均在外部的球隧穿"), FContactClothContinuousCollision::SweepPointSphere(Start, End, FVector3d::ZeroVector, 10.0, SphereHit));
	TestFalse(TEXT("球扫掠不是初始重叠"), SphereHit.bStartedOverlapping);
	TestTrue(TEXT("球最早 TOI 精确"), FMath::Abs(SphereHit.TimeOfImpact - 0.45) < 1.e-12);
	TestTrue(TEXT("球命中位置位于入射表面"), SphereHit.Position.Equals(FVector3d(-10.0, 0.0, 0.0), 1.e-10));
	TestTrue(TEXT("球命中法线朝外"), SphereHit.Normal.Equals(FVector3d(-1.0, 0.0, 0.0), 1.e-10));

	FContactClothCcdHit CapsuleHit;
	TestTrue(TEXT("完整 CCD 捕获胶囊柱段隧穿"), FContactClothContinuousCollision::SweepPointCapsule(
		FVector3d(-50.0, 0.0, 0.0),
		FVector3d(50.0, 0.0, 0.0),
		FVector3d(0.0, 0.0, -20.0),
		FVector3d(0.0, 0.0, 20.0),
		5.0,
		CapsuleHit));
	TestTrue(TEXT("胶囊柱段最早 TOI 精确"), FMath::Abs(CapsuleHit.TimeOfImpact - 0.45) < 1.e-12);
	TestTrue(TEXT("胶囊命中位置位于入射表面"), CapsuleHit.Position.Equals(FVector3d(-5.0, 0.0, 0.0), 1.e-10));

	FContactClothCcdHit CapHit;
	TestTrue(TEXT("完整 CCD 捕获胶囊端帽"), FContactClothContinuousCollision::SweepPointCapsule(
		FVector3d(0.0, 0.0, 50.0),
		FVector3d(0.0, 0.0, 10.0),
		FVector3d(0.0, 0.0, -20.0),
		FVector3d(0.0, 0.0, 20.0),
		5.0,
		CapHit));
	TestTrue(TEXT("胶囊端帽最早 TOI 精确"), FMath::Abs(CapHit.TimeOfImpact - 0.625) < 1.e-12);

	FContactClothCcdHit MissHit;
	TestFalse(TEXT("平行近失不产生伪命中"), FContactClothContinuousCollision::SweepPointCapsule(
		FVector3d(-50.0, 6.0, 0.0),
		FVector3d(50.0, 6.0, 0.0),
		FVector3d(0.0, 0.0, -20.0),
		FVector3d(0.0, 0.0, 20.0),
		5.0,
		MissHit));

	FContactClothCcdHit OverlapHit;
	TestTrue(TEXT("初始重叠被边界锚点识别"), FContactClothContinuousCollision::SweepPointSphere(
		FVector3d(2.0, 0.0, 0.0), FVector3d(20.0, 0.0, 0.0), FVector3d::ZeroVector, 10.0, OverlapHit));
	TestTrue(TEXT("初始重叠标志正确"), OverlapHit.bStartedOverlapping);
	TestEqual(TEXT("初始重叠 TOI 为零"), OverlapHit.TimeOfImpact, 0.0);

	const FVector3d SafeEnd = FMath::Lerp(Start, End, FMath::Max(0.0, SphereHit.TimeOfImpact - 1.e-6));
	TestTrue(TEXT("TOI 前安全锚点仍在球外"), SafeEnd.SquaredLength() > 100.0);
	AddInfo(FString::Printf(
		TEXT("Complete CCD boundary: sphere_toi=%.9f capsule_toi=%.9f cap_toi=%.9f discrete_endpoint_penetration=0"),
		SphereHit.TimeOfImpact,
		CapsuleHit.TimeOfImpact,
		CapHit.TimeOfImpact));
	return true;
}

#endif
