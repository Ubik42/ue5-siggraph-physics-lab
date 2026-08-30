#include "ContactClothExplicitTopologyActor.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContactClothChaosOutputAdapter.h"
#include "ContactClothModalSubspace.h"
#include "ContactClothSkinningAdapter.h"
#include "ContactClothPhysicsAssetAdapter.h"
#include "ClothingSimulationInteractor.h"
#include "ClothingSystemRuntimeTypes.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "InputCoreTypes.h"
#include "PhysicsExperimentComponent.h"
#include "PhysicsResearchTypes.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogContactClothExplicitTopology, Log, All);

namespace
{
void AppendFnv1a(uint64& Hash, const void* Data, SIZE_T NumBytes)
{
	const uint8* Bytes = static_cast<const uint8*>(Data);
	for (SIZE_T Index = 0; Index < NumBytes; ++Index)
	{
		Hash ^= Bytes[Index];
		Hash *= 1099511628211ull;
	}
}

void AppendFloat(uint64& Hash, float Value)
{
	uint32 Bits = 0;
	FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
	AppendFnv1a(Hash, &Bits, sizeof(Bits));
}

void AppendVector(uint64& Hash, const FVector3f& Value)
{
	AppendFloat(Hash, Value.X);
	AppendFloat(Hash, Value.Y);
	AppendFloat(Hash, Value.Z);
}
}

AContactClothExplicitTopologyActor::AContactClothExplicitTopologyActor()
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

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> AssassinMeshAsset(TEXT("/Game/Assassin/Mesh/SKM_Assassin_Skin1.SKM_Assassin_Skin1"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> AssassinIdleAsset(TEXT("/Game/Assassin/Animation/Anim_Assassin_idle1.Anim_Assassin_idle1"));
	AssassinMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("AssassinPoseSource"));
	AssassinMesh->SetupAttachment(Root);
	AssassinMesh->SetSkeletalMesh(AssassinMeshAsset.Object);
	AssassinMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AssassinMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	AssassinMesh->SetAnimation(AssassinIdleAsset.Object);
	AssassinMesh->SetPlayRate(0.55f);
	AssassinMesh->bAllowClothActors = true;
	AssassinMesh->bDisableClothSimulation = false;
	AssassinMesh->bWaitForParallelClothTask = true;
	AssassinMesh->ClothBlendWeight = 1.0f;
	AssassinMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	AssassinMesh->bEnableUpdateRateOptimizations = false;
	ExperimentComponent->AddTickPrerequisiteComponent(AssassinMesh);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	FloorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor"));
	FloorMesh->SetupAttachment(Root);
	FloorMesh->SetStaticMesh(CubeMesh.Object);
	FloorMesh->SetRelativeLocation(FVector(0.0, 0.0, -8.0));
	FloorMesh->SetRelativeScale3D(FVector(7.0, 5.0, 0.08));

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

void AContactClothExplicitTopologyActor::BeginPlay()
{
	Super::BeginPlay();
	bCaptureRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsClothExplicitCapture"));
	bAutoQuitRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsClothExplicitAutoQuit"));
	bReplayRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsClothExplicitReplay"));
	bWeakBarrierFailureProfile = FParse::Param(FCommandLine::Get(), TEXT("PhysicsClothExplicitFailureWeakBarrier"));
	bInteractionScriptRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsClothExplicitInteractionScript"));
	FParse::Value(FCommandLine::Get(), TEXT("PhysicsClothExplicitExpectedInputHash="), ExpectedReplayInputHash);
	ExpectedReplayInputHash.TrimQuotesInline();
	FixedFrameDeltaSeconds = FApp::GetFixedDeltaTime();
	bFixedFrameContractActive = FApp::UseFixedTimeStep()
		&& FMath::IsNearlyEqual(FixedFrameDeltaSeconds, 1.0 / 60.0, 1.e-6);
	if (IConsoleVariable* TimeStepSmoothing = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ChaosCloth.UseTimeStepSmoothing")))
	{
		PreviousChaosTimeStepSmoothing = TimeStepSmoothing->GetInt();
		TimeStepSmoothing->Set(0, ECVF_SetByCode);
		bChaosTimeStepSmoothingOverridden = TimeStepSmoothing->GetInt() == 0;
	}

	Settings.Iterations = 24;
	Settings.Relaxation = 0.9f;
	Settings.FixedDeltaSeconds = 1.0f / 60.0f;
	Settings.GravityCmPerSecondSquared = FVector3f(0.0f, 0.0f, -980.0f);
	Settings.VelocityDamping = 0.995f;
	Settings.ReadbackIntervalSteps = 4;
	Settings.CollisionThicknessCm = 1.0f;
	Settings.bEnableNonDistanceBarrier = true;
	Settings.NdbBaseWeight = 10.0f;
	Settings.NdbActivationMultiplier = 2.0f;
	Settings.NdbMaximumWeight = 1048576.0f;
	Settings.CollisionPrimitiveCapacity = 17;
	Settings.bEnableCompleteCcd = true;
	Settings.CcdSafetyOffsetCm = 0.01f;
	ApplyFailureProfileSettings();

	UAnimSequence* IdleAnimation = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Assassin/Animation/Anim_Assassin_idle1.Anim_Assassin_idle1"));
	if (!IdleAnimation)
	{
		StatusText = TEXT("失败：无法加载 Anim_Assassin_idle1");
		WriteRunReport();
		return;
	}
	AssassinMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	AssassinMesh->PlayAnimation(IdleAnimation, true);
	AssassinMesh->SetPlayRate(0.55f);
	if (bReplayRequested)
	{
		AssassinMesh->SetPosition(0.0f, false);
		AssassinMesh->ForceClothNextUpdateTeleportAndReset();
	}
	AssassinMesh->RefreshBoneTransforms();
	if (!ConfigureChaosBaseline())
	{
		WriteRunReport();
		return;
	}
	if (!InitializeAssassinTopology())
	{
		WriteRunReport();
		return;
	}
	ExperimentComponent->OnExperimentStep.AddDynamic(this, &ThisClass::HandleExperimentStep);
	ExperimentComponent->ResetExperiment();
	ExperimentComponent->StartExperiment();
	StatusText = bWeakBarrierFailureProfile
		? TEXT("故障注入：NDB 基础权重 10 → 0.05")
		: bReplayRequested
		? TEXT("重放：固定 60 Hz 输入序列 + 哈希验证")
		: Settings.bEnableRankOneCollisionHessian
		? TEXT("运行：真实 idle → 12 模态 + Rank-one + NDB + CCD")
		: TEXT("消融：真实 idle → 12 模态 + NDB + CCD（Rank-one 关闭）");
	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		PlayerController->SetViewTarget(this);
	}
	UE_LOG(LogContactClothExplicitTopology, Display, TEXT("M2_EXPLICIT_ASSASSIN_STARTED particles=%d edges=%d fixed=%d dynamic=%d modes=%d rank_one=%s"),
		Topology.RestPositions.Num(), Topology.StructuralEdges.Num(), Topology.FixedVertexCount, Topology.DynamicVertexCount,
		Settings.ModalModeCount, Settings.bEnableRankOneCollisionHessian ? TEXT("true") : TEXT("false"));
}

void AContactClothExplicitTopologyActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GpuSimulation.Shutdown();
	if (bChaosTimeStepSmoothingOverridden && PreviousChaosTimeStepSmoothing != INDEX_NONE)
	{
		if (IConsoleVariable* TimeStepSmoothing = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ChaosCloth.UseTimeStepSmoothing")))
		{
			TimeStepSmoothing->Set(PreviousChaosTimeStepSmoothing, ECVF_SetByCode);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AContactClothExplicitTopologyActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	HandleInteractiveInput();
	AdvanceInteractionScript();
	GpuSimulation.PollReadback();
	if (bAwaitingFinalReadback)
	{
		GpuSimulation.RequestReadback();
	}
	ProcessLatestReadback();
	DrawTopologyDebug();
}

FString AContactClothExplicitTopologyActor::GetDisplayModeText() const
{
	switch (DisplayMode)
	{
	case EContactClothInteractiveDisplayMode::ChaosOnly: return TEXT("Chaos 基线");
	case EContactClothInteractiveDisplayMode::ResearchOnly: return TEXT("研究求解器");
	default: return TEXT("双后端同源步");
	}
}

FString AContactClothExplicitTopologyActor::GetFailureProfileText() const
{
	return bWeakBarrierFailureProfile ? TEXT("弱 NDB：k=0.05（预期穿透失败）") : TEXT("标准：NDB k=10 + CCD");
}

void AContactClothExplicitTopologyActor::ApplyFailureProfileSettings()
{
	Settings.NdbBaseWeight = bWeakBarrierFailureProfile ? 0.05f : 10.0f;
}

void AContactClothExplicitTopologyActor::HandleInteractiveInput()
{
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
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
	if (PlayerController->WasInputKeyJustPressed(EKeys::B)) CycleDisplayBackend();
	if (PlayerController->WasInputKeyJustPressed(EKeys::D)) ToggleDebugDraw();
	if (PlayerController->WasInputKeyJustPressed(EKeys::E)) ExportManualMetrics();
	if (PlayerController->WasInputKeyJustPressed(EKeys::F))
	{
		bWeakBarrierFailureProfile = !bWeakBarrierFailureProfile;
		ApplyFailureProfileSettings();
		++ResetCount;
		ResetInteractiveRun(true);
	}
}

void AContactClothExplicitTopologyActor::TogglePauseResume()
{
	if (!ExperimentComponent || bRunComplete || bAwaitingFinalReadback) return;
	if (ExperimentComponent->IsExperimentRunning())
	{
		ExperimentComponent->PauseExperiment();
		SetDriverPaused(true);
		++DriverFreezeCount;
	}
	else
	{
		SetDriverPaused(false);
		ExperimentComponent->StartExperiment();
		++DriverResumeCount;
	}
	++PauseToggleCount;
}

void AContactClothExplicitTopologyActor::ExecuteSingleStep()
{
	if (!ExperimentComponent || bRunComplete || bAwaitingFinalReadback) return;
	if (bPauseAfterNextStep) return;
	bPauseAfterNextStep = true;
	if (!ExperimentComponent->IsExperimentRunning())
	{
		SetDriverPaused(false);
		ExperimentComponent->StartExperiment();
		++DriverResumeCount;
	}
	++SingleStepCount;
}

void AContactClothExplicitTopologyActor::SetDriverPaused(bool bPaused)
{
	if (!AssassinMesh) return;
	AssassinMesh->bPauseAnims = bPaused;
	if (bPaused)
	{
		AssassinMesh->SuspendClothingSimulation();
	}
	else
	{
		AssassinMesh->ResumeClothingSimulation();
	}
}

void AContactClothExplicitTopologyActor::CycleDisplayBackend()
{
	switch (DisplayMode)
	{
	case EContactClothInteractiveDisplayMode::Both:
		DisplayMode = EContactClothInteractiveDisplayMode::ChaosOnly;
		ExperimentComponent->SolverBackend = EPhysicsSolverBackend::ChaosBaseline;
		break;
	case EContactClothInteractiveDisplayMode::ChaosOnly:
		DisplayMode = EContactClothInteractiveDisplayMode::ResearchOnly;
		ExperimentComponent->SolverBackend = EPhysicsSolverBackend::ResearchSolver;
		break;
	default:
		DisplayMode = EContactClothInteractiveDisplayMode::Both;
		ExperimentComponent->SolverBackend = EPhysicsSolverBackend::ResearchSolver;
		break;
	}
	++BackendSwitchCount;
}

void AContactClothExplicitTopologyActor::ToggleDebugDraw()
{
	bEnableDebugDraw = !bEnableDebugDraw;
	++DebugToggleCount;
}

void AContactClothExplicitTopologyActor::ExportManualMetrics()
{
	FString ExportPath;
	ExperimentComponent->ExportMetrics(TEXT("M2_ExplicitAssassinManual"), EPhysicsMetricExportFormat::Json, ExportPath);
	ExperimentComponent->ExportMetrics(TEXT("M2_ExplicitAssassinManual"), EPhysicsMetricExportFormat::Csv, ExportPath);
}

void AContactClothExplicitTopologyActor::AdvanceInteractionScript()
{
	if (!bInteractionScriptRequested || bRunComplete || bAwaitingFinalReadback || !ExperimentComponent) return;
	const int64 Step = ExperimentComponent->GetExperimentStats().StepIndex;
	if (!bInteractionResetDone && Step >= 30)
	{
		bInteractionResetDone = true;
		++ResetCount;
		ResetInteractiveRun(true);
		return;
	}
	if (InteractionScriptPhase == 0 && Step >= 60)
	{
		TogglePauseResume();
		InteractionScriptPhase = 1;
		return;
	}
	if (InteractionScriptPhase == 1)
	{
		ExecuteSingleStep();
		InteractionScriptPhase = 2;
		return;
	}
	if (InteractionScriptPhase == 2)
	{
		if (bPauseAfterNextStep || ExperimentComponent->IsExperimentRunning()) return;
		TogglePauseResume();
		InteractionScriptPhase = 3;
		return;
	}
	if (InteractionScriptPhase == 3 && Step >= 120)
	{
		CycleDisplayBackend();
		InteractionScriptPhase = 4;
	}
	else if (InteractionScriptPhase == 4 && Step >= 150)
	{
		ToggleDebugDraw();
		InteractionScriptPhase = 5;
	}
	else if (InteractionScriptPhase == 5 && Step >= 180)
	{
		ToggleDebugDraw();
		InteractionScriptPhase = 6;
	}
	else if (InteractionScriptPhase == 6 && Step >= 210)
	{
		CycleDisplayBackend();
		InteractionScriptPhase = 7;
	}
	else if (InteractionScriptPhase == 7 && Step >= 240)
	{
		CycleDisplayBackend();
		InteractionScriptPhase = 8;
	}
}

void AContactClothExplicitTopologyActor::ResetRunMeasurements()
{
	InitialTargets.Reset();
	LatestSubmittedTargets.Reset();
	LatestPositions.Reset();
	CollisionSpheres.Reset();
	CollisionCapsules.Reset();
	LatestChaosPositions.Reset();
	PendingComparisonInputs.Reset();
	ComparisonSamples.Reset();
	LatestReadbackStep = 0;
	AnimationTargetMotionCm = 0.0;
	FixedTargetErrorCm = 0.0;
	AverageDynamicDisplacementCm = 0.0;
	MaximumEdgeErrorCm = 0.0;
	MaximumCollisionPenetrationCm = 0.0;
	LatestBackendMeanDistanceCm = 0.0;
	MaximumBackendDistanceCm = 0.0;
	LatestChaosPenetrationCm = 0.0;
	ChaosSnapshotCount = 0;
	LatestComparedStep = 0;
	ReplayInputHash = 1469598103934665603ull;
	bReplayHashMatched = true;
	bExpectedFailureObserved = false;
	bPauseAfterNextStep = false;
	bAwaitingFinalReadback = false;
	bRunComplete = false;
	bRunPassed = false;
}

bool AContactClothExplicitTopologyActor::ResetInteractiveRun(bool bStartAfterReset)
{
	ExperimentComponent->PauseExperiment();
	GpuSimulation.Shutdown();
	ResetRunMeasurements();
	ApplyFailureProfileSettings();
	if (AssassinMesh)
	{
		AssassinMesh->SetPosition(0.0f, false);
		AssassinMesh->ForceClothNextUpdateTeleportAndReset();
		AssassinMesh->RefreshBoneTransforms();
	}
	if (!ConfigureChaosBaseline() || !InitializeAssassinTopology())
	{
		WriteRunReport();
		return false;
	}
	ExperimentComponent->ResetExperiment();
	SetDriverPaused(!bStartAfterReset);
	if (bStartAfterReset) ExperimentComponent->StartExperiment();
	StatusText = bWeakBarrierFailureProfile
		? TEXT("故障注入：弱 NDB 已重置，等待穿透失败")
		: TEXT("已重置：标准接触配置运行中");
	return true;
}

bool AContactClothExplicitTopologyActor::InitializeAssassinTopology()
{
	USkeletalMesh* SkeletalMesh = AssassinMesh ? AssassinMesh->GetSkeletalMeshAsset() : nullptr;
	FString FailureReason;
	if (!SkeletalMesh || !FContactClothChaosTopology::ExtractLod0(*SkeletalMesh, Topology, &FailureReason))
	{
		StatusText = FString::Printf(TEXT("失败：Chaos Clothing 拓扑提取失败：%s"), *FailureReason);
		return false;
	}
	FContactClothSkinnedTargets InitialSkinnedTargets;
	if (!FContactClothSkinningAdapter::SkinComponentPose(*AssassinMesh, Topology, InitialSkinnedTargets, &FailureReason))
	{
		StatusText = FString::Printf(TEXT("失败：初始 UE Clothing 蒙皮失败：%s"), *FailureReason);
		return false;
	}
	InitialTargets = InitialSkinnedTargets.Positions;
	LatestSubmittedTargets = InitialTargets;
	LatestPositions = InitialTargets;
	if (!ConfigureModalSubspace())
	{
		return false;
	}
	if (!GpuSimulation.Initialize(
		InitialTargets,
		Topology.InverseMasses,
		Topology.StructuralEdges,
		Topology.StructuralRestLengthsCm,
		Settings))
	{
		StatusText = TEXT("失败：显式拓扑 GPU 会话初始化被拒绝");
		return false;
	}
	return true;
}

bool AContactClothExplicitTopologyActor::ConfigureModalSubspace()
{
	constexpr int32 ModeCount = 12;
	FContactClothModalBasis Basis;
	FContactClothReducedSystem ReducedSystem;
	FContactClothRankOneProjectionCache ProjectionCache;
	TArray<double> InverseSystem;
	if (!FContactClothModalSubspace::BuildExplicitTopologyBasis(
			Topology.RestPositions, Topology.InverseMasses, Topology.StructuralEdges, ModeCount, Basis)
		|| !FContactClothModalSubspace::BuildReducedExplicitTopologySystem(
			Basis, Topology.InverseMasses, Topology.StructuralEdges, 1.0, 1.0, ReducedSystem)
		|| !FContactClothModalSubspace::InvertReducedSystem(ReducedSystem, InverseSystem)
		|| !FContactClothModalSubspace::BuildRankOneProjectionCache(Basis, ProjectionCache))
	{
		StatusText = TEXT("失败：Assassin 任意拓扑模态/rank-one 数据构建失败");
		return false;
	}
	Settings.ModalModeCount = ModeCount;
	Settings.ModalCorrectionStrength = 1.0f;
	Settings.bEnableRankOneCollisionHessian = !FParse::Param(FCommandLine::Get(), TEXT("PhysicsClothExplicitDisableRankOne"));
	Settings.RankOneMaximumCorrectionCm = 2.0f;
	Settings.ModalBasisValues.Reset();
	Settings.ModalInverseSystemValues.Reset();
	Settings.ModalSystemValues.Reset();
	Settings.ModalParticleOuterProductValues.Reset();
	Settings.ModalBasisValues.Reserve(Basis.Values.Num());
	for (double Value : Basis.Values) Settings.ModalBasisValues.Add(static_cast<float>(Value));
	Settings.ModalInverseSystemValues.Reserve(InverseSystem.Num());
	for (double Value : InverseSystem) Settings.ModalInverseSystemValues.Add(static_cast<float>(Value));
	Settings.ModalSystemValues.Reserve(ReducedSystem.Matrix.Num());
	for (double Value : ReducedSystem.Matrix) Settings.ModalSystemValues.Add(static_cast<float>(Value));
	Settings.ModalParticleOuterProductValues.Reserve(ProjectionCache.Values.Num());
	for (double Value : ProjectionCache.Values) Settings.ModalParticleOuterProductValues.Add(static_cast<float>(Value));
	return true;
}

bool AContactClothExplicitTopologyActor::ConfigureChaosBaseline()
{
	if (!AssassinMesh)
	{
		StatusText = TEXT("失败：缺少 Chaos Cloth 角色组件");
		return false;
	}
	AssassinMesh->bDisableClothSimulation = false;
	AssassinMesh->SetAllowClothActors(true);
	AssassinMesh->RecreateClothingActors();
	AssassinMesh->ForceClothNextUpdateTeleportAndReset();
	if (UClothingSimulationInteractor* Interactor = AssassinMesh->GetClothingSimulationInteractor())
	{
		Interactor->SetNumSubsteps(1);
		Interactor->SetNumIterations(5);
	}
	if (!bFixedFrameContractActive)
	{
		StatusText = FString::Printf(TEXT("失败：Chaos 对照必须以 -UseFixedTimeStep -FPS=60 启动，当前 dt=%.9f"), FixedFrameDeltaSeconds);
		return false;
	}
	if (!bChaosTimeStepSmoothingOverridden)
	{
		StatusText = TEXT("失败：无法关闭 Chaos Cloth 时间步平滑");
		return false;
	}
	return true;
}

void AContactClothExplicitTopologyActor::HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex)
{
	if (!bRunComplete && !bAwaitingFinalReadback)
	{
		if (!UpdateAnimatedInputs(StepIndex) || !GpuSimulation.EnqueueStep())
		{
			ExperimentComponent->PauseExperiment();
			SetDriverPaused(true);
			StatusText = TEXT("失败：实时蒙皮目标或 GPU 固定步被拒绝");
			WriteRunReport();
			return;
		}
		CaptureChaosSnapshot(StepIndex);
	}
	if (bPauseAfterNextStep && StepIndex < StepsToRun)
	{
		bPauseAfterNextStep = false;
		ExperimentComponent->PauseExperiment();
		SetDriverPaused(true);
		++DriverFreezeCount;
		++ManualDriverStepCount;
	}
	if (StepIndex >= StepsToRun && !bAwaitingFinalReadback)
	{
		ExperimentComponent->PauseExperiment();
		SetDriverPaused(true);
		bAwaitingFinalReadback = true;
		StatusText = TEXT("GPU 已排入 300 步，等待最终异步快照");
		GpuSimulation.RequestReadback();
	}
}

bool AContactClothExplicitTopologyActor::UpdateAnimatedInputs(int64 StepIndex)
{
	FContactClothSkinnedTargets Targets;
	FString FailureReason;
	if (!FContactClothSkinningAdapter::SkinComponentPose(*AssassinMesh, Topology, Targets, &FailureReason))
	{
		UE_LOG(LogContactClothExplicitTopology, Error, TEXT("Step %lld skinning failed: %s"), StepIndex, *FailureReason);
		return false;
	}
	LatestSubmittedTargets = MoveTemp(Targets.Positions);
	double StepTargetMotionCm = 0.0;
	for (int32 Index = 0; Index < Topology.InverseMasses.Num(); ++Index)
	{
		if (Topology.InverseMasses[Index] <= 0.0f)
		{
			StepTargetMotionCm = FMath::Max(StepTargetMotionCm, static_cast<double>((LatestSubmittedTargets[Index] - InitialTargets[Index]).Length()));
		}
	}
	AnimationTargetMotionCm = FMath::Max(AnimationTargetMotionCm, StepTargetMotionCm);
	FContactClothPhysicsAssetPrimitives Primitives;
	if (!FContactClothPhysicsAssetAdapter::ExtractComponentSpacePose(*AssassinMesh, Primitives))
	{
		UE_LOG(LogContactClothExplicitTopology, Error, TEXT("Step %lld component-space Physics Asset extraction failed"), StepIndex);
		return false;
	}
	FContactClothPhysicsAssetAdapter::ConvertToGpuPrimitives(Primitives, CollisionSpheres, CollisionCapsules);
	AccumulateReplayInputHash(StepIndex);
	ExperimentComponent->RecordMetric(TEXT("AnimationTargetMotion"), StepTargetMotionCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("CollisionSphereCount"), CollisionSpheres.Num(), TEXT("count"));
	ExperimentComponent->RecordMetric(TEXT("CollisionCapsuleCount"), CollisionCapsules.Num(), TEXT("count"));
	return GpuSimulation.UpdateKinematicTargets(LatestSubmittedTargets)
		&& GpuSimulation.UpdateCollisionPrimitives(CollisionSpheres, CollisionCapsules);
}

void AContactClothExplicitTopologyActor::AccumulateReplayInputHash(int64 StepIndex)
{
	AppendFnv1a(ReplayInputHash, &StepIndex, sizeof(StepIndex));
	const int32 TargetCount = LatestSubmittedTargets.Num();
	const int32 SphereCount = CollisionSpheres.Num();
	const int32 CapsuleCount = CollisionCapsules.Num();
	AppendFnv1a(ReplayInputHash, &TargetCount, sizeof(TargetCount));
	AppendFnv1a(ReplayInputHash, &SphereCount, sizeof(SphereCount));
	AppendFnv1a(ReplayInputHash, &CapsuleCount, sizeof(CapsuleCount));
	for (const FVector3f& Target : LatestSubmittedTargets)
	{
		AppendVector(ReplayInputHash, Target);
	}
	for (const FContactClothGpuSphere& Sphere : CollisionSpheres)
	{
		AppendVector(ReplayInputHash, Sphere.Center);
		AppendFloat(ReplayInputHash, Sphere.RadiusCm);
	}
	for (const FContactClothGpuCapsule& Capsule : CollisionCapsules)
	{
		AppendVector(ReplayInputHash, Capsule.Start);
		AppendVector(ReplayInputHash, Capsule.End);
		AppendFloat(ReplayInputHash, Capsule.RadiusCm);
	}
}

FString AContactClothExplicitTopologyActor::GetReportStem() const
{
	if (bWeakBarrierFailureProfile)
	{
		return TEXT("gpu_explicit_assassin_failure_weak_barrier");
	}
	if (bInteractionScriptRequested)
	{
		return TEXT("gpu_explicit_assassin_interactive");
	}
	if (bReplayRequested)
	{
		return Settings.bEnableRankOneCollisionHessian
			? TEXT("gpu_explicit_assassin_replay")
			: TEXT("gpu_explicit_assassin_replay_rank_one_off");
	}
	return Settings.bEnableRankOneCollisionHessian
		? TEXT("gpu_explicit_assassin")
		: TEXT("gpu_explicit_assassin_rank_one_off");
}

bool AContactClothExplicitTopologyActor::CaptureChaosSnapshot(int64 StepIndex)
{
	if (!AssassinMesh)
	{
		return false;
	}
	AssassinMesh->WaitForExistingParallelClothSimulation_GameThread();
	const TMap<int32, FClothSimulData>& ClothData = AssassinMesh->GetCurrentClothingData_GameThread();
	FContactClothBackendComparisonInput Input;
	FContactClothChaosOutputStats OutputStats;
	FString FailureReason;
	if (!FContactClothChaosOutputAdapter::MergeComponentSpace(
		ClothData, Topology, Input.ChaosPositions, OutputStats, &FailureReason))
	{
		UE_LOG(LogContactClothExplicitTopology, Warning, TEXT("Step %lld Chaos output unavailable: %s"), StepIndex, *FailureReason);
		return false;
	}
	Input.CollisionSpheres = CollisionSpheres;
	Input.CollisionCapsules = CollisionCapsules;
	Input.ChaosMaximumEdgeErrorCm = FContactClothGpuExplicitTopology::MeasureMaximumEdgeErrorCm(
		Input.ChaosPositions, Topology.StructuralEdges, Topology.StructuralRestLengthsCm);
	Input.ChaosMaximumPenetrationCm = FContactClothGpuExplicitTopology::MeasureMaximumCollisionPenetrationCm(
		Input.ChaosPositions, Topology.InverseMasses, Input.CollisionSpheres, Input.CollisionCapsules, Settings.CollisionThicknessCm);
	PendingComparisonInputs.Add(StepIndex, MoveTemp(Input));
	ChaosSnapshotCount++;
	ExperimentComponent->RecordMetric(TEXT("ChaosParticleCount"), OutputStats.VertexCount, TEXT("particles"));
	return true;
}

void AContactClothExplicitTopologyActor::CompareBackendSnapshot(
	TConstArrayView<FVector3f> ResearchPositions,
	int64 StepIndex)
{
	FContactClothBackendComparisonInput* Input = PendingComparisonInputs.Find(StepIndex);
	if (!Input || Input->ChaosPositions.Num() != ResearchPositions.Num()
		|| ResearchPositions.Num() != Topology.RestPositions.Num())
	{
		return;
	}
	FContactClothBackendComparisonSample Sample;
	Sample.Step = StepIndex;
	Sample.ChaosMaximumEdgeErrorCm = Input->ChaosMaximumEdgeErrorCm;
	Sample.ResearchMaximumEdgeErrorCm = FContactClothGpuExplicitTopology::MeasureMaximumEdgeErrorCm(
		ResearchPositions, Topology.StructuralEdges, Topology.StructuralRestLengthsCm);
	Sample.ChaosMaximumPenetrationCm = Input->ChaosMaximumPenetrationCm;
	Sample.ResearchMaximumPenetrationCm = FContactClothGpuExplicitTopology::MeasureMaximumCollisionPenetrationCm(
		ResearchPositions, Topology.InverseMasses, Input->CollisionSpheres, Input->CollisionCapsules, Settings.CollisionThicknessCm);
	double ChaosDisplacementSum = 0.0;
	double ResearchDisplacementSum = 0.0;
	double BackendDistanceSum = 0.0;
	int32 DynamicCount = 0;
	for (int32 Index = 0; Index < ResearchPositions.Num(); ++Index)
	{
		if (Topology.InverseMasses[Index] <= 0.0f)
		{
			continue;
		}
		ChaosDisplacementSum += (Input->ChaosPositions[Index] - InitialTargets[Index]).Length();
		ResearchDisplacementSum += (ResearchPositions[Index] - InitialTargets[Index]).Length();
		const double BackendDistance = (Input->ChaosPositions[Index] - ResearchPositions[Index]).Length();
		BackendDistanceSum += BackendDistance;
		Sample.MaximumBackendDistanceCm = FMath::Max(Sample.MaximumBackendDistanceCm, BackendDistance);
		DynamicCount++;
	}
	if (DynamicCount > 0)
	{
		Sample.ChaosMeanDynamicDisplacementCm = ChaosDisplacementSum / DynamicCount;
		Sample.ResearchMeanDynamicDisplacementCm = ResearchDisplacementSum / DynamicCount;
		Sample.MeanBackendDistanceCm = BackendDistanceSum / DynamicCount;
	}
	LatestChaosPositions = Input->ChaosPositions;
	LatestComparedStep = StepIndex;
	LatestBackendMeanDistanceCm = Sample.MeanBackendDistanceCm;
	MaximumBackendDistanceCm = FMath::Max(MaximumBackendDistanceCm, Sample.MaximumBackendDistanceCm);
	LatestChaosPenetrationCm = Sample.ChaosMaximumPenetrationCm;
	ComparisonSamples.Add(Sample);
	ExperimentComponent->RecordMetric(TEXT("BackendMeanDistance"), Sample.MeanBackendDistanceCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("BackendMaximumDistance"), Sample.MaximumBackendDistanceCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("ChaosMaximumEdgeError"), Sample.ChaosMaximumEdgeErrorCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("ResearchMaximumEdgeError"), Sample.ResearchMaximumEdgeErrorCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("ChaosMaximumPenetration"), Sample.ChaosMaximumPenetrationCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("ResearchMaximumPenetration"), Sample.ResearchMaximumPenetrationCm, TEXT("cm"));
	PendingComparisonInputs.Remove(StepIndex);
}

void AContactClothExplicitTopologyActor::ProcessLatestReadback()
{
	TArray<FVector3f> Positions;
	int64 ReadbackStep = 0;
	if (!GpuSimulation.ConsumeLatestPositions(Positions, ReadbackStep))
	{
		return;
	}
	LatestPositions = MoveTemp(Positions);
	LatestReadbackStep = ReadbackStep;
	CompareBackendSnapshot(LatestPositions, LatestReadbackStep);
	MaximumEdgeErrorCm = FContactClothGpuExplicitTopology::MeasureMaximumEdgeErrorCm(
		LatestPositions, Topology.StructuralEdges, Topology.StructuralRestLengthsCm);
	MaximumCollisionPenetrationCm = FContactClothGpuExplicitTopology::MeasureMaximumCollisionPenetrationCm(
		LatestPositions, Topology.InverseMasses, CollisionSpheres, CollisionCapsules, Settings.CollisionThicknessCm);
	ExperimentComponent->RecordMetric(TEXT("MaximumEdgeError"), MaximumEdgeErrorCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("MaximumCollisionPenetration"), MaximumCollisionPenetrationCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("ReadbackStep"), LatestReadbackStep, TEXT("step"));
	if (bAwaitingFinalReadback && LatestReadbackStep >= StepsToRun)
	{
		CompleteRun();
	}
}

void AContactClothExplicitTopologyActor::DrawTopologyDebug() const
{
	if (!bEnableDebugDraw || !GetWorld() || LatestPositions.Num() != Topology.RestPositions.Num())
	{
		return;
	}
	const FTransform ComponentTransform = AssassinMesh->GetComponentTransform();
	const bool bDrawChaos = DisplayMode != EContactClothInteractiveDisplayMode::ResearchOnly;
	const bool bDrawResearch = DisplayMode != EContactClothInteractiveDisplayMode::ChaosOnly;
	if (bDrawChaos && LatestChaosPositions.Num() == Topology.RestPositions.Num())
	{
		for (int32 EdgeIndex = 0; EdgeIndex < Topology.StructuralEdges.Num(); EdgeIndex += 2)
		{
			const FIntPoint Edge = Topology.StructuralEdges[EdgeIndex];
			DrawDebugLine(GetWorld(),
				ComponentTransform.TransformPosition(FVector(LatestChaosPositions[Edge.X])),
				ComponentTransform.TransformPosition(FVector(LatestChaosPositions[Edge.Y])),
				FColor(70, 255, 120), false, 0.0f, 0, 0.6f);
		}
	}
	if (bDrawResearch)
	{
		for (const FIntPoint& Edge : Topology.StructuralEdges)
		{
			const FVector Start = ComponentTransform.TransformPosition(FVector(LatestPositions[Edge.X]));
			const FVector End = ComponentTransform.TransformPosition(FVector(LatestPositions[Edge.Y]));
			DrawDebugLine(GetWorld(), Start, End, FColor(20, 220, 255), false, 0.0f, 0, 0.7f);
		}
		for (int32 Index = 0; Index < Topology.InverseMasses.Num(); Index += 13)
		{
			if (Topology.InverseMasses[Index] <= 0.0f)
			{
				DrawDebugPoint(GetWorld(), ComponentTransform.TransformPosition(FVector(LatestPositions[Index])), 4.5f, FColor::Red, false, 0.0f);
			}
		}
	}
	for (const FContactClothGpuSphere& Sphere : CollisionSpheres)
	{
		DrawDebugSphere(GetWorld(), ComponentTransform.TransformPosition(FVector(Sphere.Center)), Sphere.RadiusCm, 12, FColor(255, 110, 30), false, 0.0f, 0, 0.7f);
	}
	for (const FContactClothGpuCapsule& Capsule : CollisionCapsules)
	{
		const FVector Start = ComponentTransform.TransformPosition(FVector(Capsule.Start));
		const FVector End = ComponentTransform.TransformPosition(FVector(Capsule.End));
		const FVector Segment = End - Start;
		const FQuat Rotation = Segment.IsNearlyZero() ? FQuat::Identity : FRotationMatrix::MakeFromZ(Segment).ToQuat();
		DrawDebugCapsule(GetWorld(), (Start + End) * 0.5, Segment.Length() * 0.5 + Capsule.RadiusCm,
			Capsule.RadiusCm, Rotation, FColor(255, 145, 30), false, 0.0f, 0, 0.7f);
	}
}

void AContactClothExplicitTopologyActor::CompleteRun()
{
	bAwaitingFinalReadback = false;
	bRunComplete = true;
	FixedTargetErrorCm = 0.0;
	double DynamicDisplacementSum = 0.0;
	int32 DynamicCount = 0;
	for (int32 Index = 0; Index < Topology.InverseMasses.Num(); ++Index)
	{
		if (Topology.InverseMasses[Index] <= 0.0f)
		{
			FixedTargetErrorCm = FMath::Max(FixedTargetErrorCm, static_cast<double>((LatestPositions[Index] - LatestSubmittedTargets[Index]).Length()));
		}
		else
		{
			DynamicDisplacementSum += (LatestPositions[Index] - InitialTargets[Index]).Length();
			DynamicCount++;
		}
	}
	AverageDynamicDisplacementCm = DynamicCount > 0 ? DynamicDisplacementSum / DynamicCount : 0.0;
	const FContactClothGpuExplicitTopologyRealtimeStats Stats = GpuSimulation.GetStats();
	const bool bComparisonMetricsFinite = ComparisonSamples.Num() > 0
		&& FMath::IsFinite(LatestBackendMeanDistanceCm)
		&& FMath::IsFinite(MaximumBackendDistanceCm)
		&& FMath::IsFinite(LatestChaosPenetrationCm);
	const FString ReplayInputHashText = GetReplayInputHashText();
	bReplayHashMatched = ExpectedReplayInputHash.IsEmpty()
		|| ReplayInputHashText.Equals(ExpectedReplayInputHash, ESearchCase::IgnoreCase);
	const bool bInfrastructurePassed = bFixedFrameContractActive
		&& bChaosTimeStepSmoothingOverridden
		&& LatestReadbackStep >= StepsToRun
		&& Stats.StepsExecuted == StepsToRun
		&& Stats.KinematicTargetUpdatesAccepted == StepsToRun
		&& Stats.KinematicTargetApplicationsExecuted == StepsToRun
		&& Stats.LatestKinematicTargetVersion == StepsToRun
		&& Stats.CollisionPrimitiveUpdatesAccepted == StepsToRun
		&& Stats.CollisionPrimitiveApplicationsExecuted == StepsToRun
		&& Stats.LatestCollisionPrimitiveVersion == StepsToRun
		&& Stats.ModalCorrectionsExecuted == StepsToRun
		&& Stats.RankOneCollisionSystemsExecuted == (Settings.bEnableRankOneCollisionHessian ? StepsToRun : 0)
		&& Stats.CompleteCcdPassesExecuted == StepsToRun
		&& Stats.NdbStateUpdatesExecuted == StepsToRun
		&& Stats.bGpuTimingSupported
		&& Stats.GpuTimingSamplesCompleted >= 30
		&& FMath::IsFinite(Stats.MeanGpuStepMilliseconds) && Stats.MeanGpuStepMilliseconds > 0.0
		&& FMath::IsFinite(Stats.P95GpuStepMilliseconds) && Stats.P95GpuStepMilliseconds > 0.0
		&& FMath::IsFinite(Stats.P99GpuStepMilliseconds) && Stats.P99GpuStepMilliseconds > 0.0
		&& Stats.SteadyStateGpuTimingSamples >= 250
		&& FMath::IsFinite(Stats.MeanSteadyStateGpuStepMilliseconds) && Stats.MeanSteadyStateGpuStepMilliseconds > 0.0
		&& FMath::IsFinite(Stats.P95SteadyStateGpuStepMilliseconds) && Stats.P95SteadyStateGpuStepMilliseconds > 0.0
		&& FMath::IsFinite(Stats.P99SteadyStateGpuStepMilliseconds) && Stats.P99SteadyStateGpuStepMilliseconds > 0.0
		&& CollisionSpheres.Num() == 1
		&& CollisionCapsules.Num() == 16
		&& FixedTargetErrorCm < 1.e-3
		&& AnimationTargetMotionCm > 0.01
		&& AverageDynamicDisplacementCm > 0.01
		&& FMath::IsFinite(MaximumEdgeErrorCm)
		&& MaximumEdgeErrorCm < 20.0
		&& FMath::IsFinite(MaximumCollisionPenetrationCm)
		&& ChaosSnapshotCount >= 250
		&& ComparisonSamples.Num() >= 50
		&& LatestComparedStep >= StepsToRun
		&& bComparisonMetricsFinite
		&& (!bReplayRequested || bReplayHashMatched);
	const bool bInteractionContractPassed = !bInteractionScriptRequested
		|| (ResetCount == 1 && PauseToggleCount == 2 && SingleStepCount == 1
			&& BackendSwitchCount == 3 && DebugToggleCount == 2
			&& DriverFreezeCount == 2 && DriverResumeCount == 2 && ManualDriverStepCount == 1
			&& DisplayMode == EContactClothInteractiveDisplayMode::Both && bEnableDebugDraw);
	bExpectedFailureObserved = bWeakBarrierFailureProfile
		&& bInfrastructurePassed
		&& MaximumCollisionPenetrationCm >= 1.e-3;
	bRunPassed = bInteractionContractPassed
		&& (bWeakBarrierFailureProfile
			? bExpectedFailureObserved
			: bInfrastructurePassed && MaximumCollisionPenetrationCm < 1.e-3);
	StatusText = bRunPassed
		? (bWeakBarrierFailureProfile
			? FString::Printf(TEXT("通过：预期弱障碍穿透已复现 %.6f cm"), MaximumCollisionPenetrationCm)
			: bInteractionScriptRequested
			? TEXT("通过：暂停/单步/重置/后端/调试黄金路径完成")
			: bReplayRequested
			? FString::Printf(TEXT("通过：重放输入哈希 %s"), *ReplayInputHashText)
			: TEXT("通过：同组件 Chaos / 研究求解器固定步时间序列已配对"))
		: (bReplayRequested && !bReplayHashMatched
			? FString::Printf(TEXT("失败：重放输入哈希 %s 与期望不一致"), *ReplayInputHashText)
			: TEXT("失败：检查固定帧合同、Chaos 快照、配对样本或显式拓扑门槛"));
	ExperimentComponent->RecordMetric(TEXT("FixedTargetError"), FixedTargetErrorCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("AverageDynamicDisplacement"), AverageDynamicDisplacementCm, TEXT("cm"));
	ExperimentComponent->RecordMetric(TEXT("MaximumCollisionPenetration"), MaximumCollisionPenetrationCm, TEXT("cm"));
	FString ExportPath;
	ExperimentComponent->ExportMetrics(TEXT("M2_ExplicitAssassinCloth"), EPhysicsMetricExportFormat::Json, ExportPath);
	ExperimentComponent->ExportMetrics(TEXT("M2_ExplicitAssassinCloth"), EPhysicsMetricExportFormat::Csv, ExportPath);
	WriteComparisonTimeSeries();
	WriteGpuTimingSeries();
	WriteRunReport();
	UE_LOG(LogContactClothExplicitTopology, Display,
		TEXT("M2_EXPLICIT_ASSASSIN_COMPLETE passed=%s steps=%lld modal_passes=%lld rank_one_systems=%lld target_updates=%lld target_applications=%lld target_version=%lld collision_updates=%lld collision_applications=%lld collision_version=%lld ndb_updates=%lld active_pairs=%d max_activation=%d ccd_passes=%lld gpu_samples=%lld gpu_skipped=%lld gpu_mean_ms=%.6f gpu_p95_ms=%.6f gpu_p99_ms=%.6f gpu_max_ms=%.6f gpu_steady_samples=%lld gpu_steady_mean_ms=%.6f gpu_steady_p95_ms=%.6f gpu_steady_p99_ms=%.6f gpu_steady_max_ms=%.6f spheres=%d capsules=%d latest_step=%lld chaos_snapshots=%lld comparison_samples=%d latest_compared_step=%lld backend_mean_cm=%.6f backend_max_cm=%.6f target_motion_cm=%.6f fixed_error_cm=%.9f dynamic_displacement_cm=%.6f max_edge_error_cm=%.6f max_penetration_cm=%.9f"),
		bRunPassed ? TEXT("true") : TEXT("false"), Stats.StepsExecuted, Stats.ModalCorrectionsExecuted,
		Stats.RankOneCollisionSystemsExecuted, Stats.KinematicTargetUpdatesAccepted,
		Stats.KinematicTargetApplicationsExecuted, Stats.LatestKinematicTargetVersion, Stats.CollisionPrimitiveUpdatesAccepted,
		Stats.CollisionPrimitiveApplicationsExecuted, Stats.LatestCollisionPrimitiveVersion, Stats.NdbStateUpdatesExecuted,
		Stats.LatestActiveContactPairs, Stats.MaximumNdbActivationCount, Stats.CompleteCcdPassesExecuted,
		Stats.GpuTimingSamplesCompleted, Stats.GpuTimingSamplesSkipped, Stats.MeanGpuStepMilliseconds,
		Stats.P95GpuStepMilliseconds, Stats.P99GpuStepMilliseconds, Stats.MaximumGpuStepMilliseconds,
		Stats.SteadyStateGpuTimingSamples, Stats.MeanSteadyStateGpuStepMilliseconds,
		Stats.P95SteadyStateGpuStepMilliseconds, Stats.P99SteadyStateGpuStepMilliseconds,
		Stats.MaximumSteadyStateGpuStepMilliseconds,
		CollisionSpheres.Num(), CollisionCapsules.Num(),
		LatestReadbackStep, ChaosSnapshotCount, ComparisonSamples.Num(), LatestComparedStep, LatestBackendMeanDistanceCm,
		MaximumBackendDistanceCm, AnimationTargetMotionCm, FixedTargetErrorCm, AverageDynamicDisplacementCm,
		MaximumEdgeErrorCm, MaximumCollisionPenetrationCm);
	if (bCaptureRequested || bAutoQuitRequested)
	{
		GetWorldTimerManager().SetTimer(CaptureTimerHandle, this, &ThisClass::CaptureScreenshotAndScheduleExit, 0.8f, false);
	}
}

void AContactClothExplicitTopologyActor::WriteRunReport() const
{
	const FContactClothGpuExplicitTopologyRealtimeStats Stats = GpuSimulation.GetStats();
	const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M2"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString ReportStem = GetReportStem();
	const FString Path = FPaths::Combine(Directory, ReportStem + TEXT("_runtime.json"));
	const FString ComparisonCsv = FString::Printf(TEXT("Docs/Validation/M2/%s_comparison.csv"), *ReportStem);
	const FString ComparisonJson = FString::Printf(TEXT("Docs/Validation/M2/%s_comparison.json"), *ReportStem);
	const TCHAR* DisplayModeToken = DisplayMode == EContactClothInteractiveDisplayMode::ChaosOnly
		? TEXT("chaos_only")
		: DisplayMode == EContactClothInteractiveDisplayMode::ResearchOnly ? TEXT("research_only") : TEXT("both");
	const FString Json = FString::Printf(
		TEXT("{\n  \"result\": \"%s\",\n  \"same_component\": true,\n  \"pose_source\": \"registered Assassin SkeletalMeshComponent playing Anim_Assassin_idle1\",\n  \"skinning_source\": \"UE ClothingMeshUtils::SkinPhysicsMesh\",\n  \"collision_source\": \"same registered Assassin component-space Physics Asset pose\",\n  \"chaos_output_source\": \"same registered Assassin component GetCurrentClothingData_GameThread\",\n  \"fixed_frame_contract_active\": %s,\n  \"fixed_frame_delta_seconds\": %.9f,\n  \"chaos_timestep_smoothing_disabled\": %s,\n  \"particles\": %d,\n  \"edges\": %d,\n  \"fixed_vertices\": %d,\n  \"dynamic_vertices\": %d,\n  \"steps_executed\": %lld,\n  \"modal_mode_count\": %d,\n  \"modal_corrections\": %lld,\n  \"rank_one_enabled\": %s,\n  \"rank_one_collision_systems\": %lld,\n  \"target_updates\": %lld,\n  \"target_applications\": %lld,\n  \"latest_target_version\": %lld,\n  \"collision_updates\": %lld,\n  \"collision_applications\": %lld,\n  \"latest_collision_version\": %lld,\n  \"ndb_state_updates\": %lld,\n  \"latest_active_contact_pairs\": %d,\n  \"maximum_active_contact_pairs\": %d,\n  \"latest_maximum_ndb_activation\": %d,\n  \"maximum_ndb_activation\": %d,\n  \"collision_primitive_capacity\": %d,\n  \"complete_ccd_passes\": %lld,\n  \"collision_spheres\": %d,\n  \"collision_capsules\": %d,\n  \"latest_readback_step\": %lld,\n  \"chaos_snapshots\": %lld,\n  \"comparison_samples\": %d,\n  \"latest_compared_step\": %lld,\n  \"latest_backend_mean_distance_cm\": %.9f,\n  \"maximum_backend_distance_cm\": %.9f,\n  \"latest_chaos_maximum_penetration_cm\": %.12f,\n  \"animation_target_motion_cm\": %.9f,\n  \"fixed_target_error_cm\": %.9f,\n  \"average_dynamic_displacement_cm\": %.9f,\n  \"maximum_edge_error_cm\": %.9f,\n  \"maximum_collision_penetration_cm\": %.12f,\n  \"comparison_csv\": \"%s\",\n  \"comparison_json\": \"%s\",\n  \"scope\": \"Paired by source step; arbitrary-topology modal correction and full 3m-by-3m rank-one analytic contact system run before local NDB/Jacobi and complete CCD; this is not cloth self-collision or moving-collider space-time CCD\"\n}\n"),
		bRunPassed ? TEXT("passed") : TEXT("failed"), bFixedFrameContractActive ? TEXT("true") : TEXT("false"),
		FixedFrameDeltaSeconds, bChaosTimeStepSmoothingOverridden ? TEXT("true") : TEXT("false"),
		Topology.RestPositions.Num(), Topology.StructuralEdges.Num(),
		Topology.FixedVertexCount, Topology.DynamicVertexCount, Stats.StepsExecuted, Settings.ModalModeCount,
		Stats.ModalCorrectionsExecuted, Settings.bEnableRankOneCollisionHessian ? TEXT("true") : TEXT("false"),
		Stats.RankOneCollisionSystemsExecuted, Stats.KinematicTargetUpdatesAccepted,
		Stats.KinematicTargetApplicationsExecuted, Stats.LatestKinematicTargetVersion, Stats.CollisionPrimitiveUpdatesAccepted,
		Stats.CollisionPrimitiveApplicationsExecuted, Stats.LatestCollisionPrimitiveVersion, Stats.NdbStateUpdatesExecuted,
		Stats.LatestActiveContactPairs, Stats.MaximumActiveContactPairs, Stats.LatestMaximumNdbActivationCount,
		Stats.MaximumNdbActivationCount, Settings.CollisionPrimitiveCapacity, Stats.CompleteCcdPassesExecuted,
		CollisionSpheres.Num(), CollisionCapsules.Num(),
		LatestReadbackStep, ChaosSnapshotCount, ComparisonSamples.Num(), LatestComparedStep, LatestBackendMeanDistanceCm,
		MaximumBackendDistanceCm, LatestChaosPenetrationCm, AnimationTargetMotionCm, FixedTargetErrorCm,
		AverageDynamicDisplacementCm, MaximumEdgeErrorCm, MaximumCollisionPenetrationCm, *ComparisonCsv, *ComparisonJson);
	const FString GpuTimingFields = FString::Printf(
		TEXT("  \"failure_profile\": \"%s\",\n  \"expected_failure_observed\": %s,\n  \"ndb_base_weight\": %.6f,\n  \"complete_ccd_enabled\": %s,\n  \"interaction_script\": %s,\n  \"pause_toggle_count\": %d,\n  \"single_step_count\": %d,\n  \"reset_count\": %d,\n  \"backend_switch_count\": %d,\n  \"debug_toggle_count\": %d,\n  \"driver_freeze_count\": %d,\n  \"driver_resume_count\": %d,\n  \"manual_driver_step_count\": %d,\n  \"driver_pause_scope\": \"experiment clock, Assassin animation, and Chaos Cloth suspension\",\n  \"display_mode\": \"%s\",\n  \"debug_draw_enabled\": %s,\n  \"replay_mode\": %s,\n  \"replay_input_hash\": \"%s\",\n  \"replay_expected_input_hash\": \"%s\",\n  \"replay_hash_matched\": %s,\n  \"replay_hash_scope\": \"step-indexed UE Clothing skinned targets and component-space Physics Asset sphere/capsule primitives\",\n  \"gpu_timing_supported\": %s,\n  \"gpu_timing_samples\": %lld,\n  \"gpu_timing_samples_skipped\": %lld,\n  \"gpu_timing_warmup_steps\": %d,\n  \"latest_gpu_timing_step\": %lld,\n  \"latest_gpu_step_ms\": %.9f,\n  \"mean_gpu_step_ms\": %.9f,\n  \"p95_gpu_step_ms\": %.9f,\n  \"p99_gpu_step_ms\": %.9f,\n  \"maximum_gpu_step_ms\": %.9f,\n  \"steady_state_gpu_timing_samples\": %lld,\n  \"mean_steady_state_gpu_step_ms\": %.9f,\n  \"p95_steady_state_gpu_step_ms\": %.9f,\n  \"p99_steady_state_gpu_step_ms\": %.9f,\n  \"maximum_steady_state_gpu_step_ms\": %.9f,\n  \"gpu_timing_csv\": \"Docs/Validation/M2/%s_gpu_timing.csv\",\n  \"gpu_timing_scope\": \"RQT_AbsoluteTime interval around full explicit-step RDG execution; includes uploads, compute passes, and scheduled readback copies\",\n"),
		bWeakBarrierFailureProfile ? TEXT("weak_ndb") : TEXT("none"),
		bExpectedFailureObserved ? TEXT("true") : TEXT("false"), Settings.NdbBaseWeight,
		Settings.bEnableCompleteCcd ? TEXT("true") : TEXT("false"),
		bInteractionScriptRequested ? TEXT("true") : TEXT("false"), PauseToggleCount, SingleStepCount,
		ResetCount, BackendSwitchCount, DebugToggleCount, DriverFreezeCount, DriverResumeCount, ManualDriverStepCount,
		DisplayModeToken, bEnableDebugDraw ? TEXT("true") : TEXT("false"),
		bReplayRequested ? TEXT("true") : TEXT("false"), *GetReplayInputHashText(), *ExpectedReplayInputHash,
		bReplayHashMatched ? TEXT("true") : TEXT("false"),
		Stats.bGpuTimingSupported ? TEXT("true") : TEXT("false"), Stats.GpuTimingSamplesCompleted,
		Stats.GpuTimingSamplesSkipped, Settings.GpuTimingWarmupSteps, Stats.LatestGpuTimingStep, Stats.LatestGpuStepMilliseconds,
		Stats.MeanGpuStepMilliseconds, Stats.P95GpuStepMilliseconds, Stats.P99GpuStepMilliseconds,
		Stats.MaximumGpuStepMilliseconds, Stats.SteadyStateGpuTimingSamples,
		Stats.MeanSteadyStateGpuStepMilliseconds, Stats.P95SteadyStateGpuStepMilliseconds,
		Stats.P99SteadyStateGpuStepMilliseconds, Stats.MaximumSteadyStateGpuStepMilliseconds, *ReportStem);
	const FString FinalJson = Json.Replace(TEXT("  \"scope\":"), *(GpuTimingFields + TEXT("  \"scope\":")));
	FFileHelper::SaveStringToFile(FinalJson, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AContactClothExplicitTopologyActor::WriteComparisonTimeSeries() const
{
	const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M2"));
	const FString ReportStem = GetReportStem();
	IFileManager::Get().MakeDirectory(*Directory, true);
	FString Csv = TEXT("step,chaos_mean_dynamic_displacement_cm,research_mean_dynamic_displacement_cm,mean_backend_distance_cm,maximum_backend_distance_cm,chaos_maximum_edge_error_cm,research_maximum_edge_error_cm,chaos_maximum_penetration_cm,research_maximum_penetration_cm\n");
	FString Json = TEXT("{\n  \"pairing\": \"same SkeletalMeshComponent and source step\",\n  \"fixed_delta_seconds\": 0.016666667,\n  \"samples\": [\n");
	for (int32 Index = 0; Index < ComparisonSamples.Num(); ++Index)
	{
		const FContactClothBackendComparisonSample& Sample = ComparisonSamples[Index];
		Csv += FString::Printf(TEXT("%lld,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.12f,%.12f\n"),
			Sample.Step, Sample.ChaosMeanDynamicDisplacementCm, Sample.ResearchMeanDynamicDisplacementCm,
			Sample.MeanBackendDistanceCm, Sample.MaximumBackendDistanceCm, Sample.ChaosMaximumEdgeErrorCm,
			Sample.ResearchMaximumEdgeErrorCm, Sample.ChaosMaximumPenetrationCm, Sample.ResearchMaximumPenetrationCm);
		Json += FString::Printf(
			TEXT("    {\"step\": %lld, \"chaos_mean_dynamic_displacement_cm\": %.9f, \"research_mean_dynamic_displacement_cm\": %.9f, \"mean_backend_distance_cm\": %.9f, \"maximum_backend_distance_cm\": %.9f, \"chaos_maximum_edge_error_cm\": %.9f, \"research_maximum_edge_error_cm\": %.9f, \"chaos_maximum_penetration_cm\": %.12f, \"research_maximum_penetration_cm\": %.12f}%s\n"),
			Sample.Step, Sample.ChaosMeanDynamicDisplacementCm, Sample.ResearchMeanDynamicDisplacementCm,
			Sample.MeanBackendDistanceCm, Sample.MaximumBackendDistanceCm, Sample.ChaosMaximumEdgeErrorCm,
			Sample.ResearchMaximumEdgeErrorCm, Sample.ChaosMaximumPenetrationCm, Sample.ResearchMaximumPenetrationCm,
			Index + 1 < ComparisonSamples.Num() ? TEXT(",") : TEXT(""));
	}
	Json += TEXT("  ]\n}\n");
	FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(Directory, ReportStem + TEXT("_comparison.csv")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Directory, ReportStem + TEXT("_comparison.json")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AContactClothExplicitTopologyActor::WriteGpuTimingSeries() const
{
	TArray<int64> Steps;
	TArray<double> Milliseconds;
	GpuSimulation.GetGpuTimingSamples(Steps, Milliseconds);
	if (Steps.Num() != Milliseconds.Num()) return;
	const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M2"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString ReportStem = GetReportStem();
	FString Csv = TEXT("step,gpu_step_ms\n");
	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		Csv += FString::Printf(TEXT("%lld,%.9f\n"), Steps[Index], Milliseconds[Index]);
	}
	FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(Directory, ReportStem + TEXT("_gpu_timing.csv")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AContactClothExplicitTopologyActor::CaptureScreenshotAndScheduleExit()
{
	if (bCaptureRequested)
	{
		const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M2/Screenshots"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString ScreenshotName = bWeakBarrierFailureProfile
			? TEXT("M2_ExplicitAssassinFailureWeakBarrier.png")
			: bInteractionScriptRequested
			? TEXT("M2_ExplicitAssassinInteractive.png")
			: bReplayRequested ? TEXT("M2_ExplicitAssassinReplay.png") : TEXT("M2_ExplicitAssassinCloth.png");
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory, ScreenshotName), false, false);
	}
	if (bAutoQuitRequested)
	{
		GetWorldTimerManager().SetTimer(ExitTimerHandle, this, &ThisClass::RequestOwnedProcessExit, 2.0f, false);
	}
}

void AContactClothExplicitTopologyActor::RequestOwnedProcessExit()
{
	FPlatformMisc::RequestExit(false);
}
