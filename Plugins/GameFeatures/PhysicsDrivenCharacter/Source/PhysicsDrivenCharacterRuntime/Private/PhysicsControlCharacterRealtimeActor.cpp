#include "PhysicsControlCharacterRealtimeActor.h"
#include "PhysicsPortfolioCapture.h"
#include "AssetCompilingManager.h"
#include "ContentStreaming.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MeasuredPhysicsControlComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsControlComponent.h"
#include "PhysicsControlData.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Components/PointLightComponent.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const FName PelvisBone(TEXT("pelvis"));
const FName ResearchBodySet(TEXT("ResearchBodies"));
const FName ResearchParentControlSet(TEXT("ResearchParentControls"));
const FName ResearchWorldControlSet(TEXT("ResearchWorldControls"));
const FName HipBone(TEXT("thigh_l"));
const FName HipParentBone(TEXT("pelvis"));
const FName KneeBone(TEXT("calf_l"));
const FName KneeParentBone(TEXT("thigh_l"));
const FName TorsoBone(TEXT("spine_03"));
const FName TorsoParentBone(TEXT("spine_02"));
constexpr double DropMappingHeightCentimeters = 60.0;
constexpr int32 PerformanceWarmupSamples = 300;

double NearestRankPercentile(TArray<double> Values, const double Percentile)
{
    if (Values.IsEmpty()) return 0.0;
    Values.Sort();
    const int32 Index = FMath::Clamp(FMath::CeilToInt(Percentile * Values.Num()) - 1, 0, Values.Num() - 1);
    return Values[Index];
}

double Mean(const TArray<double>& Values)
{
    if (Values.IsEmpty()) return 0.0;
    double Sum = 0.0;
    for (const double Value : Values) Sum += Value;
    return Sum / Values.Num();
}

double Maximum(const TArray<double>& Values)
{
    double Result = 0.0;
    for (const double Value : Values) Result = FMath::Max(Result, Value);
    return Result;
}

FQuat RelativeRotation(USkeletalMeshComponent* Mesh, const FName ChildBone, const FName ParentBone)
{
    const FQuat Child = Mesh->GetSocketQuaternion(ChildBone);
    const FQuat Parent = Mesh->GetSocketQuaternion(ParentBone);
    return Parent.Inverse() * Child;
}
}

APhysicsControlCharacterRealtimeActor::APhysicsControlCharacterRealtimeActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    bPerformanceMode = FParse::Param(FCommandLine::Get(), TEXT("PhysicsDrivenCharacterPerformance"));
    bPortfolioMode = FParse::Param(FCommandLine::Get(), TEXT("PhysicsDrivenCharacterPortfolio"));

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> MannyAsset(TEXT("/Game/Characters/Heroes/Mannequin/Meshes/SKM_Manny.SKM_Manny"));
    static ConstructorHelpers::FObjectFinder<UPhysicsAsset> MannyPhysicsAsset(TEXT("/Game/Characters/Heroes/Mannequin/Rig/PA_Mannequin.PA_Mannequin"));

    Ground = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ground"));
    Ground->SetupAttachment(Root);
    Ground->SetStaticMesh(CubeAsset.Object);
    Ground->SetRelativeLocation(FVector(0.0, 0.0, -12.0));
    Ground->SetRelativeScale3D(bPerformanceMode ? FVector(100.0, 100.0, 0.2) : FVector(8.0, 5.0, 0.2));
    Ground->SetCollisionProfileName(TEXT("BlockAll"));

    BaselineMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ChaosPassiveRagdoll"));
    BaselineMesh->SetupAttachment(Root);
    BaselineMesh->SetSkeletalMesh(MannyAsset.Object);
    BaselineMesh->SetPhysicsAsset(MannyPhysicsAsset.Object);
    BaselineMesh->SetRelativeLocation(FVector(0.0, -115.0, 0.0));
    ConfigureMesh(BaselineMesh, TEXT("Ragdoll"));

    ResearchMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PhysicsControlActive"));
    ResearchMesh->SetupAttachment(Root);
    ResearchMesh->SetSkeletalMesh(MannyAsset.Object);
    ResearchMesh->SetPhysicsAsset(MannyPhysicsAsset.Object);
    ResearchMesh->SetRelativeLocation(FVector(0.0, 115.0, 0.0));
    ConfigureMesh(ResearchMesh, TEXT("Ragdoll"));

    // The drop scenario must be selected before component registration creates the
    // Physics Asset bodies. Moving the component later leaves body transforms stale.
    bDropMappingMode = FParse::Param(FCommandLine::Get(), TEXT("PhysicsDrivenCharacterDropMapping"));
    if (bDropMappingMode)
    {
        BaselineMesh->SetRelativeLocation(BaselineMesh->GetRelativeLocation() + FVector(0.0, 0.0, DropMappingHeightCentimeters));
        ResearchMesh->SetRelativeLocation(ResearchMesh->GetRelativeLocation() + FVector(0.0, 0.0, DropMappingHeightCentimeters));
    }

    ResearchPhysicsControl = CreateDefaultSubobject<UMeasuredPhysicsControlComponent>(TEXT("ResearchPhysicsControl"));
    ResearchPhysicsControl->AddTickPrerequisiteComponent(ResearchMesh);

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Root);
    Camera->SetRelativeLocation(FVector(-610.0, 0.0, 205.0));
    Camera->SetRelativeRotation(FRotator(-8.0, 0.0, 0.0));

    FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
    FillLight->SetupAttachment(Root);
    FillLight->SetRelativeLocation(FVector(-260.0, 0.0, 310.0));
    FillLight->SetIntensity(18000.0f);
    FillLight->SetAttenuationRadius(1600.0f);
    FillLight->SetLightColor(FLinearColor(0.72f, 0.84f, 1.0f));
}

void APhysicsControlCharacterRealtimeActor::ConfigureMesh(USkeletalMeshComponent* Mesh, const TCHAR* CollisionProfile)
{
    Mesh->SetCollisionProfileName(CollisionProfile);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetEnableGravity(true);
    Mesh->SetLinearDamping(0.2f);
    Mesh->SetAngularDamping(0.5f);
    Mesh->bUpdateJointsFromAnimation = false;
}

void APhysicsControlCharacterRealtimeActor::BeginPlay()
{
    Super::BeginPlay();
    bAutoRun = FParse::Param(FCommandLine::Get(), TEXT("PhysicsDrivenCharacterAuto"));
    bDropMappingMode = FParse::Param(FCommandLine::Get(), TEXT("PhysicsDrivenCharacterDropMapping"));
    bPerformanceMode = FParse::Param(FCommandLine::Get(), TEXT("PhysicsDrivenCharacterPerformance"));
    bPortfolioMode = FParse::Param(FCommandLine::Get(), TEXT("PhysicsDrivenCharacterPortfolio"));

    BaselineMesh->SetPhysicsBlendWeight(1.0f);
    ResearchMesh->SetPhysicsBlendWeight(1.0f);
    if (bDropMappingMode)
    {
        // SetPhysicsBlendWeight(1) enables component simulation internally. The drop
        // scenario needs a strict release gate, so its lock happens after this call.
        BaselineMesh->SetSimulatePhysics(false);
        ResearchMesh->SetSimulatePhysics(false);
        BaselineMesh->SetAllBodiesSimulatePhysics(false);
        ResearchMesh->SetAllBodiesSimulatePhysics(false);
    }

    if (const UPhysicsAsset* PhysicsAsset = ResearchMesh->GetPhysicsAsset())
    {
        PhysicsBodyCount = PhysicsAsset->SkeletalBodySetups.Num();
        for (const USkeletalBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
        {
            if (!BodySetup) continue;
            const FName BoneName = BodySetup->BoneName;
            InitialBaselineBoneTransforms.Add(BoneName, BaselineMesh->GetSocketTransform(BoneName, RTS_World));
            InitialResearchBoneTransforms.Add(BoneName, ResearchMesh->GetSocketTransform(BoneName, RTS_World));
        }
    }
    InitialBaselinePelvisLocation = BaselineMesh->GetSocketLocation(PelvisBone);
    InitialResearchPelvisLocation = ResearchMesh->GetSocketLocation(PelvisBone);
    InitialBaselineRelativeJointRotations.Add(HipBone, RelativeRotation(BaselineMesh, HipBone, HipParentBone));
    InitialBaselineRelativeJointRotations.Add(KneeBone, RelativeRotation(BaselineMesh, KneeBone, KneeParentBone));
    InitialBaselineRelativeJointRotations.Add(TorsoBone, RelativeRotation(BaselineMesh, TorsoBone, TorsoParentBone));
    InitialResearchRelativeJointRotations.Add(HipBone, RelativeRotation(ResearchMesh, HipBone, HipParentBone));
    InitialResearchRelativeJointRotations.Add(KneeBone, RelativeRotation(ResearchMesh, KneeBone, KneeParentBone));
    InitialResearchRelativeJointRotations.Add(TorsoBone, RelativeRotation(ResearchMesh, TorsoBone, TorsoParentBone));
    MinimumBaselinePelvisHeightCentimeters = InitialBaselinePelvisLocation.Z;
    MinimumResearchPelvisHeightCentimeters = InitialResearchPelvisLocation.Z;
    InitializePhysicsControl();
    if (bPortfolioMode) InitializePortfolioScene();
    if (FParse::Param(FCommandLine::Get(), TEXT("PhysicsDrivenCharacterRecord")))
    {
        // Finish the recording assets before the first physics step; never hide
        // compilation messages while recording fallback materials.
        FAssetCompilingManager::Get().FinishAllCompilation();
        IStreamingManager::Get().StreamAllResources(60.0f);
        UE_LOG(LogTemp, Display, TEXT("Physics portfolio assets ready: remaining=%d"),
            FAssetCompilingManager::Get().GetNumRemainingAssets());
    }
    PreviousCaptureWallSeconds = FPlatformTime::Seconds();

    if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
    {
        PlayerController->SetViewTarget(this);
    }
}

void APhysicsControlCharacterRealtimeActor::InitializePortfolioScene()
{
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!Cube || !BaseMaterial) return;

    auto AddPanel = [&](const TCHAR* Name, const FVector Location, const FVector Scale, const FLinearColor Color)
    {
        UStaticMeshComponent* Panel = NewObject<UStaticMeshComponent>(this, Name);
        AddInstanceComponent(Panel);
        Panel->SetupAttachment(Root);
        Panel->SetStaticMesh(Cube);
        Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Panel->SetCastShadow(false);
        Panel->SetRelativeLocation(Location);
        Panel->SetRelativeScale3D(Scale);
        Panel->RegisterComponent();
        UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, Panel);
        Material->SetVectorParameterValue(TEXT("Color"), Color);
        Panel->SetMaterial(0, Material);
        PortfolioMeshes.Add(Panel);
    };
    auto AddLight = [&](const TCHAR* Name, const FVector Location, const FLinearColor Color, const float Intensity)
    {
        UPointLightComponent* Light = NewObject<UPointLightComponent>(this, Name);
        AddInstanceComponent(Light);
        Light->SetupAttachment(Root);
        Light->SetRelativeLocation(Location);
        Light->SetLightColor(Color);
        Light->SetIntensity(Intensity);
        Light->SetAttenuationRadius(720.0f);
        Light->SetCastShadows(true);
        Light->RegisterComponent();
        PortfolioLights.Add(Light);
    };

    AddPanel(TEXT("PortfolioBackdrop"), FVector(330.0, 0.0, 235.0), FVector(0.10, 15.0, 3.40), FLinearColor(0.018f, 0.028f, 0.055f));
    AddPanel(TEXT("PortfolioLeftAccent"), FVector(318.0, -145.0, 130.0), FVector(0.035, 1.55, 1.12), FLinearColor(0.16f, 0.035f, 0.025f));
    AddPanel(TEXT("PortfolioRightAccent"), FVector(317.0, 145.0, 130.0), FVector(0.035, 1.55, 1.12), FLinearColor(0.015f, 0.12f, 0.20f));
    AddPanel(TEXT("PortfolioCenterLine"), FVector(315.0, 0.0, 118.0), FVector(0.025, 0.025, 1.20), FLinearColor(0.18f, 0.65f, 0.95f));
    AddPanel(TEXT("PortfolioLeftStage"), FVector(28.0, -120.0, -0.5), FVector(2.20, 1.55, 0.035), FLinearColor(0.10f, 0.025f, 0.02f));
    AddPanel(TEXT("PortfolioRightStage"), FVector(28.0, 120.0, -0.5), FVector(2.20, 1.55, 0.035), FLinearColor(0.015f, 0.075f, 0.13f));
    AddLight(TEXT("PortfolioBaselineLight"), FVector(-80.0, -170.0, 245.0), FLinearColor(1.0f, 0.20f, 0.08f), 12500.0f);
    AddLight(TEXT("PortfolioResearchLight"), FVector(-80.0, 170.0, 245.0), FLinearColor(0.05f, 0.58f, 1.0f), 15000.0f);
}

void APhysicsControlCharacterRealtimeActor::InitializePhysicsControl()
{
    const FPhysicsControlModifierData KinematicModifier(EPhysicsMovementType::Kinematic, ECollisionEnabled::QueryAndPhysics, 1.0f, 1.0f, true, true);
    ResearchPhysicsControl->CreateBodyModifiersFromSkeletalMeshBelow(ResearchMesh, PelvisBone, true, ResearchBodySet, KinematicModifier);

    FPhysicsControlData ParentControl;
    ParentControl.AngularStrength = bDropMappingMode ? 60.0f : 18.0f;
    ParentControl.AngularDampingRatio = bDropMappingMode ? 1.2f : 1.0f;
    ParentControl.AngularExtraDamping = 0.5f;
    ParentControl.MaxTorque = 0.0f;
    ParentControl.bUseSkeletalAnimation = true;
    const TArray<FName> ParentControls = ResearchPhysicsControl->CreateControlsFromSkeletalMeshBelow(ResearchMesh, PelvisBone, false, EPhysicsControlType::ParentSpace, ParentControl, ResearchParentControlSet);

    FPhysicsControlData WorldControl;
    WorldControl.LinearStrength = 12.0f;
    WorldControl.LinearDampingRatio = 1.0f;
    // Zero means unlimited in PhysicsControl. The benchmark tests the controller
    // architecture first; a later parameter sweep will introduce energy budgets.
    WorldControl.MaxForce = 0.0f;
    WorldControl.AngularStrength = bDropMappingMode ? 40.0f : 12.0f;
    WorldControl.AngularDampingRatio = 1.0f;
    WorldControl.MaxTorque = 0.0f;
    WorldControl.bUseSkeletalAnimation = false;
    FPhysicsControlTarget Target;
    const FTransform InitialPelvis = ResearchMesh->GetSocketTransform(PelvisBone, RTS_World);
    Target.TargetPosition = InitialPelvis.GetLocation();
    Target.TargetOrientation = InitialPelvis.Rotator();
    Target.bApplyControlPointToTarget = true;
    ResearchPelvisControlName = ResearchPhysicsControl->CreateControl(nullptr, NAME_None, ResearchMesh, PelvisBone, WorldControl, Target, ResearchWorldControlSet, TEXT("ResearchPelvis"));
    ResearchControlCount = ParentControls.Num() + (ResearchPelvisControlName.IsNone() ? 0 : 1);
}

void APhysicsControlCharacterRealtimeActor::ApplySharedImpulse()
{
    if (bDropMappingMode) BaselineMesh->SetAllBodiesBelowSimulatePhysics(PelvisBone, true, true);
    else BaselineMesh->SetAllBodiesSimulatePhysics(true);
    BaselineMesh->WakeAllRigidBodies();
    ResearchPhysicsControl->SetBodyModifiersInSetMovementType(ResearchBodySet, EPhysicsMovementType::Simulated);
    // Body-modifier changes are consumed by PhysicsControl's pre-physics tick. Make the
    // same-frame impulse valid immediately, then let the modifier own subsequent state.
    ResearchMesh->SetAllBodiesBelowSimulatePhysics(PelvisBone, true, true);
    ResearchMesh->WakeAllRigidBodies();

    if (!bDropMappingMode)
    {
        const FVector Impulse(0.0, 5200.0, 0.0);
        BaselineMesh->AddImpulse(Impulse, TEXT("spine_03"), false);
        ResearchMesh->AddImpulse(Impulse, TEXT("spine_03"), false);
    }
    bImpulseApplied = true;
}

double APhysicsControlCharacterRealtimeActor::ComputeMeanPoseErrorDegrees(USkeletalMeshComponent* Mesh) const
{
    const TMap<FName, FTransform>& Initial = Mesh == BaselineMesh ? InitialBaselineBoneTransforms : InitialResearchBoneTransforms;
    if (Initial.IsEmpty()) return 0.0;
    double Sum = 0.0;
    int32 Count = 0;
    for (const TPair<FName, FTransform>& Pair : Initial)
    {
        const FQuat Current = Mesh->GetSocketQuaternion(Pair.Key);
        Sum += FMath::RadiansToDegrees(Current.AngularDistance(Pair.Value.GetRotation()));
        ++Count;
    }
    return Count > 0 ? Sum / Count : 0.0;
}

double APhysicsControlCharacterRealtimeActor::ComputeRelativeJointErrorDegrees(USkeletalMeshComponent* Mesh, const FName ChildBone, const FName ParentBone) const
{
    const TMap<FName, FQuat>& Initial = Mesh == BaselineMesh ? InitialBaselineRelativeJointRotations : InitialResearchRelativeJointRotations;
    const FQuat* InitialRotation = Initial.Find(ChildBone);
    return InitialRotation ? FMath::RadiansToDegrees(RelativeRotation(Mesh, ChildBone, ParentBone).AngularDistance(*InitialRotation)) : 0.0;
}

void APhysicsControlCharacterRealtimeActor::CaptureSample()
{
    BaselinePoseErrorDegrees = ComputeMeanPoseErrorDegrees(BaselineMesh);
    ResearchPoseErrorDegrees = ComputeMeanPoseErrorDegrees(ResearchMesh);
    BaselinePelvisDisplacementCentimeters = FVector::Distance(BaselineMesh->GetSocketLocation(PelvisBone), InitialBaselinePelvisLocation);
    ResearchPelvisDisplacementCentimeters = FVector::Distance(ResearchMesh->GetSocketLocation(PelvisBone), InitialResearchPelvisLocation);
    const double BaselinePelvisHeight = BaselineMesh->GetSocketLocation(PelvisBone).Z;
    const double ResearchPelvisHeight = ResearchMesh->GetSocketLocation(PelvisBone).Z;
    const double BaselineVerticalSpeed = BaselineMesh->GetPhysicsLinearVelocity(PelvisBone).Z;
    const double ResearchVerticalSpeed = ResearchMesh->GetPhysicsLinearVelocity(PelvisBone).Z;
    PeakBaselineDownwardSpeedCentimetersPerSecond = FMath::Max(PeakBaselineDownwardSpeedCentimetersPerSecond, -BaselineVerticalSpeed);
    PeakResearchDownwardSpeedCentimetersPerSecond = FMath::Max(PeakResearchDownwardSpeedCentimetersPerSecond, -ResearchVerticalSpeed);
    MinimumBaselinePelvisHeightCentimeters = FMath::Min(MinimumBaselinePelvisHeightCentimeters, BaselinePelvisHeight);
    MinimumResearchPelvisHeightCentimeters = FMath::Min(MinimumResearchPelvisHeightCentimeters, ResearchPelvisHeight);
    const FVector BaselineJointErrors(
        ComputeRelativeJointErrorDegrees(BaselineMesh, HipBone, HipParentBone),
        ComputeRelativeJointErrorDegrees(BaselineMesh, KneeBone, KneeParentBone),
        ComputeRelativeJointErrorDegrees(BaselineMesh, TorsoBone, TorsoParentBone));
    const FVector ResearchJointErrors(
        ComputeRelativeJointErrorDegrees(ResearchMesh, HipBone, HipParentBone),
        ComputeRelativeJointErrorDegrees(ResearchMesh, KneeBone, KneeParentBone),
        ComputeRelativeJointErrorDegrees(ResearchMesh, TorsoBone, TorsoParentBone));
    PeakBaselineJointErrorsDegrees.X = FMath::Max(PeakBaselineJointErrorsDegrees.X, BaselineJointErrors.X);
    PeakBaselineJointErrorsDegrees.Y = FMath::Max(PeakBaselineJointErrorsDegrees.Y, BaselineJointErrors.Y);
    PeakBaselineJointErrorsDegrees.Z = FMath::Max(PeakBaselineJointErrorsDegrees.Z, BaselineJointErrors.Z);
    PeakResearchJointErrorsDegrees.X = FMath::Max(PeakResearchJointErrorsDegrees.X, ResearchJointErrors.X);
    PeakResearchJointErrorsDegrees.Y = FMath::Max(PeakResearchJointErrorsDegrees.Y, ResearchJointErrors.Y);
    PeakResearchJointErrorsDegrees.Z = FMath::Max(PeakResearchJointErrorsDegrees.Z, ResearchJointErrors.Z);
    MaximumBaselinePoseErrorDegrees = FMath::Max(MaximumBaselinePoseErrorDegrees, BaselinePoseErrorDegrees);
    MaximumResearchPoseErrorDegrees = FMath::Max(MaximumResearchPoseErrorDegrees, ResearchPoseErrorDegrees);
    Samples.Add({ElapsedSeconds, BaselinePoseErrorDegrees, ResearchPoseErrorDegrees, BaselinePelvisDisplacementCentimeters, ResearchPelvisDisplacementCentimeters,
        BaselinePelvisHeight, ResearchPelvisHeight, BaselineVerticalSpeed, ResearchVerticalSpeed});
    FDrivenCharacterRealtimeSample& Sample = Samples.Last();
    Sample.BaselineHipErrorDegrees = BaselineJointErrors.X;
    Sample.BaselineKneeErrorDegrees = BaselineJointErrors.Y;
    Sample.BaselineTorsoErrorDegrees = BaselineJointErrors.Z;
    Sample.ResearchHipErrorDegrees = ResearchJointErrors.X;
    Sample.ResearchKneeErrorDegrees = ResearchJointErrors.Y;
    Sample.ResearchTorsoErrorDegrees = ResearchJointErrors.Z;
    const double CaptureWallSeconds = FPlatformTime::Seconds();
    Sample.FrameWallMilliseconds = (CaptureWallSeconds - PreviousCaptureWallSeconds) * 1000.0;
    Sample.PhysicsControlUpdateMilliseconds = ResearchPhysicsControl->GetLastUpdateMilliseconds();
    PreviousCaptureWallSeconds = CaptureWallSeconds;
}

void APhysicsControlCharacterRealtimeActor::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    ElapsedSeconds += DeltaSeconds;
    if (!bImpulseApplied && ElapsedSeconds >= 0.5) ApplySharedImpulse();
    if (bDropMappingMode && bImpulseApplied && !ResearchPelvisControlName.IsNone())
    {
        const double FallTime = FMath::Max(0.0, ElapsedSeconds - 0.5);
        const double TargetDrop = FMath::Min(DropMappingHeightCentimeters, 0.5 * 980.0 * FallTime * FallTime);
        ResearchPhysicsControl->SetControlTargetPosition(ResearchPelvisControlName,
            InitialResearchPelvisLocation - FVector(0.0, 0.0, TargetDrop), DeltaSeconds, true, true);
    }
    CaptureSample();
    CapturePhysicsPortfolioFrame(ElapsedSeconds);

    const double RunDurationSeconds = bPerformanceMode ? 30.0 : 6.0;
    if (!bRunComplete && ElapsedSeconds >= RunDurationSeconds)
    {
        bRunComplete = true;
        ExportRuntimeEvidence();
        if (bAutoRun)
        {
            GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("shot"));
            AutoExitCountdown = 1.0;
        }
    }
    if (AutoExitCountdown >= 0.0)
    {
        AutoExitCountdown -= DeltaSeconds;
        if (AutoExitCountdown <= 0.0) GetWorld()->GetFirstPlayerController()->ConsoleCommand(TEXT("quit"));
    }
}

void APhysicsControlCharacterRealtimeActor::ExportRuntimeEvidence()
{
    if (bEvidenceExported) return;
    bEvidenceExported = true;
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PhysicsResearch"));
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    PlatformFile.CreateDirectoryTree(*Directory);

    FString Csv = TEXT("time_s,baseline_pose_error_deg,research_pose_error_deg,baseline_pelvis_displacement_cm,research_pelvis_displacement_cm,baseline_pelvis_height_cm,research_pelvis_height_cm,baseline_pelvis_vertical_speed_cm_s,research_pelvis_vertical_speed_cm_s,baseline_hip_error_deg,baseline_knee_error_deg,baseline_torso_error_deg,research_hip_error_deg,research_knee_error_deg,research_torso_error_deg,frame_wall_ms,physics_control_update_ms\n");
    for (const FDrivenCharacterRealtimeSample& Sample : Samples)
    {
        Csv += FString::Printf(TEXT("%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n"), Sample.TimeSeconds, Sample.BaselinePoseErrorDegrees, Sample.ResearchPoseErrorDegrees,
            Sample.BaselinePelvisDisplacementCentimeters, Sample.ResearchPelvisDisplacementCentimeters, Sample.BaselinePelvisHeightCentimeters,
            Sample.ResearchPelvisHeightCentimeters, Sample.BaselinePelvisVerticalSpeedCentimetersPerSecond, Sample.ResearchPelvisVerticalSpeedCentimetersPerSecond,
            Sample.BaselineHipErrorDegrees, Sample.BaselineKneeErrorDegrees, Sample.BaselineTorsoErrorDegrees,
            Sample.ResearchHipErrorDegrees, Sample.ResearchKneeErrorDegrees, Sample.ResearchTorsoErrorDegrees,
            Sample.FrameWallMilliseconds, Sample.PhysicsControlUpdateMilliseconds);
    }
    const TCHAR* CsvName = bDropMappingMode ? TEXT("DrivenCharacterDropMapping.csv")
        : bPerformanceMode ? TEXT("DrivenCharacterPerformance.csv") : TEXT("DrivenCharacterRealtime.csv");
    FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(Directory, CsvName));

    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
    Json->SetStringField(TEXT("baseline_backend"), TEXT("ChaosPassiveRagdoll"));
    Json->SetStringField(TEXT("research_backend"), TEXT("ChaosPhysicsControlActive"));
    Json->SetStringField(TEXT("scenario"), bDropMappingMode ? TEXT("HeightDropMapping")
        : bPerformanceMode ? TEXT("SharedSpineImpulsePerformance") : TEXT("SharedSpineImpulse"));
    Json->SetNumberField(TEXT("physics_body_count"), PhysicsBodyCount);
    Json->SetNumberField(TEXT("scene_rigid_body_count"), PhysicsBodyCount * 2);
    Json->SetNumberField(TEXT("research_control_count"), ResearchControlCount);
    Json->SetBoolField(TEXT("shared_impulse_applied"), bImpulseApplied);
    Json->SetNumberField(TEXT("shared_impulse_y_kg_cm_per_s"), bDropMappingMode ? 0.0 : 5200.0);
    Json->SetNumberField(TEXT("drop_height_cm"), bDropMappingMode ? DropMappingHeightCentimeters : 0.0);
    Json->SetNumberField(TEXT("sample_count"), Samples.Num());
    Json->SetBoolField(TEXT("portfolio_scene"), bPortfolioMode);
    Json->SetNumberField(TEXT("portfolio_mesh_count"), PortfolioMeshes.Num());
    Json->SetNumberField(TEXT("portfolio_light_count"), PortfolioLights.Num());
    if (bPerformanceMode)
    {
        TArray<double> FrameWallMilliseconds;
        TArray<double> PhysicsControlUpdateMilliseconds;
        for (int32 Index = FMath::Min(PerformanceWarmupSamples, Samples.Num()); Index < Samples.Num(); ++Index)
        {
            FrameWallMilliseconds.Add(Samples[Index].FrameWallMilliseconds);
            PhysicsControlUpdateMilliseconds.Add(Samples[Index].PhysicsControlUpdateMilliseconds);
        }
        Json->SetStringField(TEXT("performance_rhi"), FParse::Param(FCommandLine::Get(), TEXT("nullrhi")) ? TEXT("NullRHI") : TEXT("Rendered"));
        Json->SetStringField(TEXT("cpu_brand"), FPlatformMisc::GetCPUBrand());
        Json->SetNumberField(TEXT("physical_core_count"), FPlatformMisc::NumberOfCores());
        Json->SetNumberField(TEXT("logical_core_count"), FPlatformMisc::NumberOfCoresIncludingHyperthreads());
        Json->SetNumberField(TEXT("fixed_step_hz"), 60.0);
        Json->SetStringField(TEXT("frame_wall_scope"), TEXT("Consecutive TG_PostPhysics actor samples; isolated map end-to-end frame wall time, not a Chaos-only timer"));
        Json->SetStringField(TEXT("physics_control_scope"), TEXT("Measured Super::TickComponent: Epic PhysicsControl target cache, 22 controls and body modifiers"));
        Json->SetNumberField(TEXT("performance_warmup_samples"), FMath::Min(PerformanceWarmupSamples, Samples.Num()));
        Json->SetNumberField(TEXT("performance_measurement_samples"), FrameWallMilliseconds.Num());
        Json->SetNumberField(TEXT("frame_wall_mean_ms"), Mean(FrameWallMilliseconds));
        Json->SetNumberField(TEXT("frame_wall_p50_ms"), NearestRankPercentile(FrameWallMilliseconds, 0.50));
        Json->SetNumberField(TEXT("frame_wall_p95_ms"), NearestRankPercentile(FrameWallMilliseconds, 0.95));
        Json->SetNumberField(TEXT("frame_wall_p99_ms"), NearestRankPercentile(FrameWallMilliseconds, 0.99));
        Json->SetNumberField(TEXT("frame_wall_max_ms"), Maximum(FrameWallMilliseconds));
        Json->SetNumberField(TEXT("physics_control_update_mean_ms"), Mean(PhysicsControlUpdateMilliseconds));
        Json->SetNumberField(TEXT("physics_control_update_p50_ms"), NearestRankPercentile(PhysicsControlUpdateMilliseconds, 0.50));
        Json->SetNumberField(TEXT("physics_control_update_p95_ms"), NearestRankPercentile(PhysicsControlUpdateMilliseconds, 0.95));
        Json->SetNumberField(TEXT("physics_control_update_p99_ms"), NearestRankPercentile(PhysicsControlUpdateMilliseconds, 0.99));
        Json->SetNumberField(TEXT("physics_control_update_max_ms"), Maximum(PhysicsControlUpdateMilliseconds));
    }
    Json->SetNumberField(TEXT("baseline_final_pose_error_deg"), BaselinePoseErrorDegrees);
    Json->SetNumberField(TEXT("research_final_pose_error_deg"), ResearchPoseErrorDegrees);
    Json->SetNumberField(TEXT("baseline_final_pelvis_displacement_cm"), BaselinePelvisDisplacementCentimeters);
    Json->SetNumberField(TEXT("research_final_pelvis_displacement_cm"), ResearchPelvisDisplacementCentimeters);
    Json->SetNumberField(TEXT("baseline_max_pose_error_deg"), MaximumBaselinePoseErrorDegrees);
    Json->SetNumberField(TEXT("research_max_pose_error_deg"), MaximumResearchPoseErrorDegrees);
    Json->SetNumberField(TEXT("baseline_peak_downward_speed_cm_s"), PeakBaselineDownwardSpeedCentimetersPerSecond);
    Json->SetNumberField(TEXT("research_peak_downward_speed_cm_s"), PeakResearchDownwardSpeedCentimetersPerSecond);
    Json->SetNumberField(TEXT("baseline_pelvis_drop_cm"), InitialBaselinePelvisLocation.Z - MinimumBaselinePelvisHeightCentimeters);
    Json->SetNumberField(TEXT("research_pelvis_drop_cm"), InitialResearchPelvisLocation.Z - MinimumResearchPelvisHeightCentimeters);
    Json->SetNumberField(TEXT("baseline_peak_hip_error_deg"), PeakBaselineJointErrorsDegrees.X);
    Json->SetNumberField(TEXT("baseline_peak_knee_error_deg"), PeakBaselineJointErrorsDegrees.Y);
    Json->SetNumberField(TEXT("baseline_peak_torso_error_deg"), PeakBaselineJointErrorsDegrees.Z);
    Json->SetNumberField(TEXT("research_peak_hip_error_deg"), PeakResearchJointErrorsDegrees.X);
    Json->SetNumberField(TEXT("research_peak_knee_error_deg"), PeakResearchJointErrorsDegrees.Y);
    Json->SetNumberField(TEXT("research_peak_torso_error_deg"), PeakResearchJointErrorsDegrees.Z);
    Json->SetNumberField(TEXT("baseline_proximal_to_torso_transfer_ratio"), PeakBaselineJointErrorsDegrees.Z > UE_SMALL_NUMBER
        ? FMath::Max(PeakBaselineJointErrorsDegrees.X, PeakBaselineJointErrorsDegrees.Y) / PeakBaselineJointErrorsDegrees.Z : 0.0);
    Json->SetNumberField(TEXT("research_proximal_to_torso_transfer_ratio"), PeakResearchJointErrorsDegrees.Z > UE_SMALL_NUMBER
        ? FMath::Max(PeakResearchJointErrorsDegrees.X, PeakResearchJointErrorsDegrees.Y) / PeakResearchJointErrorsDegrees.Z : 0.0);
    Json->SetNumberField(TEXT("final_pose_error_ratio_research_over_baseline"), BaselinePoseErrorDegrees > UE_SMALL_NUMBER ? ResearchPoseErrorDegrees / BaselinePoseErrorDegrees : 0.0);
    Json->SetNumberField(TEXT("final_pelvis_displacement_ratio_research_over_baseline"), BaselinePelvisDisplacementCentimeters > UE_SMALL_NUMBER ? ResearchPelvisDisplacementCentimeters / BaselinePelvisDisplacementCentimeters : 0.0);
    const bool bPassiveRagdollCollapseObserved = bDropMappingMode && BaselinePoseErrorDegrees > 90.0
        && BaselinePelvisDisplacementCentimeters > 100.0 && PeakBaselineDownwardSpeedCentimetersPerSecond > 100.0
        && PeakBaselineDownwardSpeedCentimetersPerSecond < 500.0
        && InitialBaselinePelvisLocation.Z - MinimumBaselinePelvisHeightCentimeters > 60.0
        && InitialBaselinePelvisLocation.Z - MinimumBaselinePelvisHeightCentimeters < 200.0;
    const bool bResearchBoundedLanding = bDropMappingMode && ResearchPoseErrorDegrees < 35.0
        && PeakResearchDownwardSpeedCentimetersPerSecond > 100.0 && PeakResearchDownwardSpeedCentimetersPerSecond < 700.0
        && InitialResearchPelvisLocation.Z - MinimumResearchPelvisHeightCentimeters > 45.0
        && InitialResearchPelvisLocation.Z - MinimumResearchPelvisHeightCentimeters < 100.0
        && ResearchPelvisDisplacementCentimeters > 40.0 && ResearchPelvisDisplacementCentimeters < 120.0;
    Json->SetBoolField(TEXT("passive_ragdoll_collapse_observed"), bPassiveRagdollCollapseObserved);
    Json->SetBoolField(TEXT("research_bounded_landing_pass"), bResearchBoundedLanding);
    const bool bControllerAdvantage = bDropMappingMode
        ? bPassiveRagdollCollapseObserved && bResearchBoundedLanding && ResearchPoseErrorDegrees < BaselinePoseErrorDegrees * 0.5
        : ResearchPoseErrorDegrees < BaselinePoseErrorDegrees * 0.5 && ResearchPelvisDisplacementCentimeters < 10.0;
    Json->SetBoolField(TEXT("controller_advantage_pass"), bControllerAdvantage);
    FString JsonText;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
    FJsonSerializer::Serialize(Json, Writer);
    const TCHAR* JsonName = bDropMappingMode ? TEXT("DrivenCharacterDropMapping.json")
        : bPerformanceMode ? TEXT("DrivenCharacterPerformance.json") : TEXT("DrivenCharacterRealtime.json");
    FFileHelper::SaveStringToFile(JsonText, *FPaths::Combine(Directory, JsonName));
}

FString APhysicsControlCharacterRealtimeActor::GetStatusLine() const
{
    if (!bImpulseApplied) return bDropMappingMode ? TEXT("准备：两路 Manny 位于同一 60 cm 高度") : TEXT("准备：两路 Manny 保持同一参考姿态");
    if (!bRunComplete) return bDropMappingMode ? TEXT("运行：同一高度跌落进入被动 Chaos 与主动 Physics Control") : TEXT("运行：同一冲量进入被动 Chaos 与主动 Physics Control");
    return TEXT("完成：CSV/JSON 已导出，可比较被动布娃娃与主动控制");
}
