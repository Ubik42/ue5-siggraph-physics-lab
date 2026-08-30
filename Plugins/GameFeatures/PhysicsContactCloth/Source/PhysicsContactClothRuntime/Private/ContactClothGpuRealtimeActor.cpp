#include "ContactClothGpuRealtimeActor.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContactClothPhysicsAssetAdapter.h"
#include "ContactClothModalSubspace.h"
#include "DrawDebugHelpers.h"
#include "Animation/AnimSequence.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PhysicsExperimentComponent.h"
#include "PhysicsResearchTypes.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogContactClothGpuRealtime, Log, All);

namespace ContactClothGpuRealtimeActor
{
double MeasureMaximumPrimitivePenetration(
	TConstArrayView<FVector3f> Positions,
	int32 PinnedParticleCount,
	TConstArrayView<FContactClothGpuSphere> Spheres,
	TConstArrayView<FContactClothGpuCapsule> Capsules,
	float ThicknessCm)
{
	double MaximumPenetration = 0.0;
	for (int32 Index = FMath::Max(0, PinnedParticleCount); Index < Positions.Num(); ++Index)
	{
		for (const FContactClothGpuSphere& Sphere : Spheres)
		{
			MaximumPenetration = FMath::Max(
				MaximumPenetration,
				static_cast<double>(Sphere.RadiusCm + ThicknessCm - (Positions[Index] - Sphere.Center).Length()));
		}
		for (const FContactClothGpuCapsule& Capsule : Capsules)
		{
			const FVector3f Segment = Capsule.End - Capsule.Start;
			const float SegmentLengthSquared = Segment.SquaredLength();
			const float Alpha = SegmentLengthSquared > UE_SMALL_NUMBER
				? FMath::Clamp(FVector3f::DotProduct(Positions[Index] - Capsule.Start, Segment) / SegmentLengthSquared, 0.0f, 1.0f)
				: 0.0f;
			const FVector3f ClosestPoint = Capsule.Start + Alpha * Segment;
			MaximumPenetration = FMath::Max(
				MaximumPenetration,
				static_cast<double>(Capsule.RadiusCm + ThicknessCm - (Positions[Index] - ClosestPoint).Length()));
		}
	}
	return FMath::Max(MaximumPenetration, 0.0);
}
}

AContactClothGpuRealtimeActor::AContactClothGpuRealtimeActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ExperimentComponent = CreateDefaultSubobject<UPhysicsExperimentComponent>(TEXT("Experiment"));
	ExperimentComponent->ExperimentCase = EPhysicsResearchCase::ContactCloth;
	ExperimentComponent->SolverBackend = EPhysicsSolverBackend::ResearchSolver;
	ExperimentComponent->DeterministicSeed = 20260830;
	ExperimentComponent->MaxMetricSamples = 4096;
	ExperimentComponent->StepConfig.FixedDeltaSeconds = 1.0 / 60.0;
	ExperimentComponent->bAutoStart = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	FloorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor"));
	FloorMesh->SetupAttachment(Root);
	FloorMesh->SetStaticMesh(CubeMesh.Object);
	FloorMesh->SetRelativeLocation(FVector(0.0, 0.0, -8.0));
	FloorMesh->SetRelativeScale3D(FVector(7.0, 5.0, 0.08));

	CapsuleMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CapsuleCollisionMarker"));
	CapsuleMarker->SetupAttachment(Root);
	CapsuleMarker->SetStaticMesh(CylinderMesh.Object);
	CapsuleMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CapsuleMarker->SetRelativeLocation(FVector(0.0, 0.0, 140.0));
	CapsuleMarker->SetRelativeRotation(FRotator(90.0, 0.0, 0.0));
	CapsuleMarker->SetRelativeScale3D(FVector(0.72f, 0.72f, 1.1f));
	CapsuleMarker->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> AssassinMeshAsset(TEXT("/Game/Assassin/Mesh/SKM_Assassin_Skin1.SKM_Assassin_Skin1"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> AssassinIdleAsset(TEXT("/Game/Assassin/Animation/Anim_Assassin_idle1.Anim_Assassin_idle1"));
	AssassinMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("AssassinCollisionSource"));
	AssassinMesh->SetupAttachment(Root);
	AssassinMesh->SetSkeletalMesh(AssassinMeshAsset.Object);
	AssassinMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AssassinMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	AssassinMesh->SetAnimation(AssassinIdleAsset.Object);
	AssassinMesh->SetPlayRate(0.55f);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetRelativeLocation(FVector(420.0, -850.0, 300.0));
	Camera->SetRelativeRotation(FRotator(-4.0, 116.0, 0.0));
	Camera->FieldOfView = 48.0f;
	Camera->bAutoActivate = true;

	DirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("KeyLight"));
	DirectionalLight->SetupAttachment(Root);
	DirectionalLight->SetRelativeRotation(FRotator(-42.0, -25.0, 0.0));
	DirectionalLight->SetIntensity(5.0f);

	PointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	PointLight->SetupAttachment(Root);
	PointLight->SetRelativeLocation(FVector(-120.0, -260.0, 360.0));
	PointLight->SetIntensity(2600.0f);
	PointLight->SetAttenuationRadius(1200.0f);
	PointLight->SetLightColor(FLinearColor(0.08f, 0.55f, 1.0f));
}

void AContactClothGpuRealtimeActor::BeginPlay()
{
	Super::BeginPlay();
	bCaptureRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsClothGpuRealtimeCapture"));
	bAutoQuitRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsClothGpuRealtimeAutoQuit"));

	Settings.Columns = 20;
	Settings.Rows = 15;
	Settings.SpacingCm = 12.0f;
	Settings.Iterations = 24;
	Settings.Relaxation = 0.9f;
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.GravityCmPerSecondSquared = FVector3f(0.0f, 0.0f, -980.0f);
	Settings.VelocityDamping = 0.995f;
	Settings.ReadbackIntervalSteps = 4;
	Settings.CapsuleRadiusCm = 0.0f;
	Settings.CollisionThicknessCm = 1.0f;
	Settings.SphereRadiusCm = 0.0f;
	Settings.bEnableNonDistanceBarrier = true;
	Settings.NdbBaseWeight = 1.0f;
	Settings.NdbActivationMultiplier = 2.0f;
	Settings.NdbMaximumWeight = 1048576.0f;
	Settings.bEnableRankOneCollisionHessian = true;
	Settings.RankOneMaximumCorrectionCm = 4.0f;
	Settings.bEnableCompleteCcd = true;
	Settings.CcdSafetyOffsetCm = 0.01f;
	Settings.bEnableResidualReuse = true;
	Settings.ResidualForwardScale = 0.4f;
	Settings.MaximumResidualCm = 2.0f;
	Settings.MaximumResidualCcdCorrectionCm = 0.25f;
	BuildInitialGrid();
	if (!ConfigureModalSubspace())
	{
		StatusText = TEXT("失败：12 维缩减模态系统无法构建");
		WriteRunReport();
		return;
	}
	if (!UpdateAssassinPoseColliders())
	{
		StatusText = TEXT("失败：Assassin 动态 Physics Asset 无法解析");
		WriteRunReport();
		return;
	}
	Settings.CollisionSpheres = CollisionSpheres;
	Settings.CollisionCapsules = CollisionCapsules;
	Settings.CollisionPrimitiveCapacity = CollisionSpheres.Num() + CollisionCapsules.Num();

	if (!GpuSimulation.Initialize(InitialPositions, Settings))
	{
		StatusText = TEXT("失败：持久 GPU 会话初始化被拒绝");
		WriteRunReport();
		return;
	}
	ExperimentComponent->OnExperimentStep.AddDynamic(this, &ThisClass::HandleExperimentStep);
	ExperimentComponent->ResetExperiment();
	ExperimentComponent->StartExperiment();
	AssassinMesh->Play(true);
	StatusText = TEXT("运行：缩减全局步 + rank-one NDB + CCD + 安全残差转发");
	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		PlayerController->SetViewTarget(this);
	}
	UE_LOG(LogContactClothGpuRealtime, Display, TEXT("M2_GPU_REALTIME_STARTED particles=%d iterations=%d"), InitialPositions.Num(), Settings.Iterations);
}

void AContactClothGpuRealtimeActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GpuSimulation.Shutdown();
	Super::EndPlay(EndPlayReason);
}

void AContactClothGpuRealtimeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	GpuSimulation.PollReadback();
	if (bAwaitingFinalReadback)
	{
		GpuSimulation.RequestReadback();
	}
	ProcessLatestReadback();
	DrawClothDebug();
}

void AContactClothGpuRealtimeActor::HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex)
{
	if (!bRunComplete && !bAwaitingFinalReadback)
	{
		UpdateAssassinPoseColliders();
		GpuSimulation.UpdateCollisionPrimitives(CollisionSpheres, CollisionCapsules);
		GpuSimulation.EnqueueStep();
	}
	if (StepIndex >= StepsToRun && !bAwaitingFinalReadback)
	{
		ExperimentComponent->PauseExperiment();
		bAwaitingFinalReadback = true;
		StatusText = TEXT("GPU 已排入 300 步，等待最终异步快照");
		GpuSimulation.RequestReadback();
	}
}

void AContactClothGpuRealtimeActor::BuildInitialGrid()
{
	InitialPositions.Reset(Settings.Columns * Settings.Rows);
	const float HalfWidth = (Settings.Columns - 1) * Settings.SpacingCm * 0.5f;
	for (int32 Row = 0; Row < Settings.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Settings.Columns; ++Column)
		{
			InitialPositions.Emplace(Column * Settings.SpacingCm - HalfWidth, -12.0f, 185.0f - Row * Settings.SpacingCm);
		}
	}
	LatestPositions = InitialPositions;
}

bool AContactClothGpuRealtimeActor::ConfigureModalSubspace()
{
	constexpr int32 ModeCount = 12;
	FContactClothModalBasis Basis;
	FContactClothReducedSystem ReducedSystem;
	FContactClothRankOneProjectionCache ProjectionCache;
	TArray<double> InverseSystem;
	if (!FContactClothModalSubspace::BuildPinnedGridBasis(Settings.Columns, Settings.Rows, ModeCount, Basis)
		|| !FContactClothModalSubspace::BuildReducedGridSystem(Basis, Settings.Columns, Settings.Rows, 1.0, 1.0, ReducedSystem)
		|| !FContactClothModalSubspace::BuildRankOneProjectionCache(Basis, ProjectionCache)
		|| !FContactClothModalSubspace::InvertReducedSystem(ReducedSystem, InverseSystem))
	{
		return false;
	}
	Settings.ModalModeCount = ModeCount;
	Settings.ModalCorrectionStrength = 1.0f;
	Settings.ModalBasisValues.Reset(Basis.Values.Num());
	for (double Value : Basis.Values)
	{
		Settings.ModalBasisValues.Add(static_cast<float>(Value));
	}
	Settings.ModalInverseSystemValues.Reset(InverseSystem.Num());
	for (double Value : InverseSystem)
	{
		Settings.ModalInverseSystemValues.Add(static_cast<float>(Value));
	}
	Settings.ModalSystemValues.Reset(ReducedSystem.Matrix.Num());
	for (double Value : ReducedSystem.Matrix)
	{
		Settings.ModalSystemValues.Add(static_cast<float>(Value));
	}
	Settings.ModalParticleOuterProductValues.Reset(ProjectionCache.Values.Num());
	for (double Value : ProjectionCache.Values)
	{
		Settings.ModalParticleOuterProductValues.Add(static_cast<float>(Value));
	}
	return true;
}

bool AContactClothGpuRealtimeActor::UpdateAssassinPoseColliders()
{
	if (!AssassinMesh)
	{
		return false;
	}
	FContactClothPhysicsAssetPrimitives Primitives;
	if (!FContactClothPhysicsAssetAdapter::ExtractComponentPose(*AssassinMesh, Primitives))
	{
		return false;
	}
	FContactClothPhysicsAssetAdapter::ConvertToGpuPrimitives(Primitives, CollisionSpheres, CollisionCapsules);
	PoseUpdateCount++;
	return !CollisionSpheres.IsEmpty() || !CollisionCapsules.IsEmpty();
}

void AContactClothGpuRealtimeActor::ProcessLatestReadback()
{
	TArray<FVector3f> Positions;
	int64 ReadbackStep = 0;
	if (!GpuSimulation.ConsumeLatestPositions(Positions, ReadbackStep))
	{
		return;
	}
	LatestPositions = MoveTemp(Positions);
	LatestReadbackStep = ReadbackStep;
	MaximumEdgeErrorCm = FContactClothGpuJacobi::MeasureMaximumStructuralEdgeErrorCm(LatestPositions, Settings.Columns, Settings.Rows, Settings.SpacingCm);
	MaximumCapsulePenetrationCm = ContactClothGpuRealtimeActor::MeasureMaximumPrimitivePenetration(
		LatestPositions,
		Settings.Columns,
		CollisionSpheres,
		CollisionCapsules,
		Settings.CollisionThicknessCm);
	const int32 BottomCenter = (Settings.Rows - 1) * Settings.Columns + Settings.Columns / 2;
	BottomDisplacementCm = InitialPositions.IsValidIndex(BottomCenter) && LatestPositions.IsValidIndex(BottomCenter)
		? InitialPositions[BottomCenter].Z - LatestPositions[BottomCenter].Z
		: 0.0;
	const int64 CurrentStep = ExperimentComponent->GetExperimentStats().StepIndex;
	const FContactClothGpuRealtimeStats GpuStats = GpuSimulation.GetStats();
	ExperimentComponent->RecordMetric(TEXT("MaximumEdgeError"), MaximumEdgeErrorCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("MaximumCapsulePenetration"), MaximumCapsulePenetrationCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("BottomDisplacement"), BottomDisplacementCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("ReadbackLagSteps"), FMath::Max<int64>(0, CurrentStep - LatestReadbackStep), TEXT("steps"));
	ExperimentComponent->RecordMetric(TEXT("ActiveContactPairs"), GpuStats.LatestActiveContactPairs, TEXT("pairs"));
	ExperimentComponent->RecordMetric(TEXT("MaximumNdbActivationCount"), GpuStats.LatestMaximumNdbActivationCount, TEXT("steps"));
	ExperimentComponent->RecordMetric(TEXT("RenderSubmissionTime"), GpuStats.LatestRenderSubmissionMilliseconds, TEXT("ms"));

	if (bAwaitingFinalReadback && LatestReadbackStep >= StepsToRun)
	{
		CompleteRun();
	}
}

void AContactClothGpuRealtimeActor::DrawClothDebug() const
{
	if (!GetWorld() || LatestPositions.Num() != Settings.Columns * Settings.Rows)
	{
		return;
	}
	for (int32 Row = 0; Row < Settings.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Settings.Columns; ++Column)
		{
			const int32 Index = Row * Settings.Columns + Column;
			const FVector Position(LatestPositions[Index]);
			if (Column + 1 < Settings.Columns)
			{
				DrawDebugLine(GetWorld(), Position, FVector(LatestPositions[Index + 1]), FColor(30, 220, 255), false, 0.0f, 0, 1.1f);
			}
			if (Row + 1 < Settings.Rows)
			{
				DrawDebugLine(GetWorld(), Position, FVector(LatestPositions[Index + Settings.Columns]), FColor(60, 155, 255), false, 0.0f, 0, 1.1f);
			}
			DrawDebugPoint(GetWorld(), Position, Row == 0 ? 7.0f : 3.5f, Row == 0 ? FColor::Red : FColor::White, false, 0.0f, 0);
		}
	}
	for (const FContactClothGpuSphere& Sphere : CollisionSpheres)
	{
		DrawDebugSphere(GetWorld(), FVector(Sphere.Center), Sphere.RadiusCm, 12, FColor(255, 110, 30), false, 0.0f, 0, 1.0f);
	}
	for (const FContactClothGpuCapsule& Capsule : CollisionCapsules)
	{
		const FVector Segment = FVector(Capsule.End - Capsule.Start);
		const FVector Center = FVector(Capsule.Start + Capsule.End) * 0.5;
		const FQuat Rotation = Segment.IsNearlyZero() ? FQuat::Identity : FRotationMatrix::MakeFromZ(Segment).ToQuat();
		DrawDebugCapsule(GetWorld(), Center, Segment.Length() * 0.5f + Capsule.RadiusCm, Capsule.RadiusCm, Rotation, FColor(255, 175, 40), false, 0.0f, 0, 1.0f);
	}
}

void AContactClothGpuRealtimeActor::CompleteRun()
{
	bAwaitingFinalReadback = false;
	bRunComplete = true;
	const FContactClothGpuRealtimeStats Stats = GpuSimulation.GetStats();
	bRunPassed = LatestReadbackStep >= StepsToRun
		&& Stats.StepsExecuted == StepsToRun
		&& Stats.ReadbacksCompleted > 0
		&& CollisionSpheres.Num() == 1
		&& CollisionCapsules.Num() == 16
		&& PoseUpdateCount >= StepsToRun
		&& Stats.ModalCorrectionsExecuted == StepsToRun
		&& Stats.RankOneCollisionSystemsExecuted == StepsToRun
		&& Stats.NdbStateUpdatesExecuted == StepsToRun
		&& Stats.CompleteCcdPassesExecuted == StepsToRun
		&& Stats.ResidualReusePassesExecuted == StepsToRun
		&& Stats.MaximumActiveContactPairs > 0
		&& Stats.MaximumNdbActivationCount > 0
		&& FMath::IsFinite(Stats.TotalRenderSubmissionMilliseconds)
		&& Stats.TotalRenderSubmissionMilliseconds > 0.0
		&& FMath::IsFinite(Stats.MaximumForwardedResidualCm)
		&& Stats.MaximumForwardedResidualCm <= Settings.MaximumResidualCm + 1.e-3f
		&& LatestPositions.Num() == InitialPositions.Num()
		&& FMath::IsFinite(MaximumEdgeErrorCm)
		&& MaximumEdgeErrorCm < 8.0
		&& MaximumCapsulePenetrationCm < 1.e-3
		&& BottomDisplacementCm > 1.0;
	StatusText = bRunPassed ? TEXT("通过：300 固定步与最终异步快照闭环") : TEXT("失败：检查最终步、误差或接触不变量");
	FString ExportPath;
	ExperimentComponent->ExportMetrics(TEXT("M2_GpuRealtimeCloth"), EPhysicsMetricExportFormat::Json, ExportPath);
	ExperimentComponent->ExportMetrics(TEXT("M2_GpuRealtimeCloth"), EPhysicsMetricExportFormat::Csv, ExportPath);
	WriteRunReport();
	const double AverageSubmissionMilliseconds = Stats.StepsExecuted > 0 ? Stats.TotalRenderSubmissionMilliseconds / Stats.StepsExecuted : 0.0;
	UE_LOG(LogContactClothGpuRealtime, Display, TEXT("M2_GPU_REALTIME_COMPLETE passed=%s steps=%lld readbacks=%lld latest_step=%lld pose_updates=%lld modal_modes=%d modal_passes=%lld rank_one_systems=%lld ndb_passes=%lld ccd_passes=%lld residual_passes=%lld residual_fallback_events=%lld max_residual_cm=%.6f max_active_pairs=%d max_ndb_activation=%d avg_render_submission_ms=%.6f max_render_submission_ms=%.6f spheres=%d capsules=%d max_edge_error_cm=%.6f max_penetration_cm=%.9f bottom_displacement_cm=%.6f"),
		bRunPassed ? TEXT("true") : TEXT("false"), Stats.StepsExecuted, Stats.ReadbacksCompleted, LatestReadbackStep, PoseUpdateCount,
		Settings.ModalModeCount, Stats.ModalCorrectionsExecuted, Stats.RankOneCollisionSystemsExecuted, Stats.NdbStateUpdatesExecuted, Stats.CompleteCcdPassesExecuted,
		Stats.ResidualReusePassesExecuted, Stats.ResidualFallbackEvents, Stats.MaximumForwardedResidualCm,
		Stats.MaximumActiveContactPairs, Stats.MaximumNdbActivationCount, AverageSubmissionMilliseconds, Stats.MaximumRenderSubmissionMilliseconds,
		CollisionSpheres.Num(), CollisionCapsules.Num(), MaximumEdgeErrorCm, MaximumCapsulePenetrationCm, BottomDisplacementCm);
	if (bCaptureRequested || bAutoQuitRequested)
	{
		GetWorldTimerManager().SetTimer(CaptureTimerHandle, this, &ThisClass::CaptureScreenshotAndScheduleExit, 0.8f, false);
	}
}

void AContactClothGpuRealtimeActor::WriteRunReport() const
{
	const FContactClothGpuRealtimeStats Stats = GpuSimulation.GetStats();
	const double AverageSubmissionMilliseconds = Stats.StepsExecuted > 0 ? Stats.TotalRenderSubmissionMilliseconds / Stats.StepsExecuted : 0.0;
	const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M2"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, TEXT("gpu_realtime_runtime.json"));
	const FString Json = FString::Printf(
		TEXT("{\n  \"result\": \"%s\",\n  \"solver\": \"Persistent D3D12 reduced modal, rank-one NDB global contact, local Jacobi, complete CCD, and guarded residual forwarding\",\n  \"collision_source\": \"Assassin animated Physics Asset component pose\",\n  \"particles\": %d,\n  \"iterations_per_step\": %d,\n  \"steps_executed\": %lld,\n  \"readbacks_completed\": %lld,\n  \"latest_readback_step\": %lld,\n  \"pose_updates\": %lld,\n  \"modal_modes\": %d,\n  \"modal_corrections_executed\": %lld,\n  \"rank_one_collision_systems_executed\": %lld,\n  \"ndb_state_updates_executed\": %lld,\n  \"complete_ccd_passes_executed\": %lld,\n  \"residual_reuse_passes_executed\": %lld,\n  \"residual_fallback_events\": %lld,\n  \"maximum_forwarded_residual_cm\": %.9f,\n  \"latest_active_contact_pairs\": %d,\n  \"maximum_active_contact_pairs\": %d,\n  \"latest_maximum_ndb_activation_count\": %d,\n  \"maximum_ndb_activation_count\": %d,\n  \"average_render_submission_ms\": %.9f,\n  \"maximum_render_submission_ms\": %.9f,\n  \"timing_scope\": \"CPU render-thread RDG construction and submission; not GPU kernel time\",\n  \"collision_primitive_capacity\": %d,\n  \"collision_spheres\": %d,\n  \"collision_capsules\": %d,\n  \"maximum_edge_error_cm\": %.9f,\n  \"maximum_capsule_penetration_cm\": %.12f,\n  \"bottom_displacement_cm\": %.9f\n}\n"),
		bRunPassed ? TEXT("passed") : TEXT("failed"), LatestPositions.Num(), Settings.Iterations, Stats.StepsExecuted,
		Stats.ReadbacksCompleted, LatestReadbackStep, PoseUpdateCount, Settings.ModalModeCount, Stats.ModalCorrectionsExecuted,
		Stats.RankOneCollisionSystemsExecuted, Stats.NdbStateUpdatesExecuted, Stats.CompleteCcdPassesExecuted, Stats.ResidualReusePassesExecuted,
		Stats.ResidualFallbackEvents, Stats.MaximumForwardedResidualCm,
		Stats.LatestActiveContactPairs, Stats.MaximumActiveContactPairs, Stats.LatestMaximumNdbActivationCount, Stats.MaximumNdbActivationCount,
		AverageSubmissionMilliseconds, Stats.MaximumRenderSubmissionMilliseconds, Settings.CollisionPrimitiveCapacity,
		CollisionSpheres.Num(), CollisionCapsules.Num(),
		MaximumEdgeErrorCm, MaximumCapsulePenetrationCm, BottomDisplacementCm);
	FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AContactClothGpuRealtimeActor::CaptureScreenshotAndScheduleExit()
{
	if (bCaptureRequested)
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M2/Screenshots"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString Path = FPaths::Combine(Directory, TEXT("M2_GpuRealtimeCloth.png"));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogContactClothGpuRealtime, Display, TEXT("M2_GPU_REALTIME_SCREENSHOT_REQUESTED path=%s"), *Path);
	}
	if (bAutoQuitRequested)
	{
		GetWorldTimerManager().SetTimer(ExitTimerHandle, this, &ThisClass::RequestOwnedProcessExit, 2.0f, false);
	}
}

void AContactClothGpuRealtimeActor::RequestOwnedProcessExit()
{
	UE_LOG(LogContactClothGpuRealtime, Display, TEXT("M2_GPU_REALTIME_AUTO_EXIT"));
	FPlatformMisc::RequestExit(false);
}
