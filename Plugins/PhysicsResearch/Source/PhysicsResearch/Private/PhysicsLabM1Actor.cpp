#include "PhysicsLabM1Actor.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PhysicsExperimentComponent.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogPhysicsLabM1, Log, All);

namespace PhysicsLabM1
{
void ApplyColor(UStaticMeshComponent* Mesh, const FLinearColor& Color)
{
	if (!Mesh || Mesh->GetNumMaterials() == 0)
	{
		return;
	}
	if (UMaterialInstanceDynamic* Material = Mesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
	}
}
}

APhysicsLabM1Actor::APhysicsLabM1Actor()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ExperimentComponent = CreateDefaultSubobject<UPhysicsExperimentComponent>(TEXT("Experiment"));
	ExperimentComponent->bAutoStart = false;
	ExperimentComponent->ExperimentCase = EPhysicsResearchCase::ContactCloth;
	ExperimentComponent->SolverBackend = EPhysicsSolverBackend::ChaosBaseline;
	ExperimentComponent->DeterministicSeed = 20260829;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));

	FloorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor"));
	FloorMesh->SetupAttachment(SceneRoot);
	FloorMesh->SetStaticMesh(PlaneFinder.Object);
	FloorMesh->SetRelativeScale3D(FVector(12.0, 12.0, 1.0));
	FloorMesh->SetRelativeLocation(FVector(0.0, 0.0, -10.0));

	ProbeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DeterministicProbe"));
	ProbeMesh->SetupAttachment(SceneRoot);
	ProbeMesh->SetStaticMesh(SphereFinder.Object);
	ProbeMesh->SetRelativeScale3D(FVector(0.7));
	ProbeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ObstacleMeshA = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ObstacleA"));
	ObstacleMeshA->SetupAttachment(SceneRoot);
	ObstacleMeshA->SetStaticMesh(CubeFinder.Object);
	ObstacleMeshA->SetRelativeLocation(FVector(-180.0, 0.0, 75.0));
	ObstacleMeshA->SetRelativeScale3D(FVector(0.8, 0.8, 1.5));

	ObstacleMeshB = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ObstacleB"));
	ObstacleMeshB->SetupAttachment(SceneRoot);
	ObstacleMeshB->SetStaticMesh(CubeFinder.Object);
	ObstacleMeshB->SetRelativeLocation(FVector(180.0, 0.0, 50.0));
	ObstacleMeshB->SetRelativeScale3D(FVector(1.0));

	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("LabCamera"));
	CameraComponent->SetupAttachment(SceneRoot);
	CameraComponent->SetRelativeLocation(FVector(900.0, -900.0, 650.0));
	CameraComponent->SetRelativeRotation(FRotator(-25.0, 135.0, 0.0));
	CameraComponent->FieldOfView = 58.0f;

	DirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("KeyLight"));
	DirectionalLight->SetupAttachment(SceneRoot);
	DirectionalLight->SetRelativeRotation(FRotator(-48.0, -32.0, 0.0));
	DirectionalLight->SetIntensity(6.0f);

	AccentLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("AccentLight"));
	AccentLight->SetupAttachment(SceneRoot);
	AccentLight->SetRelativeLocation(FVector(0.0, -160.0, 420.0));
	AccentLight->SetIntensity(4200.0f);
	AccentLight->SetAttenuationRadius(1300.0f);
	AccentLight->SetLightColor(FLinearColor(0.08f, 0.65f, 1.0f));
}

void APhysicsLabM1Actor::BeginPlay()
{
	Super::BeginPlay();

	PhysicsLabM1::ApplyColor(FloorMesh, FLinearColor(0.025f, 0.035f, 0.055f));
	PhysicsLabM1::ApplyColor(ProbeMesh, FLinearColor(0.0f, 0.9f, 1.0f));
	PhysicsLabM1::ApplyColor(ObstacleMeshA, FLinearColor(0.2f, 0.3f, 0.55f));
	PhysicsLabM1::ApplyColor(ObstacleMeshB, FLinearColor(0.75f, 0.22f, 0.16f));

	ExperimentComponent->OnExperimentStep.AddDynamic(this, &ThisClass::HandleExperimentStep);
	ExperimentComponent->ResetExperiment();
	ExperimentComponent->StartExperiment();

	bCaptureRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsLabCapture"));
	bAutoQuitRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsLabAutoQuit"));
	bPortfolioHubMode = FParse::Param(FCommandLine::Get(), TEXT("PhysicsLabPortfolioHub"));
#if !WITH_EDITOR
	bPortfolioHubMode = true;
#endif
	int32 AutoCase = 0;
	if (bPortfolioHubMode && FParse::Value(FCommandLine::Get(), TEXT("PhysicsLabHubAutoCase="), AutoCase) && AutoCase >= 1 && AutoCase <= 5)
	{
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, AutoCase]() { LaunchPortfolioCase(AutoCase); }));
	}

	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		PlayerController->SetViewTarget(this);
		PlayerController->bShowMouseCursor = true;
	}

	UE_LOG(LogPhysicsLabM1, Display, TEXT("M1_RUNTIME_STARTED seed=%d steps=%d"), ExperimentComponent->DeterministicSeed, StepsToRun);
}

void APhysicsLabM1Actor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	HandleInput();
	DrawExperimentDebug();
}

void APhysicsLabM1Actor::HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex)
{
	const double SimulatedSeconds = ExperimentComponent->GetExperimentStats().SimulatedSeconds;
	const double Phase = SimulatedSeconds * 1.65;
	const FVector ProbeLocation(
		250.0 * FMath::Cos(Phase),
		210.0 * FMath::Sin(Phase),
		145.0 + 65.0 * FMath::Sin(Phase * 2.0));
	ProbeMesh->SetRelativeLocation(ProbeLocation);

	const double RadiusError = FMath::Abs(FVector2D(ProbeLocation.X, ProbeLocation.Y).Size() - 230.0);
	const double EnergyProxy = ProbeLocation.SizeSquared() / 10000.0;
	ExperimentComponent->RecordMetric(TEXT("OrbitRadiusError"), RadiusError, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("EnergyProxy"), EnergyProxy, TEXT("cm2"));

	if (!bRunComplete && StepIndex >= StepsToRun)
	{
		CompleteRun();
	}
}

void APhysicsLabM1Actor::HandleInput()
{
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (!PlayerController)
	{
		return;
	}
	if (bPortfolioHubMode)
	{
		if (PlayerController->WasInputKeyJustPressed(EKeys::One)) LaunchPortfolioCase(1);
		else if (PlayerController->WasInputKeyJustPressed(EKeys::Two)) LaunchPortfolioCase(2);
		else if (PlayerController->WasInputKeyJustPressed(EKeys::Three)) LaunchPortfolioCase(3);
		else if (PlayerController->WasInputKeyJustPressed(EKeys::Four)) LaunchPortfolioCase(4);
		else if (PlayerController->WasInputKeyJustPressed(EKeys::Five)) LaunchPortfolioCase(5);
		else if (PlayerController->WasInputKeyJustPressed(EKeys::H))
		{
			HubStatus = TEXT("打开 Case05 · ALS 物理—动画交接");
			UGameplayStatics::OpenLevel(this, FName(TEXT("/PhysicsDrivenCharacter/Maps/L_PhysicalGetUpHandoff")));
		}
		return;
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		if (ExperimentComponent->IsExperimentRunning())
		{
			ExperimentComponent->PauseExperiment();
		}
		else
		{
			ExperimentComponent->StartExperiment();
		}
	}
	if (PlayerController->WasInputKeyJustPressed(EKeys::N))
	{
		ExperimentComponent->StepExperimentOnce();
	}
	if (PlayerController->WasInputKeyJustPressed(EKeys::R))
	{
		bRunComplete = false;
		ExperimentComponent->ResetExperiment();
		ExperimentComponent->StartExperiment();
	}
	if (PlayerController->WasInputKeyJustPressed(EKeys::E))
	{
		ExperimentComponent->ExportMetrics(TEXT("M1_Manual"), EPhysicsMetricExportFormat::Json, LastJsonExportPath);
		ExperimentComponent->ExportMetrics(TEXT("M1_Manual"), EPhysicsMetricExportFormat::Csv, LastCsvExportPath);
	}
	if (PlayerController->WasInputKeyJustPressed(EKeys::D))
	{
		bEnableDebugDraw = !bEnableDebugDraw;
	}
}

void APhysicsLabM1Actor::LaunchPortfolioCase(const int32 CaseIndex)
{
	static const TCHAR* Maps[] = {
		TEXT(""),
		TEXT("/PhysicsContactCloth/Maps/L_GpuExplicitAssassin"),
		TEXT("/PhysicsSoftBody/Maps/L_DragonGpuSurface"),
		TEXT("/PhysicsFlowMap/Maps/L_FlowMapChaosObstacle"),
		TEXT("/PhysicsInverseDesign/Maps/L_InverseSpringChaosReplay"),
		TEXT("/PhysicsDrivenCharacter/Maps/L_PhysicsControlCharacter")
	};
	if (CaseIndex < 1 || CaseIndex > 5) return;
	HubStatus = FString::Printf(TEXT("正在打开 Case%02d"), CaseIndex);
	UE_LOG(LogPhysicsLabM1, Display, TEXT("M7_HUB_OPEN case=%d map=%s"), CaseIndex, Maps[CaseIndex]);
	UGameplayStatics::OpenLevel(this, FName(Maps[CaseIndex]));
}

void APhysicsLabM1Actor::DrawExperimentDebug() const
{
	if (bPortfolioHubMode || !bEnableDebugDraw || !GetWorld())
	{
		return;
	}

	const FVector Origin = GetActorLocation() + FVector(0.0, 0.0, 145.0);
	DrawDebugCircle(GetWorld(), Origin, 230.0f, 64, FColor::Cyan, false, 0.0f, 0, 2.5f, FVector::ForwardVector, FVector::RightVector, false);
	DrawDebugLine(GetWorld(), Origin, ProbeMesh->GetComponentLocation(), FColor::White, false, 0.0f, 0, 1.5f);
	DrawDebugCoordinateSystem(GetWorld(), GetActorLocation(), FRotator::ZeroRotator, 120.0f, false, 0.0f, 0, 1.5f);
}

void APhysicsLabM1Actor::CompleteRun()
{
	bRunComplete = true;
	ExperimentComponent->PauseExperiment();
	const bool bJsonExported = ExperimentComponent->ExportMetrics(TEXT("M1_Runtime"), EPhysicsMetricExportFormat::Json, LastJsonExportPath);
	const bool bCsvExported = ExperimentComponent->ExportMetrics(TEXT("M1_Runtime"), EPhysicsMetricExportFormat::Csv, LastCsvExportPath);

	UE_LOG(
		LogPhysicsLabM1,
		Display,
		TEXT("M1_RUNTIME_COMPLETE steps=%lld samples=%d json=%s csv=%s"),
		ExperimentComponent->GetExperimentStats().StepIndex,
		ExperimentComponent->GetExperimentStats().RecordedMetricSamples,
		bJsonExported ? *LastJsonExportPath : TEXT("FAILED"),
		bCsvExported ? *LastCsvExportPath : TEXT("FAILED"));

	if (bCaptureRequested || bAutoQuitRequested)
	{
		GetWorldTimerManager().SetTimer(CaptureTimerHandle, this, &ThisClass::CaptureScreenshotAndScheduleExit, 0.5f, false);
	}
}

void APhysicsLabM1Actor::CaptureScreenshotAndScheduleExit()
{
	if (bCaptureRequested)
	{
		const FString ScreenshotDirectory = FPaths::Combine(FPaths::ProjectDir(), bPortfolioHubMode ? TEXT("Docs/Validation/M7/Screenshots") : TEXT("Docs/Validation/M1/Screenshots"));
		IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);
		const FString ScreenshotPath = FPaths::Combine(ScreenshotDirectory, bPortfolioHubMode ? TEXT("M7_PortfolioHub.png") : TEXT("M1_Runtime.png"));
		FScreenshotRequest::RequestScreenshot(ScreenshotPath, false, false);
		UE_LOG(LogPhysicsLabM1, Display, TEXT("M1_SCREENSHOT_REQUESTED path=%s"), *ScreenshotPath);
	}

	if (bAutoQuitRequested)
	{
		GetWorldTimerManager().SetTimer(ExitTimerHandle, this, &ThisClass::RequestOwnedProcessExit, 2.0f, false);
	}
}

void APhysicsLabM1Actor::RequestOwnedProcessExit()
{
	UE_LOG(LogPhysicsLabM1, Display, TEXT("M1_AUTO_EXIT"));
	FPlatformMisc::RequestExit(false);
}
