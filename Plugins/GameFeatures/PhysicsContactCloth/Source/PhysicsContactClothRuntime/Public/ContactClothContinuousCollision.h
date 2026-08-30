#pragma once

#include "CoreMinimal.h"

struct FContactClothCcdHit
{
	bool bHit = false;
	bool bStartedOverlapping = false;
	double TimeOfImpact = 1.0;
	FVector3d Position = FVector3d::ZeroVector;
	FVector3d Normal = FVector3d::UpVector;
};

/** Complete outer-boundary point sweeps for analytic Physics Asset collision primitives. */
class PHYSICSCONTACTCLOTHRUNTIME_API FContactClothContinuousCollision
{
public:
	static bool SweepPointSphere(
		const FVector3d& Start,
		const FVector3d& End,
		const FVector3d& SphereCenter,
		double RadiusCm,
		FContactClothCcdHit& OutHit);

	static bool SweepPointCapsule(
		const FVector3d& Start,
		const FVector3d& End,
		const FVector3d& CapsuleStart,
		const FVector3d& CapsuleEnd,
		double RadiusCm,
		FContactClothCcdHit& OutHit);
};
