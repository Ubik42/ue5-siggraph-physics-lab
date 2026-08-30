#include "InverseSpringRealtimeActor.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "InputCoreTypes.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "PhysicsExperimentComponent.h"
#include "PhysicsResearchTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogInverseSpringRealtime, Log, All);

AInverseSpringRealtimeActor::AInverseSpringRealtimeActor()
{
    PrimaryActorTick.bCanEverTick = true;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
    ExperimentComponent = CreateDefaultSubobject<UPhysicsExperimentComponent>(TEXT("Experiment"));
    ExperimentComponent->ExperimentCase = EPhysicsResearchCase::DifferentiableInverseDesign;
    ExperimentComponent->SolverBackend = EPhysicsSolverBackend::ResearchSolver;
    ExperimentComponent->StepConfig.FixedDeltaSeconds = 1.0 / 60.0;
    ExperimentComponent->StepConfig.MaxCatchUpSteps = 2;
    ExperimentComponent->DeterministicSeed = 20260830;
    ExperimentComponent->MaxMetricSamples = 4096;
    ExperimentComponent->bAutoStart = false;

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialAsset(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    auto CreateBody = [&](const TCHAR* Name, const FVector& RelativeLocation, const FVector& Scale)
    {
        UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Component->SetupAttachment(Root);
        Component->SetStaticMesh(SphereAsset.Object);
        Component->SetMaterial(0, MaterialAsset.Object);
        Component->SetRelativeLocation(RelativeLocation);
        Component->SetRelativeScale3D(Scale);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        return Component;
    };
    TargetBody = CreateBody(TEXT("TargetTrajectory"), FVector(0.0, -90.0, 34.0), FVector(0.22));
    InitialBody = CreateBody(TEXT("InitialTrajectory"), FVector(0.0, -30.0, 34.0), FVector(0.22));
    ResearchBody = CreateBody(TEXT("OptimizedResearchTrajectory"), FVector(0.0, 30.0, 34.0), FVector(0.22));
    ChaosBody = CreateBody(TEXT("ChaosConstraintReplay"), FVector(0.0, 90.0, 34.0), FVector(0.25));
    ChaosBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    ChaosBody->SetSimulatePhysics(false);
    ChaosBody->SetEnableGravity(false);
    ChaosBody->SetLinearDamping(0.0f);
    ChaosBody->SetAngularDamping(0.0f);

    ChaosAnchor = CreateBody(TEXT("ChaosWorldAnchor"), FVector(0.0, 90.0, 34.0), FVector(0.08));
    ChaosAnchor->SetVisibility(false);
    TargetGate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TargetGate"));
    TargetGate->SetupAttachment(Root);
    TargetGate->SetStaticMesh(CubeAsset.Object);
    TargetGate->SetMaterial(0, MaterialAsset.Object);
    TargetGate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TargetGate->SetRelativeScale3D(FVector(0.025f, 0.04f, 0.8f));
    TrackFloor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TrackFloor"));
    TrackFloor->SetupAttachment(Root);
    TrackFloor->SetStaticMesh(CubeAsset.Object);
    TrackFloor->SetMaterial(0, MaterialAsset.Object);
    TrackFloor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TrackFloor->SetRelativeLocation(FVector(150.0, 0.0, 0.0));
    TrackFloor->SetRelativeScale3D(FVector(3.8f, 2.4f, 0.025f));

    ChaosConstraint = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("ChaosSpringConstraint"));
    ChaosConstraint->SetupAttachment(Root);
    ChaosConstraint->SetRelativeLocation(FVector(0.0, 90.0, 34.0));

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Root);
    Camera->SetRelativeLocation(FVector(460.0, -620.0, 650.0));
    Camera->SetRelativeRotation(FRotator(-42.0, 133.0, 0.0));
    Camera->FieldOfView = 48.0f;
    Camera->bAutoActivate = true;
    DirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("KeyLight"));
    DirectionalLight->SetupAttachment(Root);
    DirectionalLight->SetRelativeRotation(FRotator(-55.0, -30.0, 0.0));
    DirectionalLight->SetIntensity(5.0f);
    PointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
    PointLight->SetupAttachment(Root);
    PointLight->SetRelativeLocation(FVector(140.0, -180.0, 300.0));
    PointLight->SetIntensity(4500.0f);
    PointLight->SetAttenuationRadius(1000.0f);
    PointLight->SetLightColor(FLinearColor(0.10f, 0.35f, 1.0f));
}

void AInverseSpringRealtimeActor::BeginPlay()
{
    Super::BeginPlay();
    bCaptureRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsResearchInverseCapture"));
    bIdentifiabilityFailure = FParse::Param(FCommandLine::Get(), TEXT("PhysicsResearchInverseIdentifiabilityFailure"));
    bInteractionScriptRequested = FParse::Param(FCommandLine::Get(), TEXT("PhysicsResearchInverseInteractionScript"));
    FParse::Value(FCommandLine::Get(), TEXT("PhysicsResearchInverseAutoExitFrames="), AutoExitFramesAfterCompletion);
    AutoExitFramesAfterCompletion = FMath::Max(AutoExitFramesAfterCompletion, 1);
    SetupVisualMaterials();
    BuildExperiment();
    if (!bInitialized)
    {
        return;
    }
    ConfigureChaosReplay();
    ExperimentComponent->OnExperimentStep.AddDynamic(this, &ThisClass::HandleExperimentStep);
    ExperimentComponent->ResetExperiment();
    ExperimentComponent->StartExperiment();
    StatusText = bIdentifiabilityFailure ? TEXT("失败配置：只观察第 30 步位置，参数不可辨识") : TEXT("运行：目标 / 错误初值 / 优化研究模型 / Chaos Constraint 同参数重放");
    UE_LOG(LogInverseSpringRealtime, Display, TEXT("M5_INVERSE_REALTIME_STARTED steps=%d observation_step=%d dt=%.12g truth_drive=%.9g truth_stiffness=%.9g truth_damping=%.9g recovered_drive=%.9g recovered_damping=%.9g parameter_l1_error=%.12g gradient_relative_error=%.12g capture=%s failure=%s interaction_script=%s"),
        TrajectorySteps, ObservationStep, ExperimentComponent->StepConfig.FixedDeltaSeconds, TruthParameters.DriveForce, TruthParameters.Stiffness, TruthParameters.Damping,
        Optimization.Parameters.DriveForce, Optimization.Parameters.Damping, ParameterL1Error, MaximumGradientRelativeError, bCaptureRequested ? TEXT("true") : TEXT("false"), bIdentifiabilityFailure ? TEXT("true") : TEXT("false"), bInteractionScriptRequested ? TEXT("true") : TEXT("false"));
}

void AInverseSpringRealtimeActor::BuildExperiment()
{
    const double Dt = ExperimentComponent->StepConfig.FixedDeltaSeconds;
    MaximumGradientRelativeError = 0.0;
    ParameterL1Error = 0.0;
    ObservationStep = bIdentifiabilityFailure ? 30 : TrajectorySteps;
    if (!FDifferentiableSpringExperiment::SimulateTrajectory(TruthParameters, Dt, TrajectorySteps, TargetTrajectory)
        || !FDifferentiableSpringExperiment::SimulateTrajectory(InitialParameters, Dt, TrajectorySteps, InitialTrajectory))
    {
        StatusText = TEXT("初始化失败：轨迹前向解无效");
        return;
    }
    const FSpringDesignState& TargetObservation = TargetTrajectory[ObservationStep];
    const FSpringDesignTarget Target{TargetObservation.Position, bIdentifiabilityFailure ? 0.0 : TargetObservation.Velocity, 1.0, bIdentifiabilityFailure ? 0.0 : 0.25};
    const FSpringDesignBounds Bounds{{0.0, TruthParameters.Stiffness, 0.05}, {60.0, TruthParameters.Stiffness + 1.0, 8.0}};
    Optimization = FDifferentiableSpringExperiment::Optimize(InitialParameters, Bounds, Target, Dt, ObservationStep, true, false, true, 250);
    if (!FDifferentiableSpringExperiment::SimulateTrajectory(Optimization.Parameters, Dt, TrajectorySteps, ResearchTrajectory))
    {
        StatusText = TEXT("初始化失败：优化后轨迹无效");
        return;
    }
    // Check derivatives away from the optimum. At a converged optimum both reference and
    // tangent gradients approach zero, so a relative-only comparison becomes ill-conditioned.
    const FSpringDesignEvaluation Tangent = FDifferentiableSpringExperiment::Evaluate(InitialParameters, Target, Dt, ObservationStep);
    const FSpringDesignGradient Finite = FDifferentiableSpringExperiment::EvaluateCenteredFiniteDifference(InitialParameters, Target, Dt, ObservationStep);
    const double Analytic[3] = {Tangent.Gradient.DriveForce, Tangent.Gradient.Stiffness, Tangent.Gradient.Damping};
    const double Reference[3] = {Finite.DriveForce, Finite.Stiffness, Finite.Damping};
    for (int32 Index = 0; Index < 3; ++Index)
    {
        MaximumGradientRelativeError = FMath::Max(MaximumGradientRelativeError, FMath::Abs(Analytic[Index] - Reference[Index]) / FMath::Max(1.0e-8, FMath::Abs(Reference[Index])));
    }
    ResearchFinalTargetErrorCm = VisualizationScaleCm * FMath::Abs(ResearchTrajectory.Last().Position - TargetTrajectory.Last().Position);
    ParameterL1Error = FMath::Abs(Optimization.Parameters.DriveForce - TruthParameters.DriveForce) + FMath::Abs(Optimization.Parameters.Damping - TruthParameters.Damping);
    TargetGate->SetRelativeLocation(FVector(VisualizationScaleCm * TargetTrajectory.Last().Position, -142.0, 55.0));
    UpdateVisualState(0);
    bInitialized = Optimization.bConverged && MaximumGradientRelativeError < 2.0e-5;
    if (!bInitialized)
    {
        UE_LOG(LogInverseSpringRealtime, Error, TEXT("M5_INVERSE_REALTIME_INIT_FAILED converged=%s initial_loss=%.12g fitted_loss=%.12g gradient_relative_error=%.12g"), Optimization.bConverged ? TEXT("true") : TEXT("false"), Optimization.InitialLoss, Optimization.FinalLoss, MaximumGradientRelativeError);
        StatusText = TEXT("初始化失败：优化或梯度门未通过");
    }
}

void AInverseSpringRealtimeActor::ConfigureChaosReplay()
{
    ChaosBody->SetSimulatePhysics(false);
    ChaosBody->SetWorldLocation(GetActorLocation() + FVector(0.0, 90.0, 34.0));
    ChaosBody->SetMassOverrideInKg(NAME_None, 1.0f, true);
    ChaosConstraint->SetConstrainedComponents(ChaosAnchor, NAME_None, ChaosBody, NAME_None);
    ChaosConstraint->SetDisableCollision(true);
    ChaosConstraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Free, 0.0f);
    ChaosConstraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
    ChaosConstraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.0f);
    ChaosConstraint->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
    ChaosConstraint->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.0f);
    ChaosConstraint->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Locked, 0.0f);
    ChaosConstraint->SetLinearPositionDrive(true, false, false);
    ChaosConstraint->SetLinearVelocityDrive(true, false, false);
    ChaosConstraint->SetLinearDriveParams(Optimization.Parameters.Stiffness, Optimization.Parameters.Damping, 0.0f);
    ChaosConstraint->SetLinearPositionTarget(FVector(VisualizationScaleCm * Optimization.Parameters.DriveForce / Optimization.Parameters.Stiffness, 0.0, 0.0));
    ChaosBody->SetSimulatePhysics(true);
    ChaosBody->SetEnableGravity(false);
    ChaosBody->SetPhysicsLinearVelocity(FVector::ZeroVector);
    ChaosBody->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
}

void AInverseSpringRealtimeActor::HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex)
{
    if (!bInitialized || bRunComplete)
    {
        return;
    }
    CompletedSteps = FMath::Clamp(static_cast<int32>(StepIndex), 0, TrajectorySteps);
    UpdateVisualState(CompletedSteps);
    const double ChaosPosition = ChaosBody->GetComponentLocation().X - GetActorLocation().X;
    const double ResearchPosition = VisualizationScaleCm * ResearchTrajectory[CompletedSteps].Position;
    if (FMath::IsFinite(ChaosPosition))
    {
        ChaosSamples.Add(ChaosPosition);
        ChaosSquaredErrorSum += FMath::Square(ChaosPosition - ResearchPosition);
    }
    ExperimentComponent->RecordMetric(TEXT("ChaosVsResearchPosition"), ChaosPosition - ResearchPosition, TEXT("cm"));
    ExperimentComponent->RecordMetric(TEXT("ResearchTargetPosition"), ResearchPosition - VisualizationScaleCm * TargetTrajectory[CompletedSteps].Position, TEXT("cm"));
    if (CompletedSteps >= TrajectorySteps)
    {
        ChaosFinalTargetErrorCm = FMath::Abs(ChaosPosition - VisualizationScaleCm * TargetTrajectory.Last().Position);
        bRunComplete = true;
        ExperimentComponent->PauseExperiment();
        StatusText = bIdentifiabilityFailure ? TEXT("预期失败：观测点拟合成功，但参数与长时轨迹错误") : TEXT("完成：优化参数已进入真实 Chaos Constraint，模型偏差已量化并导出");
        ExportEvidence();
        UE_LOG(LogInverseSpringRealtime, Display, TEXT("M5_INVERSE_REALTIME_COMPLETE steps=%d observation_step=%d chaos_samples=%d initial_loss=%.12g fitted_loss=%.12g recovered_drive=%.12g recovered_damping=%.12g parameter_l1_error=%.12g gradient_relative_error=%.12g research_final_target_error_cm=%.12g chaos_rms_vs_research_cm=%.12g chaos_final_target_error_cm=%.12g chaos_distance_moved_cm=%.12g failure=%s"),
            CompletedSteps, ObservationStep, ChaosSamples.Num(), Optimization.InitialLoss, Optimization.FinalLoss, Optimization.Parameters.DriveForce, Optimization.Parameters.Damping, ParameterL1Error,
            MaximumGradientRelativeError, ResearchFinalTargetErrorCm, GetChaosTrajectoryRmsCm(), ChaosFinalTargetErrorCm, ChaosSamples.Num() > 0 ? FMath::Abs(ChaosSamples.Last()) : 0.0, bIdentifiabilityFailure ? TEXT("true") : TEXT("false"));
    }
}

void AInverseSpringRealtimeActor::UpdateVisualState(const int32 StepIndex)
{
    if (!TargetTrajectory.IsValidIndex(StepIndex) || !InitialTrajectory.IsValidIndex(StepIndex) || !ResearchTrajectory.IsValidIndex(StepIndex))
    {
        return;
    }
    TargetBody->SetRelativeLocation(FVector(VisualizationScaleCm * TargetTrajectory[StepIndex].Position, -90.0, 34.0));
    InitialBody->SetRelativeLocation(FVector(VisualizationScaleCm * InitialTrajectory[StepIndex].Position, -30.0, 34.0));
    ResearchBody->SetRelativeLocation(FVector(VisualizationScaleCm * ResearchTrajectory[StepIndex].Position, 30.0, 34.0));
}

void AInverseSpringRealtimeActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bInitialized)
    {
        return;
    }
    HandleInteractiveInput();
    if (bSingleStepPending)
    {
        ExperimentComponent->StepExperimentOnce();
        SavedChaosVelocity = ChaosBody->GetPhysicsLinearVelocity();
        ChaosBody->SetSimulatePhysics(false);
        bSingleStepPending = false;
        SingleStepCount++;
        StatusText = TEXT("已单步：Chaos 自然物理帧与研究固定步共同前进 1 步");
    }
    AdvanceInteractionScript();
    if (!bCameraViewAssigned)
    {
        if (APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
        {
            PlayerController->SetViewTarget(this);
            bCameraViewAssigned = true;
        }
    }
    RequestPortfolioScreenshot();
    if (bRunComplete && ++FramesAfterCompletion >= AutoExitFramesAfterCompletion)
    {
        FPlatformMisc::RequestExit(false);
    }
}

void AInverseSpringRealtimeActor::HandleInteractiveInput()
{
    APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (!PlayerController)
    {
        return;
    }
    if (PlayerController->WasInputKeyJustPressed(EKeys::SpaceBar)) TogglePauseResume();
    if (PlayerController->WasInputKeyJustPressed(EKeys::N)) RequestSingleStep();
    if (PlayerController->WasInputKeyJustPressed(EKeys::R)) ResetRuntime(true);
    if (PlayerController->WasInputKeyJustPressed(EKeys::O)) ReoptimizeRuntime();
    if (PlayerController->WasInputKeyJustPressed(EKeys::F)) ToggleFailureMode();
    if (PlayerController->WasInputKeyJustPressed(EKeys::D)) ToggleDebugDisplay();
    if (PlayerController->WasInputKeyJustPressed(EKeys::E)) { ExportEvidence(); ManualExportCount++; }
}

void AInverseSpringRealtimeActor::TogglePauseResume()
{
    if (bRunComplete) return;
    if (ExperimentComponent->IsExperimentRunning())
    {
        SavedChaosVelocity = ChaosBody->GetPhysicsLinearVelocity();
        ExperimentComponent->PauseExperiment();
        ChaosBody->SetSimulatePhysics(false);
        StatusText = TEXT("已暂停：N 单步 / 空格继续");
    }
    else if (!bSingleStepPending)
    {
        ChaosBody->SetSimulatePhysics(true);
        ChaosBody->SetEnableGravity(false);
        ChaosBody->SetPhysicsLinearVelocity(SavedChaosVelocity);
        ExperimentComponent->StartExperiment();
        StatusText = bIdentifiabilityFailure ? TEXT("失败配置继续运行") : TEXT("研究模型与 Chaos Constraint 继续运行");
    }
    PauseToggleCount++;
}

void AInverseSpringRealtimeActor::RequestSingleStep()
{
    if (bRunComplete || ExperimentComponent->IsExperimentRunning() || bSingleStepPending) return;
    ChaosBody->SetSimulatePhysics(true);
    ChaosBody->SetEnableGravity(false);
    ChaosBody->SetPhysicsLinearVelocity(SavedChaosVelocity);
    bSingleStepPending = true;
}

void AInverseSpringRealtimeActor::ResetRuntime(const bool bCountReset)
{
    if (bCountReset) ResetCount++;
    ExperimentComponent->PauseExperiment();
    ChaosBody->SetSimulatePhysics(false);
    CompletedSteps = 0;
    FramesAfterCompletion = 0;
    bRunComplete = false;
    bSingleStepPending = false;
    bCaptureIssued = false;
    ChaosSamples.Reset();
    ChaosSquaredErrorSum = 0.0;
    ChaosFinalTargetErrorCm = 0.0;
    bInitialized = false;
    BuildExperiment();
    if (!bInitialized) return;
    ConfigureChaosReplay();
    ExperimentComponent->ResetExperiment();
    ExperimentComponent->StartExperiment();
    StatusText = bIdentifiabilityFailure ? TEXT("已重建：位置单目标不可辨识失败配置") : TEXT("已重建：优化与 Chaos 约束回到同一初态");
}

void AInverseSpringRealtimeActor::ReoptimizeRuntime()
{
    ReoptimizeCount++;
    ResetRuntime(false);
}

void AInverseSpringRealtimeActor::ToggleFailureMode()
{
    bIdentifiabilityFailure = !bIdentifiabilityFailure;
    FailureToggleCount++;
    ResetRuntime(false);
}

void AInverseSpringRealtimeActor::ToggleDebugDisplay()
{
    bDebugDisplay = !bDebugDisplay;
    InitialBody->SetVisibility(bDebugDisplay);
    TargetGate->SetVisibility(bDebugDisplay);
    DebugToggleCount++;
}

void AInverseSpringRealtimeActor::AdvanceInteractionScript()
{
    if (!bInteractionScriptRequested) return;
    if (InteractionScriptPhase == 0 && CompletedSteps >= 20)
    {
        TogglePauseResume(); InteractionScriptPhase = 1; InteractionHoldFrames = 0;
    }
    else if (InteractionScriptPhase == 1 && ++InteractionHoldFrames >= 5)
    {
        RequestSingleStep(); InteractionScriptPhase = 2;
    }
    else if (InteractionScriptPhase == 2 && !bSingleStepPending && SingleStepCount >= 1)
    {
        TogglePauseResume(); InteractionScriptPhase = 3;
    }
    else if (InteractionScriptPhase == 3 && CompletedSteps >= 45)
    {
        ReoptimizeRuntime(); InteractionScriptPhase = 4;
    }
    else if (InteractionScriptPhase == 4 && CompletedSteps >= 30)
    {
        ToggleDebugDisplay(); ExportEvidence(); ManualExportCount++; InteractionScriptPhase = 5;
    }
    else if (InteractionScriptPhase == 5 && CompletedSteps >= 60)
    {
        ResetRuntime(true); InteractionScriptPhase = 6;
    }
    else if (InteractionScriptPhase == 6 && bRunComplete)
    {
        InteractionScriptPhase = 7;
    }
}

double AInverseSpringRealtimeActor::GetChaosTrajectoryRmsCm() const
{
    return ChaosSamples.Num() > 0 ? FMath::Sqrt(ChaosSquaredErrorSum / static_cast<double>(ChaosSamples.Num())) : 0.0;
}

void AInverseSpringRealtimeActor::SetupVisualMaterials()
{
    auto Colorize = [this](UPrimitiveComponent* Component, const FLinearColor& Color)
    {
        if (UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Component->GetMaterial(0), this))
        {
            Material->SetVectorParameterValue(TEXT("Color"), Color);
            Component->SetMaterial(0, Material);
        }
    };
    Colorize(TargetBody, FLinearColor(0.15f, 1.0f, 0.35f));
    Colorize(InitialBody, FLinearColor(1.0f, 0.08f, 0.04f));
    Colorize(ResearchBody, FLinearColor(0.0f, 0.78f, 1.0f));
    Colorize(ChaosBody, FLinearColor(1.0f, 0.58f, 0.04f));
    Colorize(TargetGate, FLinearColor(0.15f, 1.0f, 0.35f));
    Colorize(TrackFloor, FLinearColor(0.012f, 0.025f, 0.06f));
}

void AInverseSpringRealtimeActor::ExportEvidence() const
{
    const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M5/Realtime"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetNumberField(TEXT("steps"), CompletedSteps);
    RootObject->SetNumberField(TEXT("observation_step"), ObservationStep);
    RootObject->SetBoolField(TEXT("identifiability_failure"), bIdentifiabilityFailure);
    RootObject->SetNumberField(TEXT("chaos_samples"), ChaosSamples.Num());
    RootObject->SetNumberField(TEXT("initial_loss"), Optimization.InitialLoss);
    RootObject->SetNumberField(TEXT("fitted_loss"), Optimization.FinalLoss);
    RootObject->SetNumberField(TEXT("recovered_drive"), Optimization.Parameters.DriveForce);
    RootObject->SetNumberField(TEXT("recovered_stiffness"), Optimization.Parameters.Stiffness);
    RootObject->SetNumberField(TEXT("recovered_damping"), Optimization.Parameters.Damping);
    RootObject->SetNumberField(TEXT("parameter_l1_error"), ParameterL1Error);
    RootObject->SetNumberField(TEXT("gradient_relative_error"), MaximumGradientRelativeError);
    RootObject->SetNumberField(TEXT("research_final_target_error_cm"), ResearchFinalTargetErrorCm);
    RootObject->SetNumberField(TEXT("chaos_rms_vs_research_cm"), GetChaosTrajectoryRmsCm());
    RootObject->SetNumberField(TEXT("chaos_final_target_error_cm"), ChaosFinalTargetErrorCm);
    FString Json;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(RootObject, Writer);
    const FString Suffix = bIdentifiabilityFailure ? TEXT("IdentifiabilityFailure") : TEXT("Standard");
    FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Directory, FString::Printf(TEXT("InverseSpringRealtime_%s.json"), *Suffix)));
    FString Csv = TEXT("step,target_cm,initial_cm,research_cm,chaos_cm\n");
    for (int32 Index = 1; Index <= CompletedSteps && Index <= ChaosSamples.Num(); ++Index)
    {
        Csv += FString::Printf(TEXT("%d,%.12g,%.12g,%.12g,%.12g\n"), Index, VisualizationScaleCm * TargetTrajectory[Index].Position, VisualizationScaleCm * InitialTrajectory[Index].Position, VisualizationScaleCm * ResearchTrajectory[Index].Position, ChaosSamples[Index - 1]);
    }
    FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(Directory, FString::Printf(TEXT("InverseSpringRealtime_%s.csv"), *Suffix)));
}

void AInverseSpringRealtimeActor::RequestPortfolioScreenshot()
{
    if (!bCaptureRequested || bCaptureIssued || !bRunComplete)
    {
        return;
    }
    const FString Directory = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/Validation/M5/Screenshots"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = FPaths::Combine(Directory, bIdentifiabilityFailure ? TEXT("Case04_InverseSpring_IdentifiabilityFailure.png") : TEXT("Case04_InverseSpring_ChaosReplay.png"));
    FScreenshotRequest::RequestScreenshot(Path, true, false);
    bCaptureIssued = true;
    UE_LOG(LogInverseSpringRealtime, Display, TEXT("M5_INVERSE_SCREENSHOT_REQUESTED path=%s step=%d"), *Path, CompletedSteps);
}

void AInverseSpringRealtimeActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    const double ChaosMovement = ChaosSamples.Num() > 0 ? FMath::Abs(ChaosSamples.Last()) : 0.0;
    const bool bPassed = bRunComplete && ChaosSamples.Num() >= 100 && ResearchFinalTargetErrorCm < 0.01 && FMath::IsFinite(GetChaosTrajectoryRmsCm()) && ChaosMovement > 50.0 && !bIdentifiabilityFailure;
    const bool bExpectedFailureObserved = bRunComplete && bIdentifiabilityFailure && Optimization.FinalLoss < Optimization.InitialLoss * 1.0e-4 && ParameterL1Error > 1.0 && ResearchFinalTargetErrorCm > 1.0;
    UE_LOG(LogInverseSpringRealtime, Display, TEXT("M5_INVERSE_REALTIME_SUMMARY pass=%s expected_failure=%s complete=%s failure=%s observation_step=%d steps=%d chaos_samples=%d chaos_movement_cm=%.12g chaos_rms_vs_research_cm=%.12g chaos_final_target_error_cm=%.12g research_final_target_error_cm=%.12g parameter_l1_error=%.12g screenshot=%s pause=%d single=%d reset=%d reoptimize=%d debug=%d export=%d failure_toggle=%d interaction_phase=%d"),
        bPassed ? TEXT("true") : TEXT("false"), bExpectedFailureObserved ? TEXT("true") : TEXT("false"), bRunComplete ? TEXT("true") : TEXT("false"), bIdentifiabilityFailure ? TEXT("true") : TEXT("false"), ObservationStep, CompletedSteps, ChaosSamples.Num(), ChaosMovement,
        GetChaosTrajectoryRmsCm(), ChaosFinalTargetErrorCm, ResearchFinalTargetErrorCm, ParameterL1Error, bCaptureIssued ? TEXT("true") : TEXT("false"), PauseToggleCount, SingleStepCount, ResetCount, ReoptimizeCount, DebugToggleCount, ManualExportCount, FailureToggleCount, InteractionScriptPhase);
    Super::EndPlay(EndPlayReason);
}
