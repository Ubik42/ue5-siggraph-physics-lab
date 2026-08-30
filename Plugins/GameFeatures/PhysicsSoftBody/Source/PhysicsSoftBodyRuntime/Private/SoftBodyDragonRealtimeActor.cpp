#include "SoftBodyDragonRealtimeActor.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PhysicsExperimentComponent.h"
#include "PhysicsResearchTypes.h"
#include "StaticMeshResources.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogSoftBodyDragonRealtime, Log, All);

namespace SoftBodyDragonRealtime
{
FSoftBodyGpuXpbdInput MakeGpuInput(const FSoftBodyTetMesh& Mesh, const FSoftBodyGpuTopology& Topology)
{
	FSoftBodyGpuXpbdInput Input;
	Input.RestPositions = Mesh.RestPositions;
	Input.InverseMasses = Mesh.InverseMasses;
	Input.EdgeVertices = Topology.EdgeVertices;
	Input.EdgeRestLengthsCm = Topology.EdgeRestLengthsCm;
	Input.EdgeMaterialIds = Topology.EdgeMaterialIds;
	Input.VertexEdgeOffsets = Topology.VertexEdgeOffsets;
	Input.VertexEdgeConstraintIndices = Topology.VertexEdgeConstraintIndices;
	Input.VertexEdgeCorners = Topology.VertexEdgeCorners;
	Input.TetrahedronVertices = Topology.TetrahedronVertices;
	Input.TetrahedronRestVolumesCm3 = Topology.TetrahedronRestVolumesCm3;
	Input.TetrahedronMaterialIds = Topology.TetrahedronMaterialIds;
	Input.VertexTetrahedronOffsets = Topology.VertexTetrahedronOffsets;
	Input.VertexTetrahedronConstraintIndices = Topology.VertexTetrahedronConstraintIndices;
	Input.VertexTetrahedronCorners = Topology.VertexTetrahedronCorners;
	return Input;
}

bool IsFinitePosition(const FVector3f& Position)
{
	return FMath::IsFinite(Position.X) && FMath::IsFinite(Position.Y) && FMath::IsFinite(Position.Z);
}
}

ASoftBodyDragonRealtimeActor::ASoftBodyDragonRealtimeActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ExperimentComponent = CreateDefaultSubobject<UPhysicsExperimentComponent>(TEXT("Experiment"));
	ExperimentComponent->ExperimentCase = EPhysicsResearchCase::MultiscaleSoftBody;
	ExperimentComponent->SolverBackend = EPhysicsSolverBackend::ResearchSolver;
	ExperimentComponent->DeterministicSeed = 20260830;
	ExperimentComponent->MaxMetricSamples = 4096;
	ExperimentComponent->StepConfig.FixedDeltaSeconds = 1.0 / 60.0;
	ExperimentComponent->bAutoStart = false;

	DragonSurface = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("DragonDynamicSurface"));
	DragonSurface->SetupAttachment(Root);
	DragonSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DragonSurface->SetCastShadow(true);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialAsset(
		TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
	DragonSurface->SetMaterial(0, MaterialAsset.Object);

	GroundPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GroundContactPlane"));
	GroundPlane->SetupAttachment(Root);
	GroundPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GroundPlane->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	GroundPlane->SetStaticMesh(CubeAsset.Object);
	GroundPlane->SetMaterial(0, MaterialAsset.Object);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetRelativeLocation(FVector(1750.0, -2050.0, 1050.0));
	Camera->SetRelativeRotation(FRotator(-17.0, 132.0, 0.0));
	Camera->FieldOfView = 50.0f;
	Camera->bAutoActivate = true;

	DirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("KeyLight"));
	DirectionalLight->SetupAttachment(Root);
	DirectionalLight->SetRelativeRotation(FRotator(-35.0, -30.0, 0.0));
	DirectionalLight->SetIntensity(6.0f);

	PointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	PointLight->SetupAttachment(Root);
	PointLight->SetRelativeLocation(FVector(-350.0, -500.0, 650.0));
	PointLight->SetIntensity(8500.0f);
	PointLight->SetAttenuationRadius(2600.0f);
	PointLight->SetLightColor(FLinearColor(0.08f, 0.45f, 1.0f));
}

void ASoftBodyDragonRealtimeActor::BeginPlay()
{
	Super::BeginPlay();
	bCaptureRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsSoftBodyDragonRealtimeCapture"));
	bAutoQuitRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsSoftBodyDragonRealtimeAutoQuit"));
	bInteractionScriptRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsSoftBodyDragonInteractionScript"));
	bBackendToggleScriptRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsSoftBodyDragonBackendToggleScript"));
	bWeakGroundContactRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsSoftBodyDragonWeakGroundContact"));
	bCpuReferenceRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsSoftBodyDragonCpuReference"));

	FString FailureReason;
	if (!BuildRuntimeAsset(FailureReason) || !BuildVolumeCageAndBinding(FailureReason) || !InitializeActiveSimulation(FailureReason))
	{
		StatusText = FString::Printf(TEXT("失败：%s"), *FailureReason);
		WriteRunReport();
		UE_LOG(LogSoftBodyDragonRealtime, Error, TEXT("M3_DRAGON_REALTIME_INIT_FAILED reason=%s"), *FailureReason);
		return;
	}

	ExperimentComponent->OnExperimentStep.AddDynamic(this, &ThisClass::HandleExperimentStep);
	ExperimentComponent->ResetExperiment();
	ExperimentComponent->StartExperiment();
	StatusText = bCpuReferenceRequested
		? TEXT("运行：CPU 局部 XPBD 参考后端（非 Chaos Flesh）")
		: bWeakGroundContactRequested
		? TEXT("弱接触消融：仅将地面投影刚度降至 0.02")
		: TEXT("运行：GPU Cage → 地面接触 → Dragon 动态表面");
	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		PlayerController->SetViewTarget(this);
	}
	UE_LOG(LogSoftBodyDragonRealtime, Display,
		TEXT("M3_DRAGON_REALTIME_STARTED surface_vertices=%d triangles=%d cage_vertices=%d tets=%d fixed=%d rest_error_cm=%.9f"),
		GetSurfaceVertexCount(), SurfaceTriangleCount, GetCageVertexCount(), GetCageTetrahedronCount(),
		FixedCageVertexCount, MaximumRestReconstructionErrorCm);
}

void ASoftBodyDragonRealtimeActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GpuSimulation.Shutdown();
	Super::EndPlay(EndPlayReason);
}

int64 ASoftBodyDragonRealtimeActor::GetDisplayedSolverSteps() const
{
	return bCpuReferenceRequested ? CpuLatestStats.StepsExecuted : GpuSimulation.GetStats().StepsExecuted;
}

int64 ASoftBodyDragonRealtimeActor::GetDisplayedLowFrequencyPasses() const
{
	return bCpuReferenceRequested ? CpuLowFrequencyPassesExecuted : GpuSimulation.GetStats().LowFrequencyProjectionPassesExecuted;
}

int64 ASoftBodyDragonRealtimeActor::GetDisplayedGroundContactPasses() const
{
	return bCpuReferenceRequested ? CpuGroundContactPassesExecuted : GpuSimulation.GetStats().GroundContactPassesExecuted;
}

double ASoftBodyDragonRealtimeActor::GetMeanCpuStepMilliseconds() const
{
	return CpuLatestStats.StepsExecuted > 0 ? TotalCpuStepMilliseconds / CpuLatestStats.StepsExecuted : 0.0;
}

void ASoftBodyDragonRealtimeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	HandleInteractiveInput();
	AdvanceInteractionScript();
	if (!bCpuReferenceRequested)
	{
		GpuSimulation.PollReadback();
		if (bAwaitingFinalReadback)
		{
			GpuSimulation.RequestReadback();
		}
		ProcessLatestReadback();
	}
	DrawCageDebug();
}

void ASoftBodyDragonRealtimeActor::HandleInteractiveInput()
{
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PlayerController || bRunComplete || bAwaitingFinalReadback)
	{
		return;
	}
	if (PlayerController->WasInputKeyJustPressed(EKeys::SpaceBar)) TogglePauseResume();
	if (PlayerController->WasInputKeyJustPressed(EKeys::N)) ExecuteSingleStep();
	if (PlayerController->WasInputKeyJustPressed(EKeys::R))
	{
		++ResetCount;
		ResetInteractiveRun(true);
	}
	if (PlayerController->WasInputKeyJustPressed(EKeys::D)) ToggleDebugDraw();
	if (PlayerController->WasInputKeyJustPressed(EKeys::E)) ExportManualMetrics();
	if (PlayerController->WasInputKeyJustPressed(EKeys::F)) ToggleGroundContactProfile();
	if (PlayerController->WasInputKeyJustPressed(EKeys::B)) ToggleSolverBackend();
}

void ASoftBodyDragonRealtimeActor::AdvanceInteractionScript()
{
	if (bBackendToggleScriptRequested && !bRunComplete && !bAwaitingFinalReadback && ExperimentComponent)
	{
		if (InteractionScriptPhase == 0 && !bCpuReferenceRequested
			&& ExperimentComponent->GetExperimentStats().StepIndex >= 30)
		{
			ToggleSolverBackend();
			InteractionScriptPhase = 1;
		}
		return;
	}
	if (!bInteractionScriptRequested || bRunComplete || bAwaitingFinalReadback || !ExperimentComponent)
	{
		return;
	}
	const int64 Step = ExperimentComponent->GetExperimentStats().StepIndex;
	if (InteractionScriptPhase == 0 && Step >= 30)
	{
		TogglePauseResume();
		InteractionScriptPhase = 1;
	}
	else if (InteractionScriptPhase == 1)
	{
		ExecuteSingleStep();
		InteractionScriptPhase = 2;
	}
	else if (InteractionScriptPhase == 2)
	{
		TogglePauseResume();
		InteractionScriptPhase = 3;
	}
	else if (InteractionScriptPhase == 3 && Step >= 60)
	{
		++ResetCount;
		ResetInteractiveRun(true);
		InteractionScriptPhase = 4;
	}
	else if (InteractionScriptPhase == 4 && Step >= 30)
	{
		ToggleDebugDraw();
		InteractionScriptPhase = 5;
	}
	else if (InteractionScriptPhase == 5 && Step >= 60)
	{
		ToggleDebugDraw();
		InteractionScriptPhase = 6;
	}
	else if (InteractionScriptPhase == 6 && Step >= 90)
	{
		ExportManualMetrics();
		InteractionScriptPhase = 7;
	}
}

void ASoftBodyDragonRealtimeActor::TogglePauseResume()
{
	if (!ExperimentComponent || bRunComplete || bAwaitingFinalReadback)
	{
		return;
	}
	if (ExperimentComponent->IsExperimentRunning())
	{
		ExperimentComponent->PauseExperiment();
		StatusText = TEXT("已暂停：N 单步 / R 重置 / 空格继续");
	}
	else
	{
		ExperimentComponent->StartExperiment();
		StatusText = TEXT("运行：GPU Cage → Dragon 动态表面");
	}
	++PauseToggleCount;
}

void ASoftBodyDragonRealtimeActor::ExecuteSingleStep()
{
	if (!ExperimentComponent || ExperimentComponent->IsExperimentRunning() || bRunComplete || bAwaitingFinalReadback)
	{
		return;
	}
	ExperimentComponent->StepExperimentOnce();
	StatusText = TEXT("已单步：等待对应异步 Cage 快照");
	++SingleStepCount;
}

void ASoftBodyDragonRealtimeActor::ToggleDebugDraw()
{
	bEnableDebugDraw = !bEnableDebugDraw;
	++DebugToggleCount;
}

void ASoftBodyDragonRealtimeActor::ToggleGroundContactProfile()
{
	bWeakGroundContactRequested = !bWeakGroundContactRequested;
	++ContactProfileToggleCount;
	ResetInteractiveRun(true);
}

void ASoftBodyDragonRealtimeActor::ToggleSolverBackend()
{
	bCpuReferenceRequested = !bCpuReferenceRequested;
	++BackendToggleCount;
	ResetInteractiveRun(true);
}

void ASoftBodyDragonRealtimeActor::ExportManualMetrics()
{
	FString ExportPath;
	ExperimentComponent->ExportMetrics(TEXT("M3_DragonGpuSurfaceManual"), EPhysicsMetricExportFormat::Json, ExportPath);
	ExperimentComponent->ExportMetrics(TEXT("M3_DragonGpuSurfaceManual"), EPhysicsMetricExportFormat::Csv, ExportPath);
	++ManualExportCount;
}

void ASoftBodyDragonRealtimeActor::ResetRunMeasurements()
{
	LatestCagePositions = VolumeCage.RestPositions;
	LatestSurfacePositions = SurfaceBinding.RestSurfacePositions;
	LatestReadbackStep = 0;
	SurfaceUpdateCount = 0;
	MaximumSurfaceDisplacementCm = 0.0;
	MaximumFixedFaceDriftCm = 0.0;
	MaximumGroundPenetrationCm = 0.0;
	ActiveGroundContactVertices = 0;
	CpuLatestStats = FSoftBodyLocalXpbdStats();
	CpuLowFrequencyPassesExecuted = 0;
	CpuGroundContactPassesExecuted = 0;
	TotalCpuStepMilliseconds = 0.0;
	MaximumCpuStepMilliseconds = 0.0;
	bAwaitingFinalReadback = false;
	bRunComplete = false;
	bRunPassed = false;
	UpdateDynamicSurface(LatestSurfacePositions);
}

bool ASoftBodyDragonRealtimeActor::ResetInteractiveRun(bool bStartAfterReset)
{
	ExperimentComponent->PauseExperiment();
	GpuSimulation.Shutdown();
	CpuSimulation.Reset();
	ResetRunMeasurements();
	FString FailureReason;
	if (!InitializeActiveSimulation(FailureReason))
	{
		StatusText = FString::Printf(TEXT("重置失败：%s"), *FailureReason);
		WriteRunReport();
		return false;
	}
	ExperimentComponent->ResetExperiment();
	if (bStartAfterReset)
	{
		ExperimentComponent->StartExperiment();
	}
	StatusText = bStartAfterReset ? TEXT("已重置：GPU Buffer 与 Dragon 表面回到静止态") : TEXT("已重置并暂停");
	return true;
}

void ASoftBodyDragonRealtimeActor::HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex)
{
	if (bCpuReferenceRequested)
	{
		ProcessCpuStep(StepIndex);
		return;
	}
	if (!bRunComplete && !bAwaitingFinalReadback)
	{
		GpuSimulation.EnqueueStep();
	}
	if (StepIndex >= StepsToRun && !bAwaitingFinalReadback)
	{
		ExperimentComponent->PauseExperiment();
		bAwaitingFinalReadback = true;
		StatusText = TEXT("GPU 已排入 300 步，等待最终异步 Cage 快照");
		GpuSimulation.RequestReadback();
	}
}

bool ASoftBodyDragonRealtimeActor::BuildRuntimeAsset(FString& OutFailureReason)
{
	UStaticMesh* SurfaceAsset = LoadObject<UStaticMesh>(nullptr,
		TEXT("/PhysicsSoftBody/DragonDerived/SM_TerrorBringer_SoftBodyCage.SM_TerrorBringer_SoftBodyCage"));
	if (!SurfaceAsset || !SurfaceAsset->GetRenderData() || SurfaceAsset->GetRenderData()->LODResources.IsEmpty())
	{
		OutFailureReason = TEXT("缺少可读的本地 Dragon 派生表面，请先运行导入脚本");
		return false;
	}
	const FStaticMeshLODResources& Lod = SurfaceAsset->GetRenderData()->LODResources[0];
	const FPositionVertexBuffer& PositionBuffer = Lod.VertexBuffers.PositionVertexBuffer;
	if (PositionBuffer.GetNumVertices() == 0 || Lod.IndexBuffer.GetNumIndices() == 0)
	{
		OutFailureReason = TEXT("Dragon LOD0 顶点或索引为空");
		return false;
	}

	UE::Geometry::FDynamicMesh3 Mesh;
	Mesh.EnableVertexNormals(FVector3f::UnitZ());
	TArray<FVector3f> RestSurfacePositions;
	RestSurfacePositions.Reserve(PositionBuffer.GetNumVertices());
	for (uint32 Vertex = 0; Vertex < PositionBuffer.GetNumVertices(); ++Vertex)
	{
		const FVector3f Position = PositionBuffer.VertexPosition(Vertex);
		if (Mesh.AppendVertex(FVector3d(Position)) != static_cast<int32>(Vertex))
		{
			OutFailureReason = TEXT("动态表面顶点编号不连续");
			return false;
		}
		RestSurfacePositions.Add(Position);
	}
	const FIndexArrayView Indices = Lod.IndexBuffer.GetArrayView();
	for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
	{
		const int32 Triangle = Mesh.AppendTriangle(
			static_cast<int32>(Indices[Index]), static_cast<int32>(Indices[Index + 1]), static_cast<int32>(Indices[Index + 2]));
		if (Triangle < 0)
		{
			OutFailureReason = FString::Printf(TEXT("动态表面三角面 %d 导入失败，错误码 %d"), Index / 3, Triangle);
			return false;
		}
	}
	UE::Geometry::FMeshNormals::QuickComputeVertexNormals(Mesh);
	SurfaceTriangleCount = Mesh.TriangleCount();
	DragonSurface->SetMesh(MoveTemp(Mesh));
	SurfaceBinding.RestSurfacePositions = MoveTemp(RestSurfacePositions);
	return true;
}

bool ASoftBodyDragonRealtimeActor::BuildVolumeCageAndBinding(FString& OutFailureReason)
{
	if (SurfaceBinding.RestSurfacePositions.IsEmpty())
	{
		OutFailureReason = TEXT("Dragon 表面为空");
		return false;
	}
	FVector3f Minimum(TNumericLimits<float>::Max());
	FVector3f Maximum(-TNumericLimits<float>::Max());
	for (const FVector3f& Position : SurfaceBinding.RestSurfacePositions)
	{
		Minimum.X = FMath::Min(Minimum.X, Position.X); Minimum.Y = FMath::Min(Minimum.Y, Position.Y); Minimum.Z = FMath::Min(Minimum.Z, Position.Z);
		Maximum.X = FMath::Max(Maximum.X, Position.X); Maximum.Y = FMath::Max(Maximum.Y, Position.Y); Maximum.Z = FMath::Max(Maximum.Z, Position.Z);
	}
	Minimum -= FVector3f(0.1f);
	Maximum += FVector3f(0.1f);
	GridSettings.CellCounts = FIntVector(8, 8, 8);
	GridSettings.OriginCm = Minimum;
	GridSettings.CellSizeCm = FVector3f(
		(Maximum.X - Minimum.X) / GridSettings.CellCounts.X,
		(Maximum.Y - Minimum.Y) / GridSettings.CellCounts.Y,
		(Maximum.Z - Minimum.Z) / GridSettings.CellCounts.Z);
	GridSettings.bFixMinimumXFace = true;
	const float GroundPlaneZCm = GridSettings.OriginCm.Z - 20.0f;
	const FVector3f GridExtent = GridSettings.CellSizeCm * FVector3f(GridSettings.CellCounts);
	GroundPlane->SetRelativeLocation(FVector(
		GridSettings.OriginCm.X + 0.5f * GridExtent.X,
		GridSettings.OriginCm.Y + 0.5f * GridExtent.Y,
		GroundPlaneZCm - 10.0f));
	GroundPlane->SetRelativeScale3D(FVector(
		(GridExtent.X + 300.0f) / 100.0f,
		(GridExtent.Y + 300.0f) / 100.0f,
		0.2f));
	if (!FSoftBodyTetrahedralMeshBuilder::BuildRegularBlock(GridSettings, VolumeCage, &OutFailureReason)
		|| !FSoftBodyGpuTopologyBuilder::Build(VolumeCage, GpuTopology, &OutFailureReason))
	{
		return false;
	}
	FSoftBodySurfaceBinding Binding;
	if (!FSoftBodySurfaceBindingBuilder::BuildTrilinearGridBinding(
		GridSettings, SurfaceBinding.RestSurfacePositions, Binding, &OutFailureReason))
	{
		return false;
	}
	SurfaceBinding = MoveTemp(Binding);
	MaximumRestReconstructionErrorCm = SurfaceBinding.CalculateMaximumRestReconstructionErrorCm(VolumeCage.RestPositions);
	FixedCageVertexCount = VolumeCage.CountFixedVertices();
	LatestCagePositions = VolumeCage.RestPositions;
	LatestSurfacePositions = SurfaceBinding.RestSurfacePositions;
	return MaximumRestReconstructionErrorCm < 0.01 && FixedCageVertexCount == 81;
}

bool ASoftBodyDragonRealtimeActor::InitializeGpuSimulation(FString& OutFailureReason)
{
	GpuSettings.FixedDeltaSeconds = 1.0f / 60.0f;
	GpuSettings.SolverIterations = 32;
	GpuSettings.GravityCmPerSecondSquared = FVector3f(0.0f, 0.0f, -490.0f);
	GpuSettings.VelocityDamping = 0.985f;
	GpuSettings.JacobiRelaxation = 0.82f;
	GpuSettings.MaximumVertexCorrectionCm = 8.0f;
	GpuSettings.ReadbackIntervalSteps = 4;
	GpuSettings.bEnableLowFrequencyCorrection = true;
	GpuSettings.LowFrequencyCorrectionStrength = 0.35f;
	GpuSettings.LowFrequencyCorrectionInterval = 1;
	GpuSettings.bEnableGroundContact = true;
	GpuSettings.GroundPlaneZCm = GridSettings.OriginCm.Z - 20.0f;
	GpuSettings.GroundContactStiffness = bWeakGroundContactRequested ? 0.02f : 1.0f;
	if (!GpuSimulation.Initialize(SoftBodyDragonRealtime::MakeGpuInput(VolumeCage, GpuTopology), GpuSettings))
	{
		OutFailureReason = TEXT("持久 GPU 四面体会话初始化被拒绝");
		return false;
	}
	return true;
}

bool ASoftBodyDragonRealtimeActor::InitializeCpuSimulation(FString& OutFailureReason)
{
	CpuSettings.FixedDeltaSeconds = 1.0 / 60.0;
	CpuSettings.SolverIterations = 32;
	CpuSettings.GravityCmPerSecondSquared = FVector3d(0.0, 0.0, -490.0);
	CpuSettings.VelocityDamping = 0.985;
	CpuSettings.JacobiRelaxation = 0.82;
	CpuSettings.MaximumVertexCorrectionCm = 8.0;
	CpuSettings.bEnableLowFrequencyCorrection = true;
	CpuSettings.LowFrequencyCorrectionStrength = 0.35;
	CpuSettings.LowFrequencyCorrectionInterval = 1;
	CpuSettings.bEnableGroundContact = true;
	CpuSettings.GroundPlaneZCm = GridSettings.OriginCm.Z - 20.0;
	CpuSettings.GroundContactStiffness = bWeakGroundContactRequested ? 0.02 : 1.0;
	if (!CpuSimulation.Initialize(VolumeCage, CpuSettings, &OutFailureReason))
	{
		return false;
	}
	return true;
}

bool ASoftBodyDragonRealtimeActor::InitializeActiveSimulation(FString& OutFailureReason)
{
	return bCpuReferenceRequested ? InitializeCpuSimulation(OutFailureReason) : InitializeGpuSimulation(OutFailureReason);
}

void ASoftBodyDragonRealtimeActor::ProcessCpuStep(int64 StepIndex)
{
	if (bRunComplete || bAwaitingFinalReadback)
	{
		return;
	}
	FString FailureReason;
	const double StartSeconds = FPlatformTime::Seconds();
	if (!CpuSimulation.Step(CpuLatestStats, &FailureReason))
	{
		ExperimentComponent->PauseExperiment();
		bRunComplete = true;
		bRunPassed = false;
		StatusText = FString::Printf(TEXT("CPU 参考后端失败：%s"), *FailureReason);
		WriteRunReport();
		return;
	}
	const double StepMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	TotalCpuStepMilliseconds += StepMilliseconds;
	MaximumCpuStepMilliseconds = FMath::Max(MaximumCpuStepMilliseconds, StepMilliseconds);
	CpuLowFrequencyPassesExecuted += CpuLatestStats.LowFrequencyCorrectionPasses;
	CpuGroundContactPassesExecuted += CpuLatestStats.GroundContactPassesExecuted;
	LatestCagePositions.Reset(CpuSimulation.GetPositions().Num());
	for (const FVector3d& Position : CpuSimulation.GetPositions())
	{
		LatestCagePositions.Add(FVector3f(Position));
	}
	LatestReadbackStep = StepIndex;
	MaximumFixedFaceDriftCm = CpuLatestStats.FixedVertexDriftCm;
	MaximumGroundPenetrationCm = FMath::Max(MaximumGroundPenetrationCm, CpuLatestStats.MaximumGroundPenetrationCm);
	ActiveGroundContactVertices = CpuLatestStats.ActiveGroundContactVertices;
	FString BindingFailure;
	if (!SurfaceBinding.Apply(LatestCagePositions, LatestSurfacePositions, &BindingFailure))
	{
		ExperimentComponent->PauseExperiment();
		bRunComplete = true;
		StatusText = FString::Printf(TEXT("CPU 表面绑定失败：%s"), *BindingFailure);
		WriteRunReport();
		return;
	}
	MaximumSurfaceDisplacementCm = 0.0;
	for (int32 Vertex = 0; Vertex < LatestSurfacePositions.Num(); ++Vertex)
	{
		MaximumSurfaceDisplacementCm = FMath::Max(MaximumSurfaceDisplacementCm,
			static_cast<double>((LatestSurfacePositions[Vertex] - SurfaceBinding.RestSurfacePositions[Vertex]).Length()));
	}
	UpdateDynamicSurface(LatestSurfacePositions);
	++SurfaceUpdateCount;
	ExperimentComponent->RecordMetric(TEXT("CpuStepMilliseconds"), StepMilliseconds, TEXT("ms"));
	ExperimentComponent->RecordMetric(TEXT("MaximumSurfaceDisplacement"), MaximumSurfaceDisplacementCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("MaximumGroundPenetration"), MaximumGroundPenetrationCm, TEXT("cm"));
	if (StepIndex >= StepsToRun)
	{
		ExperimentComponent->PauseExperiment();
		CompleteRun();
	}
}

void ASoftBodyDragonRealtimeActor::ProcessLatestReadback()
{
	TArray<FVector3f> Positions;
	int64 ReadbackStep = 0;
	if (!GpuSimulation.ConsumeLatestPositions(Positions, ReadbackStep))
	{
		return;
	}
	if (Positions.Num() != VolumeCage.RestPositions.Num())
	{
		StatusText = TEXT("失败：GPU Cage 顶点数量发生变化");
		return;
	}
	for (const FVector3f& Position : Positions)
	{
		if (!SoftBodyDragonRealtime::IsFinitePosition(Position))
		{
			StatusText = TEXT("失败：GPU Cage 出现非有限坐标");
			return;
		}
	}
	LatestCagePositions = MoveTemp(Positions);
	LatestReadbackStep = ReadbackStep;
	MaximumFixedFaceDriftCm = 0.0;
	ActiveGroundContactVertices = 0;
	for (int32 Vertex = 0; Vertex < LatestCagePositions.Num(); ++Vertex)
	{
		if (VolumeCage.InverseMasses[Vertex] <= 0.0f)
		{
			MaximumFixedFaceDriftCm = FMath::Max(MaximumFixedFaceDriftCm,
				static_cast<double>((LatestCagePositions[Vertex] - VolumeCage.RestPositions[Vertex]).Length()));
		}
		else
		{
			MaximumGroundPenetrationCm = FMath::Max(MaximumGroundPenetrationCm,
				static_cast<double>(FMath::Max(0.0f, GpuSettings.GroundPlaneZCm - LatestCagePositions[Vertex].Z)));
			if (FMath::Abs(LatestCagePositions[Vertex].Z - GpuSettings.GroundPlaneZCm) < 0.01f)
			{
				++ActiveGroundContactVertices;
			}
		}
	}
	FString FailureReason;
	if (!SurfaceBinding.Apply(LatestCagePositions, LatestSurfacePositions, &FailureReason))
	{
		StatusText = FString::Printf(TEXT("失败：%s"), *FailureReason);
		return;
	}
	MaximumSurfaceDisplacementCm = 0.0;
	for (int32 Vertex = 0; Vertex < LatestSurfacePositions.Num(); ++Vertex)
	{
		MaximumSurfaceDisplacementCm = FMath::Max(MaximumSurfaceDisplacementCm,
			static_cast<double>((LatestSurfacePositions[Vertex] - SurfaceBinding.RestSurfacePositions[Vertex]).Length()));
	}
	UpdateDynamicSurface(LatestSurfacePositions);
	++SurfaceUpdateCount;
	ExperimentComponent->RecordMetric(TEXT("MaximumSurfaceDisplacement"), MaximumSurfaceDisplacementCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("FixedFaceDrift"), MaximumFixedFaceDriftCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("MaximumGroundPenetration"), MaximumGroundPenetrationCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("ActiveGroundContactVertices"), ActiveGroundContactVertices, TEXT("vertices"));
	ExperimentComponent->RecordMetric(TEXT("SurfaceUpdateCount"), SurfaceUpdateCount, TEXT("updates"));
	if (bAwaitingFinalReadback && LatestReadbackStep >= StepsToRun)
	{
		CompleteRun();
	}
}

void ASoftBodyDragonRealtimeActor::UpdateDynamicSurface(TConstArrayView<FVector3f> Positions)
{
	DragonSurface->EditMesh([&Positions](UE::Geometry::FDynamicMesh3& Mesh)
	{
		for (int32 Vertex = 0; Vertex < Positions.Num(); ++Vertex)
		{
			Mesh.SetVertex(Vertex, FVector3d(Positions[Vertex]));
		}
		UE::Geometry::FMeshNormals::QuickComputeVertexNormals(Mesh);
	}, EDynamicMeshComponentRenderUpdateMode::FastUpdate);
}

void ASoftBodyDragonRealtimeActor::DrawCageDebug() const
{
	if (!bEnableDebugDraw || !GetWorld() || LatestCagePositions.Num() != VolumeCage.RestPositions.Num())
	{
		return;
	}
	const FColor CageColor(20, 190, 255);
	const FColor FixedColor(255, 65, 45);
	const int32 SizeX = GridSettings.CellCounts.X + 1;
	const int32 SizeY = GridSettings.CellCounts.Y + 1;
	const int32 SizeZ = GridSettings.CellCounts.Z + 1;
	const auto VertexIndex = [SizeX, SizeY](int32 X, int32 Y, int32 Z)
	{
		return X + SizeX * (Y + SizeY * Z);
	};
	const auto DrawGridEdge = [this, CageColor](int32 A, int32 B)
	{
		DrawDebugLine(GetWorld(), FVector(LatestCagePositions[A]), FVector(LatestCagePositions[B]),
			CageColor, false, 0.0f, 0, 1.15f);
	};
	// 只画规则 Cage 的十二条分段外框，避免内部四面体边遮挡 Dragon 表面。
	for (int32 X = 0; X + 1 < SizeX; ++X)
	{
		for (int32 Y : { 0, SizeY - 1 }) for (int32 Z : { 0, SizeZ - 1 })
		{
			DrawGridEdge(VertexIndex(X, Y, Z), VertexIndex(X + 1, Y, Z));
		}
	}
	for (int32 Y = 0; Y + 1 < SizeY; ++Y)
	{
		for (int32 X : { 0, SizeX - 1 }) for (int32 Z : { 0, SizeZ - 1 })
		{
			DrawGridEdge(VertexIndex(X, Y, Z), VertexIndex(X, Y + 1, Z));
		}
	}
	for (int32 Z = 0; Z + 1 < SizeZ; ++Z)
	{
		for (int32 X : { 0, SizeX - 1 }) for (int32 Y : { 0, SizeY - 1 })
		{
			DrawGridEdge(VertexIndex(X, Y, Z), VertexIndex(X, Y, Z + 1));
		}
	}
	for (int32 Vertex = 0; Vertex < LatestCagePositions.Num(); ++Vertex)
	{
		if (VolumeCage.InverseMasses[Vertex] <= 0.0f)
		{
			DrawDebugPoint(GetWorld(), FVector(LatestCagePositions[Vertex]), 5.0f, FixedColor, false, 0.0f, 0);
		}
	}
}

void ASoftBodyDragonRealtimeActor::CompleteRun()
{
	bAwaitingFinalReadback = false;
	bRunComplete = true;
	const FSoftBodyGpuXpbdStats Stats = GpuSimulation.GetStats();
	const bool bBackendContractPassed = bCpuReferenceRequested
		? (CpuLatestStats.StepsExecuted == StepsToRun
			&& LatestReadbackStep == StepsToRun
			&& CpuLatestStats.LowFrequencyModeCount == 2
			&& CpuLowFrequencyPassesExecuted == StepsToRun * CpuSettings.SolverIterations
			&& CpuGroundContactPassesExecuted == StepsToRun
			&& SurfaceUpdateCount == StepsToRun
			&& FMath::IsFinite(GetMeanCpuStepMilliseconds())
			&& GetMeanCpuStepMilliseconds() > 0.0)
		: (Stats.StepsExecuted == StepsToRun
			&& LatestReadbackStep >= StepsToRun
			&& Stats.ReadbacksCompleted > 0
			&& Stats.LowFrequencyModeCount == 2
			&& Stats.LowFrequencyProjectionPassesExecuted == StepsToRun * GpuSettings.SolverIterations
			&& Stats.LowFrequencyApplyPassesExecuted == StepsToRun * GpuSettings.SolverIterations
			&& Stats.GroundContactPassesExecuted == StepsToRun);
	bRunPassed = bBackendContractPassed
		&& SurfaceUpdateCount > 0
		&& GetSurfaceVertexCount() == 4854
		&& SurfaceTriangleCount == 6000
		&& GetCageVertexCount() == 729
		&& GetCageTetrahedronCount() == 3072
		&& FixedCageVertexCount == 81
		&& MaximumRestReconstructionErrorCm < 0.01
		&& MaximumFixedFaceDriftCm < 1.e-3
		&& FMath::IsFinite(MaximumSurfaceDisplacementCm)
		&& MaximumSurfaceDisplacementCm > 1.0
		&& (bWeakGroundContactRequested
			? MaximumGroundPenetrationCm > 0.5
			: (MaximumGroundPenetrationCm < 1.e-3 && ActiveGroundContactVertices > 0))
		&& (!bCpuReferenceRequested || MaximumSurfaceDisplacementCm > 200.0)
		&& (!bBackendToggleScriptRequested || (bCpuReferenceRequested && BackendToggleCount == 1))
		&& (!bInteractionScriptRequested || (PauseToggleCount == 2 && SingleStepCount == 1
			&& ResetCount == 1 && DebugToggleCount == 2 && ManualExportCount == 1 && bEnableDebugDraw));
	StatusText = bRunPassed
		? (bCpuReferenceRequested
			? TEXT("对照完成：执行合同通过，局部 CPU 物理质量失败（预期）")
			: bWeakGroundContactRequested
			? TEXT("预期失败已观测：弱接触产生明显地面穿透")
			: TEXT("通过：标准接触阻止 Dragon Cage 穿透地面"))
		: TEXT("失败：检查最终步、绑定尺寸、固定端或可见形变");
	FString ExportPath;
	ExperimentComponent->ExportMetrics(GetReportStem(), EPhysicsMetricExportFormat::Json, ExportPath);
	ExperimentComponent->ExportMetrics(GetReportStem(), EPhysicsMetricExportFormat::Csv, ExportPath);
	WriteRunReport();
	UE_LOG(LogSoftBodyDragonRealtime, Display,
		TEXT("M3_DRAGON_REALTIME_COMPLETE passed=%s backend=%s profile=%s steps=%lld readbacks=%lld latest_step=%lld surface_updates=%lld surface_vertices=%d triangles=%d cage_vertices=%d tets=%d fixed=%d rest_error_cm=%.9f fixed_drift_cm=%.9f max_surface_displacement_cm=%.6f max_ground_penetration_cm=%.9f active_ground_contacts=%d ground_contact_passes=%lld low_frequency_modes=%d cpu_mean_step_ms=%.6f cpu_max_step_ms=%.6f interaction=%s pause=%d single_step=%d reset=%d debug=%d export=%d profile_toggles=%d backend_toggles=%d"),
		bRunPassed ? TEXT("true") : TEXT("false"), bCpuReferenceRequested ? TEXT("cpu_local_xpbd_reference") : TEXT("persistent_d3d12_xpbd"), bWeakGroundContactRequested ? TEXT("weak_0.02") : TEXT("standard_1.0"), GetDisplayedSolverSteps(), bCpuReferenceRequested ? 0 : Stats.ReadbacksCompleted, LatestReadbackStep,
		SurfaceUpdateCount, GetSurfaceVertexCount(), SurfaceTriangleCount, GetCageVertexCount(), GetCageTetrahedronCount(),
		FixedCageVertexCount, MaximumRestReconstructionErrorCm, MaximumFixedFaceDriftCm, MaximumSurfaceDisplacementCm,
		MaximumGroundPenetrationCm, ActiveGroundContactVertices, GetDisplayedGroundContactPasses(),
		bCpuReferenceRequested ? CpuLatestStats.LowFrequencyModeCount : Stats.LowFrequencyModeCount,
		GetMeanCpuStepMilliseconds(), MaximumCpuStepMilliseconds, bInteractionScriptRequested ? TEXT("true") : TEXT("false"), PauseToggleCount,
		SingleStepCount, ResetCount, DebugToggleCount, ManualExportCount, ContactProfileToggleCount, BackendToggleCount);
	if (bCaptureRequested || bAutoQuitRequested)
	{
		GetWorldTimerManager().SetTimer(CaptureTimerHandle, this, &ThisClass::CaptureScreenshotAndScheduleExit, 0.8f, false);
	}
}

void ASoftBodyDragonRealtimeActor::WriteRunReport() const
{
	const FSoftBodyGpuXpbdStats Stats = GpuSimulation.GetStats();
	const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M3"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	if (bCpuReferenceRequested)
	{
		const FString CpuPath = FPaths::Combine(Directory, bBackendToggleScriptRequested
			? TEXT("dragon_backend_toggle_runtime.json") : TEXT("dragon_cpu_local_xpbd_reference_runtime.json"));
		const FString CpuJson = FString::Printf(
			TEXT("{\n  \"result\": \"%s\",\n  \"execution_contract_passed\": %s,\n  \"physics_quality_gate_passed\": false,\n  \"expected_local_propagation_failure_observed\": %s,\n  \"quality_failure_reason\": \"same 32-iteration budget produces more than 200 cm maximum surface displacement on the 729-vertex cage\",\n  \"backend\": \"CPU local XPBD research reference\",\n  \"not_chaos_flesh\": true,\n  \"chaos_flesh_status\": \"UE 5.4.4 plugin forces ChaosFleshNodes startup path with recorded reflected-property defect\",\n  \"comparison_contract\": \"same Dragon surface, tetrahedral cage, fixed dt, 32 iterations, gravity, damping, low-frequency modes, ground plane and contact stiffness as GPU standard profile\",\n  \"steps_executed\": %lld,\n  \"latest_surface_step\": %lld,\n  \"surface_updates\": %lld,\n  \"surface_vertices\": %d,\n  \"surface_triangles\": %d,\n  \"cage_vertices\": %d,\n  \"cage_tetrahedra\": %d,\n  \"fixed_cage_vertices\": %d,\n  \"rest_reconstruction_error_cm\": %.12f,\n  \"maximum_fixed_face_drift_cm\": %.12f,\n  \"maximum_surface_displacement_cm\": %.9f,\n  \"ground_plane_z_cm\": %.9f,\n  \"ground_contact_stiffness\": %.6f,\n  \"maximum_ground_penetration_cm\": %.9f,\n  \"active_ground_contact_vertices\": %d,\n  \"ground_contact_passes\": %lld,\n  \"low_frequency_modes\": %d,\n  \"low_frequency_projection_passes\": %lld,\n  \"mean_cpu_step_ms\": %.9f,\n  \"maximum_cpu_step_ms\": %.9f,\n  \"backend_toggle_key\": \"B\",\n  \"backend_toggle_script\": %s,\n  \"backend_toggles\": %d,\n  \"license_boundary\": \"Runtime mesh is a local derivative of the installed Fab Dragon asset and is gitignored\"\n}\n"),
			bRunPassed ? TEXT("passed_expected_failure") : TEXT("failed"), bRunPassed ? TEXT("true") : TEXT("false"),
			(bRunPassed && MaximumSurfaceDisplacementCm > 200.0) ? TEXT("true") : TEXT("false"), CpuLatestStats.StepsExecuted, LatestReadbackStep,
			SurfaceUpdateCount, GetSurfaceVertexCount(), SurfaceTriangleCount, GetCageVertexCount(), GetCageTetrahedronCount(),
			FixedCageVertexCount, MaximumRestReconstructionErrorCm, MaximumFixedFaceDriftCm, MaximumSurfaceDisplacementCm,
			CpuSettings.GroundPlaneZCm, CpuSettings.GroundContactStiffness, MaximumGroundPenetrationCm,
			ActiveGroundContactVertices, CpuGroundContactPassesExecuted, CpuLatestStats.LowFrequencyModeCount,
			CpuLowFrequencyPassesExecuted, GetMeanCpuStepMilliseconds(), MaximumCpuStepMilliseconds,
			bBackendToggleScriptRequested ? TEXT("true") : TEXT("false"), BackendToggleCount);
		FFileHelper::SaveStringToFile(CpuJson, *CpuPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		return;
	}
	const FString Path = FPaths::Combine(Directory, bWeakGroundContactRequested
		? TEXT("dragon_gpu_surface_weak_contact_runtime.json")
		: (bInteractionScriptRequested ? TEXT("dragon_gpu_surface_interaction_runtime.json") : TEXT("dragon_gpu_surface_contact_runtime.json")));
	const FString Json = FString::Printf(
		TEXT("{\n  \"result\": \"%s\",\n  \"contact_profile\": \"%s\",\n  \"expected_failure_observed\": %s,\n  \"single_parameter_ablation\": \"ground_contact_stiffness only; 1.0 versus 0.02\",\n  \"solver\": \"Persistent D3D12 tetrahedral XPBD with two-mode low-frequency correction and one ground projection per fixed step\",\n  \"surface_binding\": \"Deterministic trilinear 8-node cage weights\",\n  \"steps_executed\": %lld,\n  \"readbacks_completed\": %lld,\n  \"latest_readback_step\": %lld,\n  \"surface_updates\": %lld,\n  \"surface_vertices\": %d,\n  \"surface_triangles\": %d,\n  \"cage_vertices\": %d,\n  \"cage_tetrahedra\": %d,\n  \"fixed_cage_vertices\": %d,\n  \"rest_reconstruction_error_cm\": %.12f,\n  \"maximum_fixed_face_drift_cm\": %.12f,\n  \"maximum_surface_displacement_cm\": %.9f,\n  \"ground_plane_z_cm\": %.9f,\n  \"ground_contact_stiffness\": %.6f,\n  \"maximum_ground_penetration_cm\": %.9f,\n  \"active_ground_contact_vertices\": %d,\n  \"ground_contact_passes\": %lld,\n  \"low_frequency_modes\": %d,\n  \"low_frequency_projection_passes\": %lld,\n  \"low_frequency_apply_passes\": %lld,\n  \"interaction_script\": %s,\n  \"pause_toggles\": %d,\n  \"single_steps\": %d,\n  \"resets\": %d,\n  \"debug_toggles\": %d,\n  \"manual_exports\": %d,\n  \"contact_profile_toggles\": %d,\n  \"debug_draw_enabled_at_end\": %s,\n  \"license_boundary\": \"Runtime mesh is a local derivative of the installed Fab Dragon asset and is gitignored\"\n}\n"),
		bRunPassed ? TEXT("passed") : TEXT("failed"), bWeakGroundContactRequested ? TEXT("weak_0.02") : TEXT("standard_1.0"),
		(bWeakGroundContactRequested && bRunPassed) ? TEXT("true") : TEXT("false"), Stats.StepsExecuted, Stats.ReadbacksCompleted, LatestReadbackStep,
		SurfaceUpdateCount, GetSurfaceVertexCount(), SurfaceTriangleCount, GetCageVertexCount(), GetCageTetrahedronCount(),
		FixedCageVertexCount, MaximumRestReconstructionErrorCm, MaximumFixedFaceDriftCm, MaximumSurfaceDisplacementCm,
		GpuSettings.GroundPlaneZCm, GpuSettings.GroundContactStiffness, MaximumGroundPenetrationCm,
		ActiveGroundContactVertices, Stats.GroundContactPassesExecuted, Stats.LowFrequencyModeCount,
		Stats.LowFrequencyProjectionPassesExecuted, Stats.LowFrequencyApplyPassesExecuted,
		bInteractionScriptRequested ? TEXT("true") : TEXT("false"), PauseToggleCount, SingleStepCount, ResetCount,
		DebugToggleCount, ManualExportCount, ContactProfileToggleCount, bEnableDebugDraw ? TEXT("true") : TEXT("false"));
	FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

FString ASoftBodyDragonRealtimeActor::GetReportStem() const
{
	return bCpuReferenceRequested ? (bBackendToggleScriptRequested ? TEXT("M3_DragonBackendToggle") : TEXT("M3_DragonCpuLocalXpbdReference"))
		: bWeakGroundContactRequested ? TEXT("M3_DragonGpuSurface_WeakContact")
		: (bInteractionScriptRequested ? TEXT("M3_DragonGpuSurface_Interaction") : TEXT("M3_DragonGpuSurface_Contact"));
}

void ASoftBodyDragonRealtimeActor::CaptureScreenshotAndScheduleExit()
{
	if (bCaptureRequested)
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M3/Screenshots"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString Path = FPaths::Combine(Directory, bCpuReferenceRequested
			? (bBackendToggleScriptRequested ? TEXT("M3_DragonBackendToggle.png") : TEXT("M3_DragonCpuLocalXpbdReference.png"))
			: bWeakGroundContactRequested
			? TEXT("M3_DragonGpuSurface_WeakContact.png")
			: (bInteractionScriptRequested ? TEXT("M3_DragonGpuSurface_Interaction.png") : TEXT("M3_DragonGpuSurface_Contact.png")));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogSoftBodyDragonRealtime, Display, TEXT("M3_DRAGON_REALTIME_SCREENSHOT_REQUESTED path=%s"), *Path);
	}
	if (bAutoQuitRequested)
	{
		GetWorldTimerManager().SetTimer(ExitTimerHandle, this, &ThisClass::RequestOwnedProcessExit, 2.0f, false);
	}
}

void ASoftBodyDragonRealtimeActor::RequestOwnedProcessExit()
{
	UE_LOG(LogSoftBodyDragonRealtime, Display, TEXT("M3_DRAGON_REALTIME_AUTO_EXIT"));
	FPlatformMisc::RequestExit(false);
}
