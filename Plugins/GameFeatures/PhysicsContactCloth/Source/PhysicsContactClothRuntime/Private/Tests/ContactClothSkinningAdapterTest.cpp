#if WITH_DEV_AUTOMATION_TESTS

#include "ContactClothChaosTopology.h"
#include "ContactClothSkinningAdapter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothSkinningAdapterTest,
	"PhysicsResearch.ContactCloth.ChaosClothSkinningTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothSkinningAdapterTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assassin/Mesh/SKM_Assassin_Skin1.SKM_Assassin_Skin1"));
	TestNotNull(TEXT("加载 Assassin Skeletal Mesh"), Mesh);
	if (!Mesh) return false;
	FContactClothExplicitTopology Topology;
	FString FailureReason;
	TestTrue(TEXT("提取带骨骼合同的 Chaos LOD0 拓扑"), FContactClothChaosTopology::ExtractLod0(*Mesh, Topology, &FailureReason));
	if (!Topology.IsValid())
	{
		AddError(FailureReason);
		return false;
	}

	FContactClothSkinnedTargets ReferenceTargets;
	TestTrue(TEXT("UE Clothing 参考姿态蒙皮成功"), FContactClothSkinningAdapter::SkinReferencePose(
		*Mesh, Topology, ReferenceTargets, &FailureReason));
	double MaximumReferenceErrorCm = 0.0;
	for (int32 ParticleIndex = 0; ParticleIndex < ReferenceTargets.Positions.Num(); ++ParticleIndex)
	{
		MaximumReferenceErrorCm = FMath::Max(MaximumReferenceErrorCm, static_cast<double>(
			(ReferenceTargets.Positions[ParticleIndex] - Topology.RestPositions[ParticleIndex]).Length()));
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	FURL WorldUrl;
	World->InitializeActorsForPlay(WorldUrl);
	World->BeginPlay();
	AActor* Owner = World->SpawnActor<AActor>();
	USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Owner);
	Owner->AddInstanceComponent(Component);
	Component->RegisterComponent();
	Component->SetSkeletalMesh(Mesh, true);
	Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Component->SetForceRefPose(true);
	Component->InitAnim(true);
	Component->RefreshBoneTransforms();
	FContactClothSkinnedTargets ComponentTargets;
	TestTrue(TEXT("真实 SkeletalMeshComponent 参考姿态蒙皮成功"), FContactClothSkinningAdapter::SkinComponentPose(
		*Component, Topology, ComponentTargets, &FailureReason));
	double MaximumComponentReferenceErrorCm = 0.0;
	for (int32 ParticleIndex = 0; ParticleIndex < ComponentTargets.Positions.Num(); ++ParticleIndex)
	{
		MaximumComponentReferenceErrorCm = FMath::Max(MaximumComponentReferenceErrorCm, static_cast<double>(
			(ComponentTargets.Positions[ParticleIndex] - Topology.RestPositions[ParticleIndex]).Length()));
	}
	Component->UnregisterComponent();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	const FTransform UniformPoseTransform(FRotator(0.0, 17.0, 0.0), FVector(12.0, -7.0, 5.0));
	TArray<FMatrix44f> UniformRefToLocalMatrices;
	UniformRefToLocalMatrices.Init(FMatrix44f(UniformPoseTransform.ToMatrixWithScale()), Mesh->GetRefSkeleton().GetNum());
	FContactClothSkinnedTargets TransformedTargets;
	TestTrue(TEXT("合成统一骨骼姿态蒙皮成功"), FContactClothSkinningAdapter::SkinRefToLocalMatrices(
		*Mesh, Topology, UniformRefToLocalMatrices, TransformedTargets, &FailureReason));
	double MaximumUniformTransformErrorCm = 0.0;
	double MinimumFixedTargetMotionCm = TNumericLimits<double>::Max();
	double MaximumFixedTargetMotionCm = 0.0;
	for (int32 ParticleIndex = 0; ParticleIndex < TransformedTargets.Positions.Num(); ++ParticleIndex)
	{
		const FVector3f Expected = UniformRefToLocalMatrices[0].TransformPosition(Topology.RestPositions[ParticleIndex]);
		MaximumUniformTransformErrorCm = FMath::Max(MaximumUniformTransformErrorCm, static_cast<double>(
			(TransformedTargets.Positions[ParticleIndex] - Expected).Length()));
		if (Topology.InverseMasses[ParticleIndex] <= 0.0f)
		{
			const double MotionCm = (TransformedTargets.Positions[ParticleIndex] - ReferenceTargets.Positions[ParticleIndex]).Length();
			MinimumFixedTargetMotionCm = FMath::Min(MinimumFixedTargetMotionCm, MotionCm);
			MaximumFixedTargetMotionCm = FMath::Max(MaximumFixedTargetMotionCm, MotionCm);
		}
	}
	AddInfo(FString::Printf(
		TEXT("Chaos cloth skinning targets: assets=%d vertices=%d fixed=%d dynamic=%d used_bones=%d total_influences=%d max_influences=%d min_weight_sum=%.9f max_weight_sum=%.9f reference_error_cm=%.9f component_reference_error_cm=%.9f uniform_transform_error_cm=%.9f min_fixed_target_motion_cm=%.6f max_fixed_target_motion_cm=%.6f"),
		ReferenceTargets.Stats.AssetCount, ReferenceTargets.Stats.VertexCount, ReferenceTargets.Stats.FixedTargetCount,
		ReferenceTargets.Stats.DynamicTargetCount, ReferenceTargets.Stats.TotalUsedBoneCount,
		ReferenceTargets.Stats.TotalBoneInfluenceCount, ReferenceTargets.Stats.MaxBoneInfluenceCount,
		ReferenceTargets.Stats.MinimumBoneWeightSum, ReferenceTargets.Stats.MaximumBoneWeightSum,
		MaximumReferenceErrorCm, MaximumComponentReferenceErrorCm, MaximumUniformTransformErrorCm, MinimumFixedTargetMotionCm, MaximumFixedTargetMotionCm));
	if (!FailureReason.IsEmpty()) AddError(FailureReason);
	TestEqual(TEXT("蒙皮目标覆盖 2011 个物理顶点"), ReferenceTargets.Positions.Num(), 2011);
	TestEqual(TEXT("固定/动态分区保持 1307/704"), ReferenceTargets.Stats.FixedTargetCount, 1307);
	TestEqual(TEXT("动态粒子数保持 704"), ReferenceTargets.Stats.DynamicTargetCount, 704);
	TestTrue(TEXT("所有物理顶点至少有一个骨骼影响"), ReferenceTargets.Stats.TotalBoneInfluenceCount >= 2011);
	TestTrue(TEXT("最大骨骼影响不超过 UE Clothing 上限"), ReferenceTargets.Stats.MaxBoneInfluenceCount > 0 && ReferenceTargets.Stats.MaxBoneInfluenceCount <= 12);
	TestTrue(TEXT("参考姿态蒙皮复原物理网格静止位置"), MaximumReferenceErrorCm < 1.e-4);
	TestTrue(TEXT("SkeletalMeshComponent 参考姿态复原静止位置"), MaximumComponentReferenceErrorCm < 1.e-3);
	TestTrue(TEXT("统一骨骼矩阵得到统一刚体变换"), MaximumUniformTransformErrorCm < 1.e-3);
	TestTrue(TEXT("全部固定粒子目标随姿态移动"), MinimumFixedTargetMotionCm > 1.0 && MaximumFixedTargetMotionCm < 30.0);
	return true;
}

#endif
