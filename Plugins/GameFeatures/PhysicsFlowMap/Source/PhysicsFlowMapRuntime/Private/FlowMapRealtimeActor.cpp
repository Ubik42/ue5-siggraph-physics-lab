#include "FlowMapRealtimeActor.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsExperimentComponent.h"
#include "PhysicsResearchTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "UnrealClient.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlowMapRealtime, Log, All);

namespace FlowMapRealtime
{
FFlowMapGpuClosedLoopInput BuildInput(
	const FFlowMapGridSettings2D& Grid,
	EFlowMapRuntimeBackend RuntimeBackend,
	EFlowMapBudgetProfile BudgetProfile,
	bool bVelocityBudgetAblation)
{
	FFlowMapGpuClosedLoopInput Input;
	const bool bShortMapBaseline=RuntimeBackend==EFlowMapRuntimeBackend::ShortMapBaseline;
	int32 PressureIterations=64;
	if(BudgetProfile==EFlowMapBudgetProfile::Low)PressureIterations=32;
	else if(BudgetProfile==EFlowMapBudgetProfile::High)PressureIterations=96;
	Input.CellCounts=Grid.CellCounts;Input.CellSizeCm=Grid.CellSizeCm;Input.OriginCm=Grid.OriginCm;Input.FixedDeltaSeconds=1.0f/60.0f;Input.PressureIterations=PressureIterations;Input.LongMapResetSteps=bShortMapBaseline?1:(bVelocityBudgetAblation?20:8);Input.ReadbackIntervalSteps=2;Input.GpuTimingWarmupSteps=16;Input.MaximumFaceVelocityCmPerSecond=bVelocityBudgetAblation?1.0e9f:120.0f;Input.bUseGradient=!bShortMapBaseline;
	const int32 Nx=Grid.CellCounts.X,Ny=Grid.CellCounts.Y;const float H=Grid.CellSizeCm,L=Nx*H,K=2.0f*PI/L,A=bVelocityBudgetAblation?80.0f:55.0f;
	auto Velocity=[&](const FVector2f& P){const FVector2f Local=P-Grid.OriginCm;return FVector2f(A*FMath::Sin(K*Local.X)*FMath::Cos(K*Local.Y),-A*FMath::Cos(K*Local.X)*FMath::Sin(K*Local.Y));};
	auto Gradient=[&](const FVector2f& P){const FVector2f Local=P-Grid.OriginCm;const float AK=A*K;return FVector4f(AK*FMath::Cos(K*Local.X)*FMath::Cos(K*Local.Y),-AK*FMath::Sin(K*Local.X)*FMath::Sin(K*Local.Y),AK*FMath::Sin(K*Local.X)*FMath::Sin(K*Local.Y),-AK*FMath::Cos(K*Local.X)*FMath::Cos(K*Local.Y));};
	Input.InitialFaceU.SetNumUninitialized((Nx+1)*Ny);Input.InitialFaceV.SetNumUninitialized(Nx*(Ny+1));
	for(int32 Y=0;Y<Ny;++Y)for(int32 X=0;X<=Nx;++X)Input.InitialFaceU[Y*(Nx+1)+X]=Velocity(Grid.OriginCm+FVector2f(X*H,(Y+0.5f)*H)).X;
	for(int32 Y=0;Y<=Ny;++Y)for(int32 X=0;X<Nx;++X)Input.InitialFaceV[Y*Nx+X]=Velocity(Grid.OriginCm+FVector2f((X+0.5f)*H,Y*H)).Y;
	for(int32 Y=0;Y<Ny;++Y)for(int32 X=0;X<Nx;++X){FFlowMapGpuImpulseParticle P;P.PositionCm=Grid.OriginCm+FVector2f((X+0.5f)*H,(Y+0.5f)*H);P.ReferenceImpulseCmPerSecond=Velocity(P.PositionCm);P.ReferenceImpulseGradientPerSecond=Gradient(P.PositionCm);Input.Particles.Add(P);}
	return Input;
}
}

AFlowMapRealtimeActor::AFlowMapRealtimeActor()
{
	PrimaryActorTick.bCanEverTick=true;
	Root=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));SetRootComponent(Root);
	ExperimentComponent=CreateDefaultSubobject<UPhysicsExperimentComponent>(TEXT("Experiment"));ExperimentComponent->ExperimentCase=EPhysicsResearchCase::FlowMapFluid;ExperimentComponent->SolverBackend=EPhysicsSolverBackend::ResearchSolver;ExperimentComponent->StepConfig.FixedDeltaSeconds=1.0/60.0;ExperimentComponent->StepConfig.MaxCatchUpSteps=4;ExperimentComponent->DeterministicSeed=20260830;ExperimentComponent->MaxMetricSamples=16384;ExperimentComponent->bAutoStart=false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialAsset(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	ChaosObstacle=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LiveChaosObstacle"));ChaosObstacle->SetupAttachment(Root);ChaosObstacle->SetStaticMesh(SphereAsset.Object);ChaosObstacle->SetMaterial(0,MaterialAsset.Object);ChaosObstacle->SetRelativeScale3D(FVector(0.6f));ChaosObstacle->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);ChaosObstacle->SetSimulatePhysics(false);ChaosObstacle->SetEnableGravity(false);ChaosObstacle->SetLinearDamping(0.0f);ChaosObstacle->SetAngularDamping(0.0f);
	ParticleInstances=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("FlowMapParticles"));ParticleInstances->SetupAttachment(Root);ParticleInstances->SetStaticMesh(SphereAsset.Object);ParticleInstances->SetMaterial(0,MaterialAsset.Object);ParticleInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);ParticleInstances->SetCastShadow(false);
	VelocityInstances=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GpuMacVelocityArrows"));VelocityInstances->SetupAttachment(Root);VelocityInstances->SetStaticMesh(CubeAsset.Object);VelocityInstances->SetMaterial(0,MaterialAsset.Object);VelocityInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);VelocityInstances->SetCastShadow(false);
	NegativePressureInstances=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("NegativePressureCells"));NegativePressureInstances->SetupAttachment(Root);NegativePressureInstances->SetStaticMesh(CubeAsset.Object);NegativePressureInstances->SetMaterial(0,MaterialAsset.Object);NegativePressureInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);NegativePressureInstances->SetCastShadow(false);
	NeutralPressureInstances=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("NeutralPressureCells"));NeutralPressureInstances->SetupAttachment(Root);NeutralPressureInstances->SetStaticMesh(CubeAsset.Object);NeutralPressureInstances->SetMaterial(0,MaterialAsset.Object);NeutralPressureInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);NeutralPressureInstances->SetCastShadow(false);
	PositivePressureInstances=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PositivePressureCells"));PositivePressureInstances->SetupAttachment(Root);PositivePressureInstances->SetStaticMesh(CubeAsset.Object);PositivePressureInstances->SetMaterial(0,MaterialAsset.Object);PositivePressureInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);PositivePressureInstances->SetCastShadow(false);
	DomainPlane=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DomainPlane"));DomainPlane->SetupAttachment(Root);DomainPlane->SetStaticMesh(CubeAsset.Object);DomainPlane->SetMaterial(0,MaterialAsset.Object);DomainPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);DomainPlane->SetRelativeLocation(FVector(0,0,-8));DomainPlane->SetRelativeScale3D(FVector(2.4f,2.4f,0.04f));
	Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));Camera->SetupAttachment(Root);Camera->SetRelativeLocation(FVector(390,-450,430));Camera->SetRelativeRotation(FRotator(-36,131,0));Camera->FieldOfView=50.0f;Camera->bAutoActivate=true;
	DirectionalLight=CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("KeyLight"));DirectionalLight->SetupAttachment(Root);DirectionalLight->SetRelativeRotation(FRotator(-50,-25,0));DirectionalLight->SetIntensity(5.0f);
	PointLight=CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));PointLight->SetupAttachment(Root);PointLight->SetRelativeLocation(FVector(-80,-120,240));PointLight->SetIntensity(5000.0f);PointLight->SetAttenuationRadius(900.0f);PointLight->SetLightColor(FLinearColor(0.05f,0.4f,1.0f));
	GridSettings.CellCounts=FIntPoint(24,24);GridSettings.CellSizeCm=10.0f;GridSettings.OriginCm=FVector2f(-120,-120);
}

void AFlowMapRealtimeActor::BeginPlay()
{
	Super::BeginPlay();bVelocityBudgetAblationRequested=FParse::Param(FCommandLine::Get(),TEXT("PhysicsResearchFlowMapVelocityBudgetAblation"));RuntimeBackend=FParse::Param(FCommandLine::Get(),TEXT("PhysicsResearchFlowMapShortMap"))?EFlowMapRuntimeBackend::ShortMapBaseline:EFlowMapRuntimeBackend::ParticleFlowMapResearch;FString BudgetArgument;if(FParse::Value(FCommandLine::Get(),TEXT("PhysicsResearchFlowMapBudget="),BudgetArgument)){if(BudgetArgument.Equals(TEXT("Low"),ESearchCase::IgnoreCase))BudgetProfile=EFlowMapBudgetProfile::Low;else if(BudgetArgument.Equals(TEXT("High"),ESearchCase::IgnoreCase))BudgetProfile=EFlowMapBudgetProfile::High;else BudgetProfile=EFlowMapBudgetProfile::Medium;}if(bVelocityBudgetAblationRequested){RuntimeBackend=EFlowMapRuntimeBackend::ParticleFlowMapResearch;BudgetProfile=EFlowMapBudgetProfile::Medium;}ConfigureRuntimeProfile();SetupVisualMaterials();FString FailureReason;if(!InitializeFlowMap(FailureReason)){StatusText=FString::Printf(TEXT("初始化失败：%s"),*FailureReason);UE_LOG(LogFlowMapRealtime,Error,TEXT("M4_FLOWMAP_REALTIME_INIT_FAILED reason=%s"),*FailureReason);return;}
	ChaosObstacle->SetWorldLocation(GetActorLocation()+FVector(-60,0,12));ChaosObstacle->SetSimulatePhysics(true);ChaosObstacle->SetEnableGravity(false);ChaosObstacle->SetPhysicsLinearVelocity(FVector(ObstacleDriveSpeedCmPerSecond,0,0));ChaosObstacle->SetPhysicsAngularVelocityInRadians(FVector(0,0,0.8f));PreviousChaosTransform=ChaosObstacle->GetComponentTransform();
	ExperimentComponent->OnExperimentStep.AddDynamic(this,&ThisClass::HandleExperimentStep);ExperimentComponent->ResetExperiment();ExperimentComponent->StartExperiment();bInteractionScriptRequested=FParse::Param(FCommandLine::Get(),TEXT("PhysicsResearchFlowMapInteractionScript"));bBackendBudgetScriptRequested=FParse::Param(FCommandLine::Get(),TEXT("PhysicsResearchFlowMapBackendBudgetScript"));bLongSequenceCaptureRequested=FParse::Param(FCommandLine::Get(),TEXT("PhysicsResearchFlowMapLongSequence"));FParse::Value(FCommandLine::Get(),TEXT("PhysicsResearchFlowMapAutoExitSteps="),AutoExitSteps);FParse::Value(FCommandLine::Get(),TEXT("PhysicsResearchFlowMapCaptureStep="),PortfolioCaptureStep);PortfolioCaptureStep=FMath::Max(PortfolioCaptureStep,300);bPortfolioCaptureRequested=FParse::Param(FCommandLine::Get(),TEXT("PhysicsResearchFlowMapCapture"));bInitialized=true;StatusText=bVelocityBudgetAblationRequested?TEXT("失败消融：关闭作品速度预算 / 延长 Map Reset"):FString::Printf(TEXT("运行：%s · %s · ChaosRigidBoundary"),*GetRuntimeBackendDisplayName(),*GetBudgetProfileDisplayName());UE_LOG(LogFlowMapRealtime,Display,TEXT("M4_FLOWMAP_REALTIME_STARTED particles=%d arrows=%d cells=%dx%d cell_size_cm=%.6f pressure_iterations=%d map_reset_steps=%d gradient=%s obstacle_radius_cm=%.3f interaction_script=%s backend_budget_script=%s long_sequence=%s auto_exit_steps=%d capture=%s capture_step=%d velocity_budget_ablation=%s backend=%s budget=%s"),GetParticleInstanceCount(),GetVelocityArrowCount(),GridSettings.CellCounts.X,GridSettings.CellCounts.Y,GridSettings.CellSizeCm,RuntimePressureIterations,RuntimeMapResetSteps,bRuntimeUseGradient?TEXT("true"):TEXT("false"),ObstacleRadiusCm,bInteractionScriptRequested?TEXT("true"):TEXT("false"),bBackendBudgetScriptRequested?TEXT("true"):TEXT("false"),bLongSequenceCaptureRequested?TEXT("true"):TEXT("false"),AutoExitSteps,bPortfolioCaptureRequested?TEXT("true"):TEXT("false"),PortfolioCaptureStep,bVelocityBudgetAblationRequested?TEXT("true"):TEXT("false"),*GetRuntimeBackendDisplayName(),*GetBudgetProfileDisplayName());
}

void AFlowMapRealtimeActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	const FFlowMapGpuClosedLoopStats Stats=GpuSession.GetStats();
	const bool bRuntimeGatePassed=Stats.StepsExecuted>=120&&Stats.SolidBoundaryApplicationsExecuted>=120&&Stats.ReadbacksCompleted>0&&RejectedSnapshots==0&&!bVelocityBudgetAblationRequested&&LatestMaximumVelocityCmPerSecond<=RuntimeMaximumFaceVelocityCmPerSecond+0.1f;
	UE_LOG(LogFlowMapRealtime,Display,TEXT("M4_FLOWMAP_REALTIME_SUMMARY pass=%s steps=%lld boundary_applied=%lld boundary_version=%lld readbacks=%lld snapshots_latest=%lld rejected=%lld timing_samples=%lld steady_mean_ms=%.6f p95_ms=%.6f p99_ms=%.6f map_resets=%lld max_velocity=%.6f velocity_limited_faces=%d pressure_min=%.6f pressure_max=%.6f ablation=%s backend=%s budget=%s backend_toggles=%d budget_toggles=%d backend_budget_phase=%d pause=%d single=%d reset=%d debug=%d export=%d interaction_phase=%d"),bRuntimeGatePassed?TEXT("true"):TEXT("false"),Stats.StepsExecuted,Stats.SolidBoundaryApplicationsExecuted,Stats.LatestSolidBoundaryVersion,Stats.ReadbacksCompleted,LatestSnapshotStep,RejectedSnapshots,Stats.SteadyStateGpuTimingSamples,Stats.MeanSteadyStateGpuStepMilliseconds,Stats.P95SteadyStateGpuStepMilliseconds,Stats.P99SteadyStateGpuStepMilliseconds,Stats.MapResets,LatestMaximumVelocityCmPerSecond,LatestVelocityLimitedFaceCount,LatestMinimumPressurePotential,LatestMaximumPressurePotential,bVelocityBudgetAblationRequested?TEXT("true"):TEXT("false"),*GetRuntimeBackendDisplayName(),*GetBudgetProfileDisplayName(),BackendToggleCount,BudgetToggleCount,BackendBudgetScriptPhase,PauseToggleCount,SingleStepCount,ResetCount,DebugToggleCount,ManualExportCount,InteractionScriptPhase);
	if(bLongSequenceCaptureRequested)ExportLongSequenceEvidence();GpuSession.Shutdown();
	Super::EndPlay(EndPlayReason);
}

void AFlowMapRealtimeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);if(!bInitialized)return;if(!bCameraViewAssigned){if(APlayerController* PlayerController=GetWorld()?GetWorld()->GetFirstPlayerController():nullptr){PlayerController->SetViewTarget(this);bCameraViewAssigned=true;}}HandleInteractiveInput();AdvanceInteractionScript();AdvanceBackendBudgetScript();UpdateObstacleDrive();GpuSession.PollReadback();ProcessLatestSnapshot();DrawFlowDebug();RequestPortfolioScreenshot();if(AutoExitSteps>0&&GpuSession.GetStats().StepsExecuted>=AutoExitSteps)FPlatformMisc::RequestExit(false);
}

int32 AFlowMapRealtimeActor::GetParticleInstanceCount() const{return ParticleInstances?ParticleInstances->GetInstanceCount():0;}
int32 AFlowMapRealtimeActor::GetVelocityArrowCount() const{return VelocityInstances?VelocityInstances->GetInstanceCount():0;}
FString AFlowMapRealtimeActor::GetRuntimeBackendDisplayName() const{return RuntimeBackend==EFlowMapRuntimeBackend::ShortMapBaseline?TEXT("ShortMapBaseline"):TEXT("ParticleFlowMapResearch");}
FString AFlowMapRealtimeActor::GetBudgetProfileDisplayName() const{if(BudgetProfile==EFlowMapBudgetProfile::Low)return TEXT("低预算");if(BudgetProfile==EFlowMapBudgetProfile::High)return TEXT("高预算");return TEXT("中预算");}

bool AFlowMapRealtimeActor::InitializeFlowMap(FString& OutFailureReason)
{
	FFlowMapGpuClosedLoopInput Input=FlowMapRealtime::BuildInput(GridSettings,RuntimeBackend,BudgetProfile,bVelocityBudgetAblationRequested);RuntimeMaximumFaceVelocityCmPerSecond=Input.MaximumFaceVelocityCmPerSecond;RuntimePressureIterations=Input.PressureIterations;RuntimeMapResetSteps=Input.LongMapResetSteps;bRuntimeUseGradient=Input.bUseGradient;ExperimentComponent->SolverBackend=RuntimeBackend==EFlowMapRuntimeBackend::ShortMapBaseline?EPhysicsSolverBackend::NumericalBaseline:EPhysicsSolverBackend::ResearchSolver;if(!GpuSession.Initialize(Input)){OutFailureReason=TEXT("持久 GPU Flow Map 会话拒绝初始化");return false;}
	ParticleInstances->ClearInstances();for(const FFlowMapGpuImpulseParticle& P:Input.Particles)ParticleInstances->AddInstance(FTransform(FQuat::Identity,FVector(P.PositionCm.X,P.PositionCm.Y,10),FVector(0.025f)));VelocityInstances->ClearInstances();for(int32 Y=0;Y<GridSettings.CellCounts.Y;Y+=2)for(int32 X=0;X<GridSettings.CellCounts.X;X+=2)VelocityInstances->AddInstance(FTransform(FQuat::Identity,FVector::ZeroVector,FVector(0.01f)));UpdateVelocityInstances(Input.InitialFaceU,Input.InitialFaceV);return true;
}

void AFlowMapRealtimeActor::HandleExperimentStep(double FixedDeltaSeconds,int64 StepIndex)
{
	if(!bInitialized)return;FString FailureReason;if(!SubmitChaosBoundary(FixedDeltaSeconds,FailureReason)||!GpuSession.EnqueueStep()){StatusText=FString::Printf(TEXT("固定步失败：%s"),*FailureReason);ExperimentComponent->PauseExperiment();UE_LOG(LogFlowMapRealtime,Error,TEXT("M4_FLOWMAP_REALTIME_STEP_FAILED step=%lld reason=%s"),StepIndex,*FailureReason);return;}ExperimentComponent->RecordMetric(TEXT("BoundaryVersion"),static_cast<double>(BoundaryUpdatesSubmitted),TEXT("version"));ExperimentComponent->RecordMetric(TEXT("RuntimeBackend"),RuntimeBackend==EFlowMapRuntimeBackend::ShortMapBaseline?0.0:1.0,TEXT("Short0_PFM1"));ExperimentComponent->RecordMetric(TEXT("BudgetProfile"),static_cast<double>(static_cast<uint8>(BudgetProfile)),TEXT("Low0_Medium1_High2"));
}

bool AFlowMapRealtimeActor::SubmitChaosBoundary(double FixedDeltaSeconds,FString& OutFailureReason)
{
	const FTransform CurrentWorld=ChaosObstacle->GetComponentTransform();
	FTransform PreviousLocal=PreviousChaosTransform;
	FTransform CurrentLocal=CurrentWorld;
	const FVector ActorLocation=GetActorLocation();
	PreviousLocal.SetLocation(PreviousLocal.GetLocation()-ActorLocation);
	CurrentLocal.SetLocation(CurrentLocal.GetLocation()-ActorLocation);
	FFlowMapKinematicCircle2D Circle;if(!FFlowMapSolidBoundaryRasterizer2D::BuildCircleFromRigidBodyTransforms(PreviousLocal,CurrentLocal,ObstacleRadiusCm,static_cast<float>(FixedDeltaSeconds),Circle,&OutFailureReason))return false;FFlowMapSolidBoundaryRaster2D Raster;if(!FFlowMapSolidBoundaryRasterizer2D::RasterizeCircle(GridSettings,Circle,Raster,&OutFailureReason))return false;FFlowMapGpuSolidBoundaryUpdate Boundary;Boundary.SolidCellMask=MoveTemp(Raster.SolidCellMask);Boundary.SolidFaceUMask=MoveTemp(Raster.SolidFaceUMask);Boundary.SolidFaceVMask=MoveTemp(Raster.SolidFaceVMask);Boundary.SolidFaceVelocityU=MoveTemp(Raster.SolidFaceVelocityU);Boundary.SolidFaceVelocityV=MoveTemp(Raster.SolidFaceVelocityV);FFlowMapGpuSolidBoundaryUpdate::FSolidCircle SolidCircle;SolidCircle.CenterCm=Circle.CenterCm;SolidCircle.RadiusCm=Circle.RadiusCm;SolidCircle.SafetyOffsetCm=0.05f;Boundary.ParticleCollisionCircles.Add(SolidCircle);if(!GpuSession.UpdateSolidBoundary(Boundary)){OutFailureReason=TEXT("GPU 会话拒绝 Chaos 边界版本");return false;}PreviousChaosTransform=CurrentWorld;BoundaryUpdatesSubmitted++;ChaosPoseSamples++;return true;
}

void AFlowMapRealtimeActor::ProcessLatestSnapshot()
{
	FFlowMapGpuClosedLoopSnapshot Snapshot;if(!GpuSession.ConsumeLatestSnapshot(Snapshot))return;
	const FVector2f DomainMaximum=GridSettings.OriginCm+FVector2f(GridSettings.CellCounts)*GridSettings.CellSizeCm;
	for(const FVector2f& P:Snapshot.ParticlePositionsCm)
	{
		if(!FMath::IsFinite(P.X)||!FMath::IsFinite(P.Y)||P.X<GridSettings.OriginCm.X-1.0f||P.Y<GridSettings.OriginCm.Y-1.0f||P.X>DomainMaximum.X+1.0f||P.Y>DomainMaximum.Y+1.0f)
		{
			RejectedSnapshots++;StatusText=TEXT("保护触发：拒绝包含非有限值或越界粒子的 GPU 快照");UE_LOG(LogFlowMapRealtime,Error,TEXT("M4_FLOWMAP_REALTIME_SNAPSHOT_REJECTED step=%lld rejected=%lld particle=(%.9g,%.9g) domain_min=(%.9g,%.9g) domain_max=(%.9g,%.9g)"),Snapshot.StepIndex,RejectedSnapshots,P.X,P.Y,GridSettings.OriginCm.X,GridSettings.OriginCm.Y,DomainMaximum.X,DomainMaximum.Y);return;
		}
	}
	for(float Value:Snapshot.PressurePotential)if(!FMath::IsFinite(Value)){RejectedSnapshots++;StatusText=TEXT("保护触发：拒绝包含非有限压力势的 GPU 快照");UE_LOG(LogFlowMapRealtime,Error,TEXT("M4_FLOWMAP_REALTIME_PRESSURE_REJECTED step=%lld rejected=%lld"),Snapshot.StepIndex,RejectedSnapshots);return;}
	LatestSnapshotStep=Snapshot.StepIndex;for(int32 I=0;I<Snapshot.ParticlePositionsCm.Num()&&I<ParticleInstances->GetInstanceCount();++I){const FVector2f P=Snapshot.ParticlePositionsCm[I];ParticleInstances->UpdateInstanceTransform(I,FTransform(FQuat::Identity,FVector(P.X,P.Y,10),FVector(0.025f)),false,false,true);}ParticleInstances->MarkRenderStateDirty();UpdatePressureHeatmap(Snapshot.PressurePotential);UpdateVelocityInstances(Snapshot.FaceU,Snapshot.FaceV);const FFlowMapGpuClosedLoopStats Stats=GpuSession.GetStats();ExperimentComponent->RecordMetric(TEXT("GpuStepMs"),Stats.LatestGpuStepMilliseconds,TEXT("ms"));ExperimentComponent->RecordMetric(TEXT("SolidCells"),Stats.LatestSolidCellCount,TEXT("cells"));ExperimentComponent->RecordMetric(TEXT("MaxVelocity"),LatestMaximumVelocityCmPerSecond,TEXT("cm/s"));ExperimentComponent->RecordMetric(TEXT("PressureRange"),LatestMaximumPressurePotential-LatestMinimumPressurePotential,TEXT("potential"));
}

void AFlowMapRealtimeActor::SetupVisualMaterials()
{
	auto MakeColor=[this](UPrimitiveComponent* Component,const FLinearColor& Color){if(!Component)return;UMaterialInterface* Base=Component->GetMaterial(0);if(UMaterialInstanceDynamic* Material=UMaterialInstanceDynamic::Create(Base,this)){Material->SetVectorParameterValue(TEXT("Color"),Color);Component->SetMaterial(0,Material);}};
	MakeColor(ChaosObstacle,FLinearColor(1.0f,0.18f,0.04f));MakeColor(ParticleInstances,FLinearColor(0.0f,0.8f,1.0f));MakeColor(VelocityInstances,FLinearColor(0.7f,0.18f,1.0f));MakeColor(NegativePressureInstances,FLinearColor(0.02f,0.16f,1.0f));MakeColor(NeutralPressureInstances,FLinearColor(0.025f,0.07f,0.12f));MakeColor(PositivePressureInstances,FLinearColor(1.0f,0.04f,0.22f));MakeColor(DomainPlane,FLinearColor(0.015f,0.04f,0.09f));
}

void AFlowMapRealtimeActor::UpdateVelocityInstances(const TArray<float>& FaceU,const TArray<float>& FaceV)
{
	const int32 Nx=GridSettings.CellCounts.X,Ny=GridSettings.CellCounts.Y;if(FaceU.Num()!=(Nx+1)*Ny||FaceV.Num()!=Nx*(Ny+1))return;int32 InstanceIndex=0;LatestMaximumVelocityCmPerSecond=0.0f;LatestVelocityLimitedFaceCount=0;for(float Value:FaceU)if(FMath::Abs(Value)>=RuntimeMaximumFaceVelocityCmPerSecond-0.01f)LatestVelocityLimitedFaceCount++;for(float Value:FaceV)if(FMath::Abs(Value)>=RuntimeMaximumFaceVelocityCmPerSecond-0.01f)LatestVelocityLimitedFaceCount++;
	for(int32 Y=0;Y<Ny;Y+=2)for(int32 X=0;X<Nx;X+=2)
	{
		const float U=0.5f*(FaceU[Y*(Nx+1)+X]+FaceU[Y*(Nx+1)+X+1]);const float V=0.5f*(FaceV[Y*Nx+X]+FaceV[(Y+1)*Nx+X]);if(!FMath::IsFinite(U)||!FMath::IsFinite(V))continue;const FVector2f Velocity(U,V);const float Speed=Velocity.Length();LatestMaximumVelocityCmPerSecond=FMath::Max(LatestMaximumVelocityCmPerSecond,Speed);const float Length=FMath::Clamp(Speed*0.10f,1.0f,12.0f);const FVector2f Direction=Speed>1.e-4f?Velocity/Speed:FVector2f(1,0);const FVector2f CellCenter=GridSettings.OriginCm+FVector2f((X+0.5f)*GridSettings.CellSizeCm,(Y+0.5f)*GridSettings.CellSizeCm);const FVector Location(CellCenter.X+Direction.X*Length*0.5f,CellCenter.Y+Direction.Y*Length*0.5f,15.0f);const FRotator Rotation(0.0f,FMath::RadiansToDegrees(FMath::Atan2(Direction.Y,Direction.X)),0.0f);VelocityInstances->UpdateInstanceTransform(InstanceIndex++,FTransform(Rotation,Location,FVector(Length/100.0f,0.009f,0.009f)),false,false,true);
	}
	VelocityInstances->MarkRenderStateDirty();
}

void AFlowMapRealtimeActor::RequestPortfolioScreenshot()
{
	if(!bPortfolioCaptureRequested||bPortfolioCaptureIssued||LatestSnapshotStep<PortfolioCaptureStep)return;const FString Directory=FPaths::Combine(FPaths::ProjectDir(),TEXT("Docs/Validation/M4/Screenshots"));IFileManager::Get().MakeDirectory(*Directory,true);const FString BudgetToken=BudgetProfile==EFlowMapBudgetProfile::Low?TEXT("Low"):(BudgetProfile==EFlowMapBudgetProfile::High?TEXT("High"):TEXT("Medium"));FString FileName;if(bVelocityBudgetAblationRequested)FileName=TEXT("Case03_FlowMap_FailureVelocityBudget.png");else if(RuntimeBackend==EFlowMapRuntimeBackend::ParticleFlowMapResearch&&BudgetProfile==EFlowMapBudgetProfile::Medium)FileName=TEXT("Case03_FlowMap_LiveChaos.png");else FileName=FString::Printf(TEXT("Case03_FlowMap_%s_%s.png"),*GetRuntimeBackendDisplayName(),*BudgetToken);const FString ScreenshotPath=FPaths::Combine(Directory,FileName);FScreenshotRequest::RequestScreenshot(ScreenshotPath,true,false);bPortfolioCaptureIssued=true;UE_LOG(LogFlowMapRealtime,Display,TEXT("M4_FLOWMAP_REALTIME_SCREENSHOT_REQUESTED path=%s snapshot=%lld backend=%s budget=%s ablation=%s"),*ScreenshotPath,LatestSnapshotStep,*GetRuntimeBackendDisplayName(),*BudgetToken,bVelocityBudgetAblationRequested?TEXT("true"):TEXT("false"));
}

void AFlowMapRealtimeActor::UpdatePressureHeatmap(const TArray<float>& PressurePotential)
{
	const int32 Nx=GridSettings.CellCounts.X,Ny=GridSettings.CellCounts.Y;if(PressurePotential.Num()!=Nx*Ny)return;NegativePressureInstances->ClearInstances();NeutralPressureInstances->ClearInstances();PositivePressureInstances->ClearInstances();LatestMinimumPressurePotential=PressurePotential[0];LatestMaximumPressurePotential=PressurePotential[0];for(float Value:PressurePotential){LatestMinimumPressurePotential=FMath::Min(LatestMinimumPressurePotential,Value);LatestMaximumPressurePotential=FMath::Max(LatestMaximumPressurePotential,Value);}const float MaximumAbsolute=FMath::Max3(FMath::Abs(LatestMinimumPressurePotential),FMath::Abs(LatestMaximumPressurePotential),1.e-6f);PositivePressureCellCount=0;NegativePressureCellCount=0;
	for(int32 Y=0;Y<Ny;++Y)for(int32 X=0;X<Nx;++X)
	{
		const float Value=PressurePotential[Y*Nx+X];const float Normalized=FMath::Clamp(FMath::Abs(Value)/MaximumAbsolute,0.0f,1.0f);UInstancedStaticMeshComponent* Target=NeutralPressureInstances;if(Value>0.12f*MaximumAbsolute){Target=PositivePressureInstances;PositivePressureCellCount++;}else if(Value<-0.12f*MaximumAbsolute){Target=NegativePressureInstances;NegativePressureCellCount++;}const FVector2f Center=GridSettings.OriginCm+FVector2f((X+0.5f)*GridSettings.CellSizeCm,(Y+0.5f)*GridSettings.CellSizeCm);const float ThicknessScale=0.006f+0.018f*Normalized;Target->AddInstance(FTransform(FQuat::Identity,FVector(Center.X,Center.Y,-1.0f+ThicknessScale*50.0f),FVector(0.096f,0.096f,ThicknessScale)));
	}
}

void AFlowMapRealtimeActor::UpdateObstacleDrive()
{
	if(!ExperimentComponent->IsExperimentRunning())return;
	const float LocalX=ChaosObstacle->GetComponentLocation().X-GetActorLocation().X;if(LocalX>70.0f&&ObstacleDriveDirection>0)ObstacleDriveDirection=-1;else if(LocalX<-70.0f&&ObstacleDriveDirection<0)ObstacleDriveDirection=1;ChaosObstacle->SetPhysicsLinearVelocity(FVector(ObstacleDriveDirection*ObstacleDriveSpeedCmPerSecond,0,0));
}

void AFlowMapRealtimeActor::HandleInteractiveInput()
{
	APlayerController* PlayerController=GetWorld()?GetWorld()->GetFirstPlayerController():nullptr;if(!PlayerController)return;
	if(PlayerController->WasInputKeyJustPressed(EKeys::SpaceBar))TogglePauseResume();
	if(PlayerController->WasInputKeyJustPressed(EKeys::N))ExecuteSingleStep();
	if(PlayerController->WasInputKeyJustPressed(EKeys::R)){ResetCount++;ResetInteractiveRun();}
	if(PlayerController->WasInputKeyJustPressed(EKeys::B))ToggleRuntimeBackend();
	if(PlayerController->WasInputKeyJustPressed(EKeys::C))CycleBudgetProfile();
	if(PlayerController->WasInputKeyJustPressed(EKeys::D))ToggleDebugDraw();
	if(PlayerController->WasInputKeyJustPressed(EKeys::E))ExportManualMetrics();
}

void AFlowMapRealtimeActor::TogglePauseResume()
{
	if(ExperimentComponent->IsExperimentRunning())
	{
		ExperimentComponent->PauseExperiment();ChaosObstacle->SetPhysicsLinearVelocity(FVector::ZeroVector);ChaosObstacle->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);StatusText=TEXT("已暂停：N 单步 / R 重置 / 空格继续");
	}
	else
	{
		PreviousChaosTransform=ChaosObstacle->GetComponentTransform();ChaosObstacle->SetPhysicsLinearVelocity(FVector(ObstacleDriveDirection*ObstacleDriveSpeedCmPerSecond,0,0));ChaosObstacle->SetPhysicsAngularVelocityInRadians(FVector(0,0,0.8f));ExperimentComponent->StartExperiment();StatusText=FString::Printf(TEXT("运行：%s · %s · ChaosRigidBoundary"),*GetRuntimeBackendDisplayName(),*GetBudgetProfileDisplayName());
	}
	PauseToggleCount++;
}

void AFlowMapRealtimeActor::ExecuteSingleStep()
{
	if(ExperimentComponent->IsExperimentRunning())return;ExperimentComponent->StepExperimentOnce();StatusText=TEXT("已单步：边界版本与 GPU 快照继续使用同一固定步");SingleStepCount++;
}

bool AFlowMapRealtimeActor::ResetInteractiveRun()
{
	ExperimentComponent->PauseExperiment();GpuSession.Shutdown();bInitialized=false;LatestSnapshotStep=0;BoundaryUpdatesSubmitted=0;ChaosPoseSamples=0;RejectedSnapshots=0;ObstacleDriveDirection=1;
	ChaosObstacle->SetSimulatePhysics(false);ChaosObstacle->SetWorldTransform(FTransform(FQuat::Identity,GetActorLocation()+FVector(-60,0,12),FVector(0.6f)));
	FString FailureReason;if(!InitializeFlowMap(FailureReason)){StatusText=FString::Printf(TEXT("重置失败：%s"),*FailureReason);return false;}
	ChaosObstacle->SetSimulatePhysics(true);ChaosObstacle->SetEnableGravity(false);ChaosObstacle->SetPhysicsLinearVelocity(FVector(ObstacleDriveSpeedCmPerSecond,0,0));ChaosObstacle->SetPhysicsAngularVelocityInRadians(FVector(0,0,0.8f));PreviousChaosTransform=ChaosObstacle->GetComponentTransform();ExperimentComponent->ResetExperiment();ExperimentComponent->StartExperiment();bInitialized=true;StatusText=FString::Printf(TEXT("已重建：%s · %s · Chaos 边界与 GPU 状态回到同一初态"),*GetRuntimeBackendDisplayName(),*GetBudgetProfileDisplayName());return true;
}

void AFlowMapRealtimeActor::ToggleDebugDraw(){bDebugDrawEnabled=!bDebugDrawEnabled;DebugToggleCount++;}

void AFlowMapRealtimeActor::ToggleRuntimeBackend()
{
	if(bVelocityBudgetAblationRequested)return;RuntimeBackend=RuntimeBackend==EFlowMapRuntimeBackend::ShortMapBaseline?EFlowMapRuntimeBackend::ParticleFlowMapResearch:EFlowMapRuntimeBackend::ShortMapBaseline;BackendToggleCount++;ResetCount++;ResetInteractiveRun();
}

void AFlowMapRealtimeActor::CycleBudgetProfile()
{
	if(bVelocityBudgetAblationRequested)return;if(BudgetProfile==EFlowMapBudgetProfile::Low)BudgetProfile=EFlowMapBudgetProfile::Medium;else if(BudgetProfile==EFlowMapBudgetProfile::Medium)BudgetProfile=EFlowMapBudgetProfile::High;else BudgetProfile=EFlowMapBudgetProfile::Low;BudgetToggleCount++;ConfigureRuntimeProfile();ResetCount++;ResetInteractiveRun();
}

void AFlowMapRealtimeActor::ConfigureRuntimeProfile()
{
	constexpr float DomainSizeCm=240.0f;int32 Cells=24;if(BudgetProfile==EFlowMapBudgetProfile::Low)Cells=16;else if(BudgetProfile==EFlowMapBudgetProfile::High)Cells=32;GridSettings.CellCounts=FIntPoint(Cells,Cells);GridSettings.CellSizeCm=DomainSizeCm/Cells;GridSettings.OriginCm=FVector2f(-0.5f*DomainSizeCm,-0.5f*DomainSizeCm);if(DomainPlane)DomainPlane->SetRelativeScale3D(FVector(DomainSizeCm/100.0f,DomainSizeCm/100.0f,0.04f));
}

void AFlowMapRealtimeActor::ExportManualMetrics()
{
	const FString Prefix=FString::Printf(TEXT("M4_FlowMap_%s_%s"),*GetRuntimeBackendDisplayName(),*GetBudgetProfileDisplayName());FString ExportPath;ExperimentComponent->ExportMetrics(Prefix,EPhysicsMetricExportFormat::Json,ExportPath);ExperimentComponent->ExportMetrics(Prefix,EPhysicsMetricExportFormat::Csv,ExportPath);ManualExportCount++;StatusText=FString::Printf(TEXT("已导出 %s / %s 固定步、GPU 时间与 Chaos 边界指标"),*GetRuntimeBackendDisplayName(),*GetBudgetProfileDisplayName());
}

void AFlowMapRealtimeActor::ExportLongSequenceEvidence()
{
	TArray<int64> Steps;TArray<double> Milliseconds;GpuSession.GetGpuTimingSamples(Steps,Milliseconds);if(Steps.Num()!=Milliseconds.Num()||Steps.IsEmpty()){UE_LOG(LogFlowMapRealtime,Error,TEXT("M4_FLOWMAP_LONG_SEQUENCE_EXPORT_FAILED reason=missing_or_mismatched_samples steps=%d values=%d"),Steps.Num(),Milliseconds.Num());return;}TArray<TPair<int64,double>> OrderedSamples;OrderedSamples.Reserve(Steps.Num());for(int32 Index=0;Index<Steps.Num();++Index)OrderedSamples.Emplace(Steps[Index],Milliseconds[Index]);OrderedSamples.Sort([](const TPair<int64,double>& A,const TPair<int64,double>& B){return A.Key<B.Key;});
	const FString BudgetToken=BudgetProfile==EFlowMapBudgetProfile::Low?TEXT("Low"):(BudgetProfile==EFlowMapBudgetProfile::High?TEXT("High"):TEXT("Medium"));const FString BaseName=FString::Printf(TEXT("FlowMapLongSequence_%s_%s"),*GetRuntimeBackendDisplayName(),*BudgetToken);const FString Directory=FPaths::Combine(FPaths::ProjectDir(),TEXT("Docs/Validation/M4/LongSequence"));IFileManager::Get().MakeDirectory(*Directory,true);const FString CsvPath=FPaths::Combine(Directory,BaseName+TEXT(".csv"));const FString JsonPath=FPaths::Combine(Directory,BaseName+TEXT(".json"));
	FString Csv=TEXT("step,gpu_step_ms,steady_state\n");for(const TPair<int64,double>& Sample:OrderedSamples)Csv+=FString::Printf(TEXT("%lld,%.9f,%s\n"),Sample.Key,Sample.Value,Sample.Key>16?TEXT("true"):TEXT("false"));
	const FFlowMapGpuClosedLoopStats Stats=GpuSession.GetStats();TSharedRef<FJsonObject> RootObject=MakeShared<FJsonObject>();RootObject->SetStringField(TEXT("backend"),GetRuntimeBackendDisplayName());RootObject->SetStringField(TEXT("budget"),BudgetToken);RootObject->SetNumberField(TEXT("cells_x"),GridSettings.CellCounts.X);RootObject->SetNumberField(TEXT("cells_y"),GridSettings.CellCounts.Y);RootObject->SetNumberField(TEXT("physical_domain_cm"),GridSettings.CellCounts.X*GridSettings.CellSizeCm);RootObject->SetNumberField(TEXT("steps_executed"),Stats.StepsExecuted);RootObject->SetNumberField(TEXT("solid_boundary_applications"),Stats.SolidBoundaryApplicationsExecuted);RootObject->SetNumberField(TEXT("map_resets"),Stats.MapResets);RootObject->SetNumberField(TEXT("samples"),OrderedSamples.Num());RootObject->SetNumberField(TEXT("timing_samples_skipped"),Stats.GpuTimingSamplesSkipped);RootObject->SetNumberField(TEXT("steady_samples"),Stats.SteadyStateGpuTimingSamples);RootObject->SetNumberField(TEXT("steady_mean_ms"),Stats.MeanSteadyStateGpuStepMilliseconds);RootObject->SetNumberField(TEXT("steady_p95_ms"),Stats.P95SteadyStateGpuStepMilliseconds);RootObject->SetNumberField(TEXT("steady_p99_ms"),Stats.P99SteadyStateGpuStepMilliseconds);RootObject->SetNumberField(TEXT("steady_max_ms"),Stats.MaximumSteadyStateGpuStepMilliseconds);RootObject->SetNumberField(TEXT("rejected_snapshots"),RejectedSnapshots);RootObject->SetNumberField(TEXT("latest_max_velocity_cm_per_second"),LatestMaximumVelocityCmPerSecond);RootObject->SetNumberField(TEXT("latest_pressure_minimum"),LatestMinimumPressurePotential);RootObject->SetNumberField(TEXT("latest_pressure_maximum"),LatestMaximumPressurePotential);RootObject->SetStringField(TEXT("csv"),FPaths::GetCleanFilename(CsvPath));TArray<TSharedPtr<FJsonValue>> Samples;Samples.Reserve(OrderedSamples.Num());for(const TPair<int64,double>& OrderedSample:OrderedSamples){TSharedRef<FJsonObject> Sample=MakeShared<FJsonObject>();Sample->SetNumberField(TEXT("step"),OrderedSample.Key);Sample->SetNumberField(TEXT("gpu_step_ms"),OrderedSample.Value);Samples.Add(MakeShared<FJsonValueObject>(Sample));}RootObject->SetArrayField(TEXT("raw_samples"),Samples);FString Json;TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);FJsonSerializer::Serialize(RootObject,Writer);
	const bool bCsvSaved=FFileHelper::SaveStringToFile(Csv,*CsvPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);const bool bJsonSaved=FFileHelper::SaveStringToFile(Json,*JsonPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);if(bCsvSaved&&bJsonSaved){UE_LOG(LogFlowMapRealtime,Display,TEXT("M4_FLOWMAP_LONG_SEQUENCE_EXPORTED csv=%s json=%s samples=%d steady_samples=%lld saved=true"),*CsvPath,*JsonPath,Steps.Num(),Stats.SteadyStateGpuTimingSamples);}else{UE_LOG(LogFlowMapRealtime,Error,TEXT("M4_FLOWMAP_LONG_SEQUENCE_EXPORTED csv=%s json=%s samples=%d steady_samples=%lld saved=false"),*CsvPath,*JsonPath,Steps.Num(),Stats.SteadyStateGpuTimingSamples);}
}

void AFlowMapRealtimeActor::DrawFlowDebug() const
{
	if(!bDebugDrawEnabled||!GetWorld())return;const FVector Center=ChaosObstacle->GetComponentLocation();DrawDebugCircle(GetWorld(),Center,ObstacleRadiusCm,48,FColor::Cyan,false,0.0f,0,1.5f,FVector::ForwardVector,FVector::RightVector,false);const FVector Velocity=ChaosObstacle->GetPhysicsLinearVelocity();DrawDebugDirectionalArrow(GetWorld(),Center+FVector(0,0,35),Center+FVector(0,0,35)+Velocity,12.0f,FColor::Yellow,false,0.0f,0,2.0f);const FVector Origin=GetActorLocation()+FVector(GridSettings.OriginCm.X,GridSettings.OriginCm.Y,2);const FVector Extent(GridSettings.CellCounts.X*GridSettings.CellSizeCm,GridSettings.CellCounts.Y*GridSettings.CellSizeCm,0);DrawDebugBox(GetWorld(),Origin+Extent*0.5f,FVector(Extent.X*0.5f,Extent.Y*0.5f,1),FColor::Blue,false,0.0f,0,1.0f);
}

void AFlowMapRealtimeActor::AdvanceInteractionScript()
{
	if(!bInteractionScriptRequested||!ExperimentComponent)return;const int64 Step=ExperimentComponent->GetExperimentStats().StepIndex;
	if(InteractionScriptPhase==0&&Step>=60){TogglePauseResume();InteractionScriptPhase=1;}
	else if(InteractionScriptPhase==1){ExecuteSingleStep();InteractionScriptPhase=2;}
	else if(InteractionScriptPhase==2){TogglePauseResume();InteractionScriptPhase=3;}
	else if(InteractionScriptPhase==3&&Step>=90){ResetCount++;ResetInteractiveRun();InteractionScriptPhase=4;}
	else if(InteractionScriptPhase==4&&Step>=30){ToggleDebugDraw();InteractionScriptPhase=5;}
	else if(InteractionScriptPhase==5&&Step>=60){ToggleDebugDraw();ExportManualMetrics();InteractionScriptPhase=6;UE_LOG(LogFlowMapRealtime,Display,TEXT("M4_FLOWMAP_REALTIME_INTERACTION_COMPLETE pause=%d single=%d reset=%d debug=%d export=%d"),PauseToggleCount,SingleStepCount,ResetCount,DebugToggleCount,ManualExportCount);}
}

void AFlowMapRealtimeActor::AdvanceBackendBudgetScript()
{
	if(!bBackendBudgetScriptRequested||!ExperimentComponent)return;const int64 Step=ExperimentComponent->GetExperimentStats().StepIndex;
	if(BackendBudgetScriptPhase==0&&Step>=60){ToggleRuntimeBackend();BackendBudgetScriptPhase=1;}
	else if(BackendBudgetScriptPhase==1&&Step>=60){CycleBudgetProfile();BackendBudgetScriptPhase=2;}
	else if(BackendBudgetScriptPhase==2&&Step>=60){ToggleRuntimeBackend();BackendBudgetScriptPhase=3;}
	else if(BackendBudgetScriptPhase==3&&Step>=120){BackendBudgetScriptPhase=4;UE_LOG(LogFlowMapRealtime,Display,TEXT("M4_FLOWMAP_BACKEND_BUDGET_INTERACTION_COMPLETE backend_toggles=%d budget_toggles=%d final_backend=%s final_budget=%s"),BackendToggleCount,BudgetToggleCount,*GetRuntimeBackendDisplayName(),*GetBudgetProfileDisplayName());FPlatformMisc::RequestExit(false);}
}
