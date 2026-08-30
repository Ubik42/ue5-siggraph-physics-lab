#include "ContactClothGpuValidationActor.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "DrawDebugHelpers.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogContactClothGpuValidation, Log, All);

AContactClothGpuValidationActor::AContactClothGpuValidationActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ExperimentComponent = CreateDefaultSubobject<UPhysicsExperimentComponent>(TEXT("Experiment"));
	ExperimentComponent->ExperimentCase = EPhysicsResearchCase::ContactCloth;
	ExperimentComponent->SolverBackend = EPhysicsSolverBackend::ResearchSolver;
	ExperimentComponent->DeterministicSeed = 20260830;
	ExperimentComponent->MaxMetricSamples = 32;
	ExperimentComponent->bAutoStart = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetRelativeLocation(FVector(0.0, -980.0, 285.0));
	Camera->SetRelativeRotation(FRotator(-2.0, 90.0, 0.0));
	Camera->FieldOfView = 52.0f;
	Camera->bAutoActivate = true;

	DirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("KeyLight"));
	DirectionalLight->SetupAttachment(Root);
	DirectionalLight->SetRelativeRotation(FRotator(-42.0, -25.0, 0.0));
	DirectionalLight->SetIntensity(4.0f);

	PointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	PointLight->SetupAttachment(Root);
	PointLight->SetRelativeLocation(FVector(0.0, -250.0, 350.0));
	PointLight->SetIntensity(2200.0f);
	PointLight->SetAttenuationRadius(1200.0f);
	PointLight->SetLightColor(FLinearColor(0.08f, 0.55f, 1.0f));
}

void AContactClothGpuValidationActor::BeginPlay()
{
	Super::BeginPlay();
	bCaptureRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsClothGpuCapture"));
	bAutoQuitRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsClothGpuAutoQuit"));

	Settings.Columns = 16;
	Settings.Rows = 12;
	Settings.SpacingCm = 12.0f;
	Settings.Iterations = 96;
	Settings.Relaxation = 1.0f;
	Settings.SphereRadiusCm = 0.0f;
	BuildInputGrid();

	ExperimentComponent->ResetExperiment();
	ExperimentComponent->StepExperimentOnce();
	GpuResult = FContactClothGpuJacobi::RunBlocking(InputPositions, Settings);
	ExperimentComponent->RecordMetric(TEXT("InputMaximumEdgeError"), GpuResult.InputMaximumEdgeErrorCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("OutputMaximumEdgeError"), GpuResult.OutputMaximumEdgeErrorCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("ErrorReductionRatio"), GpuResult.OutputMaximumEdgeErrorCm / FMath::Max(GpuResult.InputMaximumEdgeErrorCm, UE_DOUBLE_SMALL_NUMBER), TEXT("ratio"));
	ExperimentComponent->RecordMetric(TEXT("BlockingWallTime"), GpuResult.BlockingWallMilliseconds, TEXT("ms"));

	bValidationPassed = GpuResult.bDispatched
		&& GpuResult.bReadbackSucceeded
		&& GpuResult.Positions.Num() == InputPositions.Num()
		&& GpuResult.OutputMaximumEdgeErrorCm < GpuResult.InputMaximumEdgeErrorCm * 0.8
		&& FMath::IsFinite(GpuResult.OutputMaximumEdgeErrorCm);
	StatusText = bValidationPassed ? TEXT("通过：真实 D3D GPU 派发与误差回读闭环") : TEXT("失败：检查 GPU 派发、回读或误差预算");

	FString ExportPath;
	ExperimentComponent->ExportMetrics(TEXT("M2_GpuJacobiValidation"), EPhysicsMetricExportFormat::Json, ExportPath);
	ExperimentComponent->ExportMetrics(TEXT("M2_GpuJacobiValidation"), EPhysicsMetricExportFormat::Csv, ExportPath);
	WriteRunReport();

	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		PlayerController->SetViewTarget(this);
	}
	UE_LOG(LogContactClothGpuValidation, Display, TEXT("M2_GPU_VISUAL_COMPLETE passed=%s particles=%d iterations=%d input_error_cm=%.6f output_error_cm=%.6f wall_ms=%.3f"),
		bValidationPassed ? TEXT("true") : TEXT("false"), InputPositions.Num(), Settings.Iterations, GpuResult.InputMaximumEdgeErrorCm, GpuResult.OutputMaximumEdgeErrorCm, GpuResult.BlockingWallMilliseconds);

	if (bCaptureRequested || bAutoQuitRequested)
	{
		GetWorldTimerManager().SetTimer(CaptureTimerHandle, this, &ThisClass::CaptureScreenshotAndScheduleExit, 1.0f, false);
	}
}

void AContactClothGpuValidationActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	DrawValidationDebug();
}

void AContactClothGpuValidationActor::BuildInputGrid()
{
	InputPositions.Reset(Settings.Columns * Settings.Rows);
	const float HalfWidth = (Settings.Columns - 1) * Settings.SpacingCm * 0.5f;
	for (int32 Row = 0; Row < Settings.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Settings.Columns; ++Column)
		{
			InputPositions.Emplace(Column * Settings.SpacingCm - HalfWidth, 0.0f, 240.0f - Row * Settings.SpacingCm);
		}
	}
	const int32 PerturbedIndex = (Settings.Rows / 2) * Settings.Columns + Settings.Columns / 2;
	InputPositions[PerturbedIndex].X += 60.0f;
	InputPositions[PerturbedIndex].Y += 25.0f;
}

void AContactClothGpuValidationActor::DrawGrid(TConstArrayView<FVector3f> Positions, const FVector& DisplayOffset, const FColor& LineColor) const
{
	if (!GetWorld() || Positions.Num() != Settings.Columns * Settings.Rows)
	{
		return;
	}
	for (int32 Row = 0; Row < Settings.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Settings.Columns; ++Column)
		{
			const int32 Index = Row * Settings.Columns + Column;
			const FVector Position = FVector(Positions[Index]) + DisplayOffset;
			if (Column + 1 < Settings.Columns)
			{
				DrawDebugLine(GetWorld(), Position, FVector(Positions[Index + 1]) + DisplayOffset, LineColor, false, 0.0f, 0, 1.25f);
			}
			if (Row + 1 < Settings.Rows)
			{
				DrawDebugLine(GetWorld(), Position, FVector(Positions[Index + Settings.Columns]) + DisplayOffset, LineColor, false, 0.0f, 0, 1.25f);
			}
			DrawDebugPoint(GetWorld(), Position, Row == 0 ? 8.0f : 4.0f, Row == 0 ? FColor::Red : FColor::White, false, 0.0f, 0);
		}
	}
}

void AContactClothGpuValidationActor::DrawValidationDebug() const
{
	DrawGrid(InputPositions, FVector(220.0, 0.0, 0.0), FColor(255, 130, 35));
	DrawGrid(GpuResult.Positions, FVector(-220.0, 0.0, 0.0), FColor(30, 220, 255));
	if (GetWorld())
	{
		DrawDebugString(GetWorld(), FVector(220.0, 0.0, 275.0), TEXT("GPU 输入：中心扰动"), nullptr, FColor(255, 175, 70), 0.0f, true, 1.15f);
		DrawDebugString(GetWorld(), FVector(-220.0, 0.0, 275.0), TEXT("96 次 Jacobi 输出"), nullptr, FColor(60, 235, 255), 0.0f, true, 1.15f);
	}
}

void AContactClothGpuValidationActor::WriteRunReport() const
{
	const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M2"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Path = FPaths::Combine(Directory, TEXT("gpu_jacobi_runtime.json"));
	const FString Json = FString::Printf(
		TEXT("{\n  \"result\": \"%s\",\n  \"solver\": \"D3D12 Structured Buffer aggregated Jacobi validation\",\n  \"particles\": %d,\n  \"iterations\": %d,\n  \"dispatched\": %s,\n  \"readback_succeeded\": %s,\n  \"input_maximum_edge_error_cm\": %.9f,\n  \"output_maximum_edge_error_cm\": %.9f,\n  \"blocking_wall_milliseconds\": %.6f\n}\n"),
		bValidationPassed ? TEXT("passed") : TEXT("failed"), InputPositions.Num(), Settings.Iterations,
		GpuResult.bDispatched ? TEXT("true") : TEXT("false"), GpuResult.bReadbackSucceeded ? TEXT("true") : TEXT("false"),
		GpuResult.InputMaximumEdgeErrorCm, GpuResult.OutputMaximumEdgeErrorCm, GpuResult.BlockingWallMilliseconds);
	FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AContactClothGpuValidationActor::CaptureScreenshotAndScheduleExit()
{
	if (bCaptureRequested)
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M2/Screenshots"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString Path = FPaths::Combine(Directory, TEXT("M2_GpuJacobiValidation.png"));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogContactClothGpuValidation, Display, TEXT("M2_GPU_VISUAL_SCREENSHOT_REQUESTED path=%s"), *Path);
	}
	if (bAutoQuitRequested)
	{
		GetWorldTimerManager().SetTimer(ExitTimerHandle, this, &ThisClass::RequestOwnedProcessExit, 2.0f, false);
	}
}

void AContactClothGpuValidationActor::RequestOwnedProcessExit()
{
	UE_LOG(LogContactClothGpuValidation, Display, TEXT("M2_GPU_VISUAL_AUTO_EXIT"));
	FPlatformMisc::RequestExit(false);
}
