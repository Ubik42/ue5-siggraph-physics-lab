#include "ContactClothCpuReferenceActor.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogContactClothCpuReference, Log, All);

AContactClothCpuReferenceActor::AContactClothCpuReferenceActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ExperimentComponent = CreateDefaultSubobject<UPhysicsExperimentComponent>(TEXT("Experiment"));
	ExperimentComponent->ExperimentCase = EPhysicsResearchCase::ContactCloth;
	ExperimentComponent->SolverBackend = EPhysicsSolverBackend::ResearchSolver;
	ExperimentComponent->DeterministicSeed = 20260829;
	ExperimentComponent->MaxMetricSamples = 4096;
	ExperimentComponent->StepConfig.FixedDeltaSeconds = 1.0 / 60.0;
	ExperimentComponent->bAutoStart = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	FloorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor"));
	FloorMesh->SetupAttachment(Root);
	FloorMesh->SetStaticMesh(CubeMesh.Object);
	FloorMesh->SetRelativeLocation(FVector(0.0, 0.0, -8.0));
	FloorMesh->SetRelativeScale3D(FVector(7.0, 7.0, 0.08));

	SphereColliderMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AnalyticSphereCollider"));
	SphereColliderMesh->SetupAttachment(Root);
	SphereColliderMesh->SetStaticMesh(SphereMesh.Object);
	SphereColliderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetRelativeLocation(FVector(600.0, -850.0, 360.0));
	Camera->SetRelativeRotation(FRotator(-5.3, 129.5, 0.0));
	Camera->FieldOfView = 50.0f;
	Camera->bAutoActivate = true;

	DirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("KeyLight"));
	DirectionalLight->SetupAttachment(Root);
	DirectionalLight->SetRelativeRotation(FRotator(-42.0, -25.0, 0.0));
	DirectionalLight->SetIntensity(5.5f);

	PointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	PointLight->SetupAttachment(Root);
	PointLight->SetRelativeLocation(FVector(-150.0, -220.0, 390.0));
	PointLight->SetIntensity(3000.0f);
	PointLight->SetAttenuationRadius(1100.0f);
	PointLight->SetLightColor(FLinearColor(0.1f, 0.65f, 1.0f));
}

void AContactClothCpuReferenceActor::BeginPlay()
{
	Super::BeginPlay();
	bCaptureRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsClothCpuCapture"));
	bAutoQuitRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsClothCpuAutoQuit"));

	FContactClothCpuSettings Settings;
	Settings.Columns = 12;
	Settings.Rows = 10;
	Settings.SpacingCm = 18.0;
	Settings.LocalGlobalIterations = 10;
	Settings.GlobalGaussSeidelSweeps = 18;
	Solver.InitializeGrid(Settings, FVector3d(0.0, 0.0, 410.0));
	SphereCollider.Center = FVector3d(0.0, 0.0, 160.0);
	SphereCollider.RadiusCm = 92.0;
	SphereColliderMesh->SetRelativeLocation(FVector(SphereCollider.Center));
	SphereColliderMesh->SetRelativeScale3D(FVector((SphereCollider.RadiusCm * 2.0) / 100.0));

	ExperimentComponent->OnExperimentStep.AddDynamic(this, &ThisClass::HandleExperimentStep);
	ExperimentComponent->ResetExperiment();
	ExperimentComponent->StartExperiment();
	StatusText = TEXT("运行：CPU local-global PD + 球接触");
	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		PlayerController->SetViewTarget(this);
	}
	UE_LOG(LogContactClothCpuReference, Display, TEXT("M2_CPU_REFERENCE_STARTED particles=%d constraints=%d"), Solver.GetParticles().Num(), Solver.GetConstraints().Num());
}

void AContactClothCpuReferenceActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	DrawSolverDebug();
}

void AContactClothCpuReferenceActor::HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex)
{
	LastStepStats = Solver.Step(FixedDeltaSeconds, MakeArrayView(&SphereCollider, 1));
	TotalCollisionProjections += LastStepStats.CollisionProjectionCount;
	MaximumObservedStretchError = FMath::Max(MaximumObservedStretchError, LastStepStats.MaximumStretchRatioError);
	MaximumObservedPenetrationCm = FMath::Max(MaximumObservedPenetrationCm, LastStepStats.MaximumRemainingPenetrationCm);
	ExperimentComponent->RecordMetric(TEXT("MaxStretchRatioError"), LastStepStats.MaximumStretchRatioError, TEXT("ratio"));
	ExperimentComponent->RecordMetric(TEXT("MeanStretchRatioError"), LastStepStats.MeanStretchRatioError, TEXT("ratio"));
	ExperimentComponent->RecordMetric(TEXT("CollisionProjections"), LastStepStats.CollisionProjectionCount, TEXT("count"));
	ExperimentComponent->RecordMetric(TEXT("RemainingPenetration"), LastStepStats.MaximumRemainingPenetrationCm, TEXT("cm"));

	if (!bRunComplete && StepIndex >= StepsToRun)
	{
		CompleteRun();
	}
}

void AContactClothCpuReferenceActor::DrawSolverDebug() const
{
	if (!GetWorld())
	{
		return;
	}
	const TArray<FContactClothCpuParticle>& Particles = Solver.GetParticles();
	for (const FContactClothCpuConstraint& Constraint : Solver.GetConstraints())
	{
		const FColor Color = Constraint.Weight >= 50000.0 ? FColor(40, 220, 255) : Constraint.Weight >= 10000.0 ? FColor(255, 175, 45) : FColor(150, 90, 255);
		DrawDebugLine(GetWorld(), FVector(Particles[Constraint.ParticleA].Position), FVector(Particles[Constraint.ParticleB].Position), Color, false, 0.0f, 0, 0.75f);
	}
	for (const FContactClothCpuParticle& Particle : Particles)
	{
		DrawDebugPoint(GetWorld(), FVector(Particle.Position), Particle.IsPinned() ? 10.0f : 4.0f, Particle.IsPinned() ? FColor::Red : FColor::White, false, 0.0f, 0);
	}
}

void AContactClothCpuReferenceActor::CompleteRun()
{
	bRunComplete = true;
	ExperimentComponent->PauseExperiment();
	ExperimentComponent->ExportMetrics(TEXT("M2_CpuProjectiveDynamics"), EPhysicsMetricExportFormat::Json, LastJsonExportPath);
	ExperimentComponent->ExportMetrics(TEXT("M2_CpuProjectiveDynamics"), EPhysicsMetricExportFormat::Csv, LastCsvExportPath);
	const bool bPassed = Solver.GetParticles().Num() == 120
		&& Solver.GetConstraints().Num() > Solver.GetParticles().Num()
		&& TotalCollisionProjections > 0
		&& FMath::IsFinite(MaximumObservedStretchError)
		&& MaximumObservedPenetrationCm < 1.e-5;
	StatusText = bPassed ? TEXT("通过：CPU 数学参考稳定完成") : TEXT("失败：检查误差或接触不变量");
	WriteRunReport(bPassed);
	UE_LOG(LogContactClothCpuReference, Display, TEXT("M2_CPU_REFERENCE_COMPLETE passed=%s collisions=%lld max_stretch=%.9f max_penetration_cm=%.9f"), bPassed ? TEXT("true") : TEXT("false"), TotalCollisionProjections, MaximumObservedStretchError, MaximumObservedPenetrationCm);
	if (bCaptureRequested || bAutoQuitRequested)
	{
		GetWorldTimerManager().SetTimer(CaptureTimerHandle, this, &ThisClass::CaptureScreenshotAndScheduleExit, 0.6f, false);
	}
}

void AContactClothCpuReferenceActor::WriteRunReport(bool bPassed) const
{
	const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M2"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, TEXT("cpu_reference_runtime.json"));
	const FString Json = FString::Printf(
		TEXT("{\n  \"result\": \"%s\",\n  \"solver\": \"CPU local-global Projective Dynamics reference\",\n  \"particles\": %d,\n  \"constraints\": %d,\n  \"steps\": %lld,\n  \"total_collision_projections\": %lld,\n  \"maximum_observed_stretch_ratio_error\": %.12f,\n  \"maximum_remaining_penetration_cm\": %.12f\n}\n"),
		bPassed ? TEXT("passed") : TEXT("failed"), Solver.GetParticles().Num(), Solver.GetConstraints().Num(), StepsToRun, TotalCollisionProjections, MaximumObservedStretchError, MaximumObservedPenetrationCm);
	FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AContactClothCpuReferenceActor::CaptureScreenshotAndScheduleExit()
{
	if (bCaptureRequested)
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M2/Screenshots"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString Path = FPaths::Combine(Directory, TEXT("M2_CpuProjectiveDynamics.png"));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogContactClothCpuReference, Display, TEXT("M2_CPU_REFERENCE_SCREENSHOT_REQUESTED path=%s"), *Path);
	}
	if (bAutoQuitRequested)
	{
		GetWorldTimerManager().SetTimer(ExitTimerHandle, this, &ThisClass::RequestOwnedProcessExit, 2.0f, false);
	}
}

void AContactClothCpuReferenceActor::RequestOwnedProcessExit()
{
	UE_LOG(LogContactClothCpuReference, Display, TEXT("M2_CPU_REFERENCE_AUTO_EXIT"));
	FPlatformMisc::RequestExit(false);
}
