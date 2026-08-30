#pragma once

#include "ContactClothGpuJacobi.h"
#include "CoreMinimal.h"

class UPhysicsAsset;
class USkeletalMesh;
class USkeletalMeshComponent;

struct FContactClothPhysicsSphere
{
	FName BoneName;
	FVector Center = FVector::ZeroVector;
	double RadiusCm = 0.0;
};

struct FContactClothPhysicsCapsule
{
	FName BoneName;
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	double RadiusCm = 0.0;
};

struct FContactClothPhysicsAssetPrimitives
{
	int32 BodyCount = 0;
	int32 SkippedMissingBoneBodies = 0;
	TArray<FContactClothPhysicsSphere> Spheres;
	TArray<FContactClothPhysicsCapsule> Capsules;
};

/** Converts UE Physics Asset sphere and Sphyl geometry into research-solver analytic primitives. */
class PHYSICSCONTACTCLOTHRUNTIME_API FContactClothPhysicsAssetAdapter
{
public:
	static bool ExtractReferencePose(
		const USkeletalMesh& SkeletalMesh,
		const UPhysicsAsset& PhysicsAsset,
		FContactClothPhysicsAssetPrimitives& OutPrimitives);

	static bool ExtractComponentPose(
		const USkeletalMeshComponent& SkeletalMeshComponent,
		FContactClothPhysicsAssetPrimitives& OutPrimitives);

	static bool ExtractComponentSpacePose(
		const USkeletalMeshComponent& SkeletalMeshComponent,
		FContactClothPhysicsAssetPrimitives& OutPrimitives);

	static void ConvertToGpuPrimitives(
		const FContactClothPhysicsAssetPrimitives& Primitives,
		TArray<FContactClothGpuSphere>& OutSpheres,
		TArray<FContactClothGpuCapsule>& OutCapsules);
};
