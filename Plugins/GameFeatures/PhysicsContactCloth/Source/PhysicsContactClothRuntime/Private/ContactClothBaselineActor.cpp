#include "ContactClothBaselineActor.h"

#include "Animation/AnimationAsset.h"
#include "Camera/CameraComponent.h"
#include "ClothingSystemRuntimeTypes.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/KismetMathLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PhysicsExperimentComponent.h"
#include "PhysicsResearchTypes.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogContactClothBaseline, Log, All);

AContactClothBaselineActor::AContactClothBaselineActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ExperimentComponent = CreateDefaultSubobject<UPhysicsExperimentComponent>(TEXT("Experiment"));
	ExperimentComponent->ExperimentCase = EPhysicsResearchCase::ContactCloth;
	ExperimentComponent->SolverBackend = EPhysicsSolverBackend::ChaosBaseline;
	ExperimentComponent->DeterministicSeed = 20260829;
	ExperimentComponent->MaxMetricSamples = 4096;
	ExperimentComponent->StepConfig.FixedDeltaSeconds = 1.0 / 60.0;
	ExperimentComponent->bAutoStart = false;

	AssassinMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("AssassinChaosCloth"));
	AssassinMesh->SetupAttachment(Root);
	AssassinMesh->SetRelativeLocation(FVector(0.0, 0.0, 2.0));
	AssassinMesh->SetRelativeRotation(FRotator(0.0, -25.0, 0.0));
	AssassinMesh->bAllowClothActors = true;
	AssassinMesh->bDisableClothSimulation = false;
	AssassinMesh->bWaitForParallelClothTask = true;
	AssassinMesh->ClothBlendWeight = 1.0f;
	AssassinMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	FloorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor"));
	FloorMesh->SetupAttachment(Root);
	FloorMesh->SetRelativeLocation(FVector(0.0, 0.0, -10.0));
	FloorMesh->SetRelativeScale3D(FVector(8.0, 8.0, 0.1));

	ContactObstacle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ContactObstacle"));
	ContactObstacle->SetupAttachment(Root);
	ContactObstacle->SetRelativeLocation(FVector(-55.0, -20.0, 105.0));
	ContactObstacle->SetRelativeScale3D(FVector(0.42, 0.42, 0.42));

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetRelativeLocation(FVector(-470.0, 390.0, 215.0));
	Camera->SetRelativeRotation(FRotator(-10.0, -39.0, 0.0));
	Camera->FieldOfView = 50.0f;
	Camera->bAutoActivate = true;

	DirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("KeyLight"));
	DirectionalLight->SetupAttachment(Root);
	DirectionalLight->SetRelativeRotation(FRotator(-38.0, -32.0, 0.0));
	DirectionalLight->SetIntensity(5.0f);

	PointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	PointLight->SetupAttachment(Root);
	PointLight->SetRelativeLocation(FVector(-180.0, 100.0, 290.0));
	PointLight->SetIntensity(2200.0f);
	PointLight->SetAttenuationRadius(900.0f);
	PointLight->SetLightColor(FLinearColor(0.25f, 0.65f, 1.0f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (CubeMesh.Succeeded())
	{
		FloorMesh->SetStaticMesh(CubeMesh.Object);
	}
	if (SphereMesh.Succeeded())
	{
		ContactObstacle->SetStaticMesh(SphereMesh.Object);
	}
}

void AContactClothBaselineActor::BeginPlay()
{
	Super::BeginPlay();
	bCaptureRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsClothGateCapture"));
	bAutoQuitRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsClothGateAutoQuit"));

	ConfigureAssassinAsset();
	ExperimentComponent->OnExperimentStep.AddDynamic(this, &ThisClass::HandleExperimentStep);
	ExperimentComponent->ResetExperiment();
	ExperimentComponent->StartExperiment();
	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		PlayerController->SetViewTarget(this);
	}
	UE_LOG(LogContactClothBaseline, Display, TEXT("M2_CLOTH_GATE_STARTED asset_loaded=%s clothing_assets=%d"), bAssetLoaded ? TEXT("true") : TEXT("false"), ClothingAssetCount);
}

void AContactClothBaselineActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	bClothSimulationCreated = AssassinMesh && AssassinMesh->GetClothingSimulation() != nullptr;
}

void AContactClothBaselineActor::ConfigureAssassinAsset()
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assassin/Mesh/SKM_Assassin_Skin1.SKM_Assassin_Skin1"));
	UAnimationAsset* IdleAnimation = LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/Assassin/Animation/Anim_Assassin_idle1.Anim_Assassin_idle1"));
	if (!Mesh)
	{
		GateStatusText = TEXT("失败：无法加载 SKM_Assassin_Skin1");
		UE_LOG(LogContactClothBaseline, Error, TEXT("M2_CLOTH_GATE_ASSET_LOAD_FAILED"));
		return;
	}

	AssassinMesh->SetSkeletalMesh(Mesh);
	ClothingAssetCount = Mesh->GetMeshClothingAssets().Num();
	bAssetLoaded = true;
	if (IdleAnimation)
	{
		AssassinMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		AssassinMesh->PlayAnimation(IdleAnimation, true);
	}
	AssassinMesh->ForceClothNextUpdateTeleportAndReset();
	GateStatusText = ClothingAssetCount > 0 ? TEXT("资产已加载，等待 Chaos Cloth 回写") : TEXT("失败：Skeletal Mesh 未绑定 Clothing Asset");
}

void AContactClothBaselineActor::HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex)
{
	const double SimulatedSeconds = static_cast<double>(StepIndex) * FixedDeltaSeconds;
	const float Yaw = -25.0f + 14.0f * FMath::Sin(static_cast<float>(SimulatedSeconds * 2.1));
	const float Roll = 3.0f * FMath::Sin(static_cast<float>(SimulatedSeconds * 3.4));
	AssassinMesh->SetRelativeRotation(FRotator(0.0f, Yaw, Roll));
	AssassinMesh->SetRelativeLocation(FVector(0.0f, 10.0f * FMath::Sin(static_cast<float>(SimulatedSeconds * 1.6)), 2.0f));

	if (StepIndex >= WarmupSteps)
	{
		GatherClothEvidence(StepIndex);
	}
	if (StepIndex >= StepsToRun && !bRunComplete)
	{
		CompleteAssetGate();
	}
}

void AContactClothBaselineActor::GatherClothEvidence(int64 StepIndex)
{
	AssassinMesh->WaitForExistingParallelClothSimulation_GameThread();
	const TMap<int32, FClothSimulData>& ClothData = AssassinMesh->GetCurrentClothingData_GameThread();
	int32 FrameParticleCount = 0;
	double FrameMeanDisplacement = 0.0;
	double FrameMaximumDisplacement = 0.0;
	int32 ComparedParticleCount = 0;

	for (const TPair<int32, FClothSimulData>& Entry : ClothData)
	{
		const TArray<FVector3f, TAlignedHeapAllocator<16>>& Positions = Entry.Value.Positions;
		FrameParticleCount += Positions.Num();
		TArray<FVector3f, TAlignedHeapAllocator<16>>* Reference = ReferencePositions.Find(Entry.Key);
		if (!Reference)
		{
			ReferencePositions.Add(Entry.Key, Positions);
			continue;
		}
		const int32 Count = FMath::Min(Reference->Num(), Positions.Num());
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Displacement = FVector3d(Positions[Index] - (*Reference)[Index]).Length();
			FrameMeanDisplacement += Displacement;
			FrameMaximumDisplacement = FMath::Max(FrameMaximumDisplacement, Displacement);
			++ComparedParticleCount;
		}
	}

	SimulatedParticleCount = FMath::Max(SimulatedParticleCount, FrameParticleCount);
	bHasRuntimeClothData |= FrameParticleCount > 0;
	if (ComparedParticleCount > 0)
	{
		FrameMeanDisplacement /= static_cast<double>(ComparedParticleCount);
	}
	MaximumObservedDisplacementCm = FMath::Max(MaximumObservedDisplacementCm, FrameMaximumDisplacement);

	ExperimentComponent->RecordMetric(TEXT("ClothParticleCount"), FrameParticleCount, TEXT("particles"));
	ExperimentComponent->RecordMetric(TEXT("MeanClothDisplacement"), FrameMeanDisplacement, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("MaxClothDisplacement"), FrameMaximumDisplacement, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("ActiveClothActors"), ClothData.Num(), TEXT("actors"));
}

void AContactClothBaselineActor::CompleteAssetGate()
{
	bRunComplete = true;
	ExperimentComponent->PauseExperiment();
	ExperimentComponent->ExportMetrics(TEXT("M2_ChaosClothAssetGate"), EPhysicsMetricExportFormat::Json, LastJsonExportPath);
	ExperimentComponent->ExportMetrics(TEXT("M2_ChaosClothAssetGate"), EPhysicsMetricExportFormat::Csv, LastCsvExportPath);

	const bool bPassed = bAssetLoaded && ClothingAssetCount > 0 && bClothSimulationCreated && bHasRuntimeClothData && SimulatedParticleCount > 0 && MaximumObservedDisplacementCm > 0.01;
	GateStatusText = bPassed ? TEXT("通过：Assassin Chaos Cloth 可直接运行") : TEXT("未通过：改用程序化布片保底路径");
	WriteAssetGateReport(bPassed);
	UE_LOG(LogContactClothBaseline, Display, TEXT("M2_CLOTH_GATE_COMPLETE passed=%s simulation=%s particles=%d max_displacement_cm=%.6f"), bPassed ? TEXT("true") : TEXT("false"), bClothSimulationCreated ? TEXT("true") : TEXT("false"), SimulatedParticleCount, MaximumObservedDisplacementCm);

	if (bCaptureRequested || bAutoQuitRequested)
	{
		GetWorldTimerManager().SetTimer(CaptureTimerHandle, this, &ThisClass::CaptureScreenshotAndScheduleExit, 0.6f, false);
	}
}

void AContactClothBaselineActor::WriteAssetGateReport(bool bPassed) const
{
	const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M2"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString ReportPath = FPaths::Combine(Directory, TEXT("chaos_cloth_asset_gate.json"));
	const FString Json = FString::Printf(
		TEXT("{\n  \"result\": \"%s\",\n  \"asset\": \"/Game/Assassin/Mesh/SKM_Assassin_Skin1\",\n  \"asset_loaded\": %s,\n  \"clothing_assets\": %d,\n  \"cloth_simulation_created\": %s,\n  \"runtime_cloth_data\": %s,\n  \"simulated_particles\": %d,\n  \"maximum_observed_displacement_cm\": %.9f,\n  \"fallback_required\": %s\n}\n"),
		bPassed ? TEXT("passed") : TEXT("failed"),
		bAssetLoaded ? TEXT("true") : TEXT("false"),
		ClothingAssetCount,
		bClothSimulationCreated ? TEXT("true") : TEXT("false"),
		bHasRuntimeClothData ? TEXT("true") : TEXT("false"),
		SimulatedParticleCount,
		MaximumObservedDisplacementCm,
		bPassed ? TEXT("false") : TEXT("true"));
	FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AContactClothBaselineActor::CaptureScreenshotAndScheduleExit()
{
	if (bCaptureRequested)
	{
		const FString ScreenshotDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M2/Screenshots"));
		IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);
		const FString ScreenshotPath = FPaths::Combine(ScreenshotDirectory, TEXT("M2_ChaosClothAssetGate.png"));
		FScreenshotRequest::RequestScreenshot(ScreenshotPath, false, false);
		UE_LOG(LogContactClothBaseline, Display, TEXT("M2_CLOTH_GATE_SCREENSHOT_REQUESTED path=%s"), *ScreenshotPath);
	}
	if (bAutoQuitRequested)
	{
		GetWorldTimerManager().SetTimer(ExitTimerHandle, this, &ThisClass::RequestOwnedProcessExit, 2.0f, false);
	}
}

void AContactClothBaselineActor::RequestOwnedProcessExit()
{
	UE_LOG(LogContactClothBaseline, Display, TEXT("M2_CLOTH_GATE_AUTO_EXIT"));
	FPlatformMisc::RequestExit(false);
}
