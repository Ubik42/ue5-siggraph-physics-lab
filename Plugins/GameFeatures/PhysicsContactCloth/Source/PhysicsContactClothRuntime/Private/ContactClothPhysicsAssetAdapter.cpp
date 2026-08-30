#include "ContactClothPhysicsAssetAdapter.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"

namespace ContactClothPhysicsAssetAdapter
{
using FBoneTransformResolver = TFunction<FTransform(FName)>;

void AppendBodyPrimitives(
	const USkeletalBodySetup& BodySetup,
	const FTransform& BoneTransform,
	FContactClothPhysicsAssetPrimitives& OutPrimitives)
{
	for (const FKSphereElem& Sphere : BodySetup.AggGeom.SphereElems)
	{
		const FTransform ShapeTransform = Sphere.GetTransform() * BoneTransform;
		FContactClothPhysicsSphere& Output = OutPrimitives.Spheres.Emplace_GetRef();
		Output.BoneName = BodySetup.BoneName;
		Output.Center = ShapeTransform.GetLocation();
		Output.RadiusCm = Sphere.Radius * ShapeTransform.GetScale3D().GetAbsMax();
	}

	for (const FKSphylElem& Capsule : BodySetup.AggGeom.SphylElems)
	{
		const FTransform ShapeTransform = Capsule.GetTransform() * BoneTransform;
		const FVector HalfSegment = ShapeTransform.TransformVector(FVector(0.0, 0.0, Capsule.Length * 0.5));
		const double RadialScale = FMath::Max(
			ShapeTransform.TransformVector(FVector::XAxisVector).Length(),
			ShapeTransform.TransformVector(FVector::YAxisVector).Length());
		FContactClothPhysicsCapsule& Output = OutPrimitives.Capsules.Emplace_GetRef();
		Output.BoneName = BodySetup.BoneName;
		Output.Start = ShapeTransform.GetLocation() - HalfSegment;
		Output.End = ShapeTransform.GetLocation() + HalfSegment;
		Output.RadiusCm = Capsule.Radius * RadialScale;
	}
}

bool Extract(
	const UPhysicsAsset& PhysicsAsset,
	const FBoneTransformResolver& ResolveBoneTransform,
	const TFunction<bool(FName)>& HasBone,
	FContactClothPhysicsAssetPrimitives& OutPrimitives)
{
	OutPrimitives = FContactClothPhysicsAssetPrimitives();
	OutPrimitives.BodyCount = PhysicsAsset.SkeletalBodySetups.Num();
	for (const TObjectPtr<USkeletalBodySetup>& BodySetup : PhysicsAsset.SkeletalBodySetups)
	{
		if (!BodySetup)
		{
			continue;
		}
		if (!HasBone(BodySetup->BoneName))
		{
			OutPrimitives.SkippedMissingBoneBodies++;
			continue;
		}
		AppendBodyPrimitives(*BodySetup, ResolveBoneTransform(BodySetup->BoneName), OutPrimitives);
	}
	return OutPrimitives.BodyCount > 0 && (OutPrimitives.Spheres.Num() > 0 || OutPrimitives.Capsules.Num() > 0);
}
}

bool FContactClothPhysicsAssetAdapter::ExtractReferencePose(
	const USkeletalMesh& SkeletalMesh,
	const UPhysicsAsset& PhysicsAsset,
	FContactClothPhysicsAssetPrimitives& OutPrimitives)
{
	const FReferenceSkeleton& ReferenceSkeleton = SkeletalMesh.GetRefSkeleton();
	const TArray<FTransform>& LocalPose = ReferenceSkeleton.GetRefBonePose();
	TArray<FTransform> ComponentPose;
	ComponentPose.SetNum(LocalPose.Num());
	for (int32 BoneIndex = 0; BoneIndex < LocalPose.Num(); ++BoneIndex)
	{
		const int32 ParentIndex = ReferenceSkeleton.GetParentIndex(BoneIndex);
		ComponentPose[BoneIndex] = ParentIndex == INDEX_NONE ? LocalPose[BoneIndex] : LocalPose[BoneIndex] * ComponentPose[ParentIndex];
	}
	return ContactClothPhysicsAssetAdapter::Extract(
		PhysicsAsset,
		[&ReferenceSkeleton, &ComponentPose](FName BoneName)
		{
			return ComponentPose[ReferenceSkeleton.FindBoneIndex(BoneName)];
		},
		[&ReferenceSkeleton](FName BoneName)
		{
			return ReferenceSkeleton.FindBoneIndex(BoneName) != INDEX_NONE;
		},
		OutPrimitives);
}

bool FContactClothPhysicsAssetAdapter::ExtractComponentPose(
	const USkeletalMeshComponent& SkeletalMeshComponent,
	FContactClothPhysicsAssetPrimitives& OutPrimitives)
{
	const UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent.GetPhysicsAsset();
	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent.GetSkeletalMeshAsset();
	if (!PhysicsAsset || !SkeletalMesh)
	{
		OutPrimitives = FContactClothPhysicsAssetPrimitives();
		return false;
	}
	return ContactClothPhysicsAssetAdapter::Extract(
		*PhysicsAsset,
		[&SkeletalMeshComponent](FName BoneName)
		{
			return SkeletalMeshComponent.GetBoneTransform(SkeletalMeshComponent.GetBoneIndex(BoneName));
		},
		[&SkeletalMeshComponent](FName BoneName)
		{
			return SkeletalMeshComponent.GetBoneIndex(BoneName) != INDEX_NONE;
		},
		OutPrimitives);
}

bool FContactClothPhysicsAssetAdapter::ExtractComponentSpacePose(
	const USkeletalMeshComponent& SkeletalMeshComponent,
	FContactClothPhysicsAssetPrimitives& OutPrimitives)
{
	const UPhysicsAsset* PhysicsAsset = SkeletalMeshComponent.GetPhysicsAsset();
	const USkeletalMesh* SkeletalMesh = SkeletalMeshComponent.GetSkeletalMeshAsset();
	if (!PhysicsAsset || !SkeletalMesh)
	{
		OutPrimitives = FContactClothPhysicsAssetPrimitives();
		return false;
	}
	return ContactClothPhysicsAssetAdapter::Extract(
		*PhysicsAsset,
		[&SkeletalMeshComponent](FName BoneName)
		{
			return SkeletalMeshComponent.GetBoneTransform(SkeletalMeshComponent.GetBoneIndex(BoneName), FTransform::Identity);
		},
		[&SkeletalMeshComponent](FName BoneName)
		{
			return SkeletalMeshComponent.GetBoneIndex(BoneName) != INDEX_NONE;
		},
		OutPrimitives);
}

void FContactClothPhysicsAssetAdapter::ConvertToGpuPrimitives(
	const FContactClothPhysicsAssetPrimitives& Primitives,
	TArray<FContactClothGpuSphere>& OutSpheres,
	TArray<FContactClothGpuCapsule>& OutCapsules)
{
	OutSpheres.Reset(Primitives.Spheres.Num());
	for (const FContactClothPhysicsSphere& Sphere : Primitives.Spheres)
	{
		if (Sphere.RadiusCm > 0.0 && FMath::IsFinite(Sphere.RadiusCm) && !Sphere.Center.ContainsNaN())
		{
			OutSpheres.Add({FVector3f(Sphere.Center), static_cast<float>(Sphere.RadiusCm)});
		}
	}
	OutCapsules.Reset(Primitives.Capsules.Num());
	for (const FContactClothPhysicsCapsule& Capsule : Primitives.Capsules)
	{
		if (Capsule.RadiusCm > 0.0 && FMath::IsFinite(Capsule.RadiusCm)
			&& !Capsule.Start.ContainsNaN() && !Capsule.End.ContainsNaN())
		{
			OutCapsules.Add({FVector3f(Capsule.Start), FVector3f(Capsule.End), static_cast<float>(Capsule.RadiusCm)});
		}
	}
}
