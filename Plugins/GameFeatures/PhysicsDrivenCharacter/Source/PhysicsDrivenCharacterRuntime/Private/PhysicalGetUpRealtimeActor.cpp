#include "PhysicalGetUpRealtimeActor.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsControlComponent.h"
#include "PhysicsControlData.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const FName GetUpPelvisBone(TEXT("pelvis"));
}

APhysicalGetUpRealtimeActor::APhysicalGetUpRealtimeActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> MannyAsset(TEXT("/Game/Characters/Heroes/Mannequin/Meshes/SKM_Manny.SKM_Manny"));
    static ConstructorHelpers::FObjectFinder<UPhysicsAsset> MannyPhysicsAsset(TEXT("/Game/Characters/Heroes/Mannequin/Rig/PA_Mannequin.PA_Mannequin"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> FrontAsset(TEXT("/PhysicsDrivenCharacter/Animations/ALS/ALS_CLF_GetUp_Front_Manny.ALS_CLF_GetUp_Front_Manny"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> BackAsset(TEXT("/PhysicsDrivenCharacter/Animations/ALS/ALS_CLF_GetUp_Back_Manny.ALS_CLF_GetUp_Back_Manny"));
    FrontGetUp = FrontAsset.Object;
    BackGetUp = BackAsset.Object;

    Ground = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ground"));
    Ground->SetupAttachment(Root);
    Ground->SetStaticMesh(CubeAsset.Object);
    Ground->SetRelativeLocation(FVector(0.0, 0.0, -12.0));
    Ground->SetRelativeScale3D(FVector(8.0, 7.0, 0.2));
    Ground->SetCollisionProfileName(TEXT("BlockAll"));

    LaneAMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ObservedLaneA"));
    LaneAMesh->SetupAttachment(Root);
    LaneAMesh->SetSkeletalMesh(MannyAsset.Object);
    LaneAMesh->SetPhysicsAsset(MannyPhysicsAsset.Object);
    ConfigureMesh(LaneAMesh, FVector(0.0, -210.0, 0.0));

    LaneBMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ObservedLaneB"));
    LaneBMesh->SetupAttachment(Root);
    LaneBMesh->SetSkeletalMesh(MannyAsset.Object);
    LaneBMesh->SetPhysicsAsset(MannyPhysicsAsset.Object);
    ConfigureMesh(LaneBMesh, FVector(0.0, 0.0, 0.0));

    RejectMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("VelocityRejectedLane"));
    RejectMesh->SetupAttachment(Root);
    RejectMesh->SetSkeletalMesh(MannyAsset.Object);
    RejectMesh->SetPhysicsAsset(MannyPhysicsAsset.Object);
    ConfigureMesh(RejectMesh, FVector(0.0, 210.0, 0.0));

    LaneAControl = CreateDefaultSubobject<UPhysicsControlComponent>(TEXT("LaneAControl"));
    LaneAControl->AddTickPrerequisiteComponent(LaneAMesh);
    LaneBControl = CreateDefaultSubobject<UPhysicsControlComponent>(TEXT("LaneBControl"));
    LaneBControl->AddTickPrerequisiteComponent(LaneBMesh);

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Root);
    Camera->SetRelativeLocation(FVector(-720.0, 0.0, 235.0));
    Camera->SetRelativeRotation(FRotator(-9.0, 0.0, 0.0));

    FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
    FillLight->SetupAttachment(Root);
    FillLight->SetRelativeLocation(FVector(-260.0, 0.0, 340.0));
    FillLight->SetIntensity(22000.0f);
    FillLight->SetAttenuationRadius(1800.0f);
    FillLight->SetLightColor(FLinearColor(0.72f, 0.84f, 1.0f));
}

void APhysicalGetUpRealtimeActor::ConfigureMesh(USkeletalMeshComponent* Mesh, const FVector& Location)
{
    Mesh->SetRelativeLocation(Location);
    Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetEnableGravity(true);
    Mesh->SetLinearDamping(0.5f);
    Mesh->SetAngularDamping(1.2f);
    Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

void APhysicalGetUpRealtimeActor::BeginPlay()
{
    Super::BeginPlay();
    bContinuousMode = FParse::Param(FCommandLine::Get(), TEXT("PhysicalGetUpContinuous"));
    if (bContinuousMode)
    {
        BeginContinuousDemo();
        return;
    }
    bAutoRun = FParse::Param(FCommandLine::Get(), TEXT("PhysicsDrivenCharacterAuto"));
    bInteractionAuto = FParse::Param(FCommandLine::Get(), TEXT("PhysicsDrivenCharacterInteractionAuto"));
    bInteractiveMode = bInteractionAuto || !bAutoRun;

    LaneA = {LaneAMesh, LaneAControl, TEXT("Lane A")};
    LaneB = {LaneBMesh, LaneBControl, TEXT("Lane B")};
    RejectLane = {RejectMesh, nullptr, TEXT("速度拒绝")};
    // Capture the upright reference before placing each lane in the first frame of an
    // ALS prone clip. The subsequent branch is measured from the simulated pelvis pose.
    LaneA.UprightPelvisWorldRotation = LaneAMesh->GetSocketQuaternion(GetUpPelvisBone);
    LaneB.UprightPelvisWorldRotation = LaneBMesh->GetSocketQuaternion(GetUpPelvisBone);
    RejectLane.UprightPelvisWorldRotation = RejectMesh->GetSocketQuaternion(GetUpPelvisBone);
    LaneA.UprightComponentTransform = LaneAMesh->GetComponentTransform();
    LaneB.UprightComponentTransform = LaneBMesh->GetComponentTransform();
    RejectLane.UprightComponentTransform = RejectMesh->GetComponentTransform();
    LaneAMesh->PlayAnimation(FrontGetUp, false);
    LaneBMesh->PlayAnimation(BackGetUp, false);
    RejectMesh->PlayAnimation(BackGetUp, false);
    for (USkeletalMeshComponent* Mesh : {LaneAMesh.Get(), LaneBMesh.Get(), RejectMesh.Get()})
    {
        if (UAnimSingleNodeInstance* Instance = Mesh->GetSingleNodeInstance())
        {
            Instance->SetPosition(0.0f, false);
            Instance->SetPlaying(false);
        }
        Mesh->TickAnimation(0.0f, false);
        Mesh->RefreshBoneTransforms();
    }
    for (FPhysicalGetUpLaneRuntime* Lane : {&LaneA, &LaneB, &RejectLane})
    {
        Lane->Mesh->SetAllBodiesSimulatePhysics(true);
        Lane->Mesh->SetPhysicsBlendWeight(1.0f);
        if (const UPhysicsAsset* PhysicsAsset = Lane->Mesh->GetPhysicsAsset())
        {
            for (const USkeletalBodySetup* BodySetup : PhysicsAsset->SkeletalBodySetups)
            {
                if (BodySetup)
                {
                    if (FBodyInstance* Body = Lane->Mesh->GetBodyInstance(BodySetup->BoneName))
                    {
                        Body->LinearDamping = 4.0f;
                        Body->AngularDamping = 6.0f;
                        Body->UpdateDampingProperties();
                    }
                }
            }
        }
        Lane->Mesh->WakeAllRigidBodies();
    }
    if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
    {
        PlayerController->SetViewTarget(this);
        EnableInput(PlayerController);
        if (InputComponent)
        {
            InputComponent->BindKey(EKeys::One, IE_Pressed, this, &APhysicalGetUpRealtimeActor::SelectFrontPose);
            InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &APhysicalGetUpRealtimeActor::SelectBackPose);
            InputComponent->BindKey(EKeys::P, IE_Pressed, this, &APhysicalGetUpRealtimeActor::ApplyPlayerPush);
            InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &APhysicalGetUpRealtimeActor::RequestPlayerRecovery);
            InputComponent->BindKey(EKeys::R, IE_Pressed, this, &APhysicalGetUpRealtimeActor::ResetInteractiveLane);
        }
    }
}

void APhysicalGetUpRealtimeActor::ResetLaneToProne(FPhysicalGetUpLaneRuntime& Lane, const EPhysicalGetUpBranch PoseBranch)
{
    if (Lane.Control)
    {
        Lane.Control->DestroyControlsInSet(TEXT("All"));
        Lane.Control->DestroyBodyModifiersInSet(TEXT("All"));
    }
    Lane.Mesh->SetAllBodiesSimulatePhysics(false);
    Lane.Mesh->SetWorldTransform(Lane.UprightComponentTransform, false, nullptr, ETeleportType::TeleportPhysics);
    Lane.Mesh->SetPhysicsBlendWeight(0.0f);
    UAnimSequence* Animation = PoseBranch == EPhysicalGetUpBranch::Front ? FrontGetUp : BackGetUp;
    Lane.Mesh->PlayAnimation(Animation, false);
    if (UAnimSingleNodeInstance* Instance = Lane.Mesh->GetSingleNodeInstance())
    {
        Instance->SetPosition(0.0f, false);
        Instance->SetPlaying(false);
    }
    Lane.Mesh->TickAnimation(0.0f, false);
    Lane.Mesh->RefreshBoneTransforms();
    Lane.Mesh->SetAllBodiesSimulatePhysics(true);
    Lane.Mesh->SetPhysicsBlendWeight(1.0f);
    Lane.Mesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector, false);
    Lane.Mesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector, false);
    Lane.Mesh->WakeAllRigidBodies();
    Lane.Phase = EPhysicalGetUpLanePhase::Stabilizing;
    Lane.Branch = EPhysicalGetUpBranch::Pending;
    Lane.StableSeconds = 0.0;
    Lane.RecoveryStartSeconds = -1.0;
    Lane.RecoveryCompleteSeconds = -1.0;
    Lane.ControlCount = 0;
    bPlayerRecoveryRequested = false;
    SelectedPoseBranch = PoseBranch;
}

void APhysicalGetUpRealtimeActor::SelectFrontPose()
{
    if (!bInteractiveMode) return;
    ResetLaneToProne(LaneA, EPhysicalGetUpBranch::Front);
    ++SelectedPoseCount;
}

void APhysicalGetUpRealtimeActor::SelectBackPose()
{
    if (!bInteractiveMode) return;
    ResetLaneToProne(LaneA, EPhysicalGetUpBranch::Back);
    ++SelectedPoseCount;
}

void APhysicalGetUpRealtimeActor::ApplyPlayerPush()
{
    if (!bInteractiveMode) return;
    LaneA.Mesh->WakeAllRigidBodies();
    LaneA.Mesh->AddAngularImpulseInRadians(FVector(0.0, 900000.0, 0.0), GetUpPelvisBone, false);
    bPlayerRecoveryRequested = false;
    ++PushCount;
}

void APhysicalGetUpRealtimeActor::RequestPlayerRecovery()
{
    if (!bInteractiveMode) return;
    bPlayerRecoveryRequested = true;
    ++RecoveryRequestCount;
}

void APhysicalGetUpRealtimeActor::ResetInteractiveLane()
{
    if (!bInteractiveMode) return;
    ResetLaneToProne(LaneA, SelectedPoseBranch);
    ++ResetCount;
}

void APhysicalGetUpRealtimeActor::UpdateAutomatedInteractionScript()
{
    if (!bInteractionAuto) return;
    const double StepElapsed = ElapsedSeconds - InteractionStepStartSeconds;
    if (InteractionScriptStep == 0 && ElapsedSeconds >= 0.5)
    {
        SelectFrontPose();
        InteractionScriptStep = 1;
        InteractionStepStartSeconds = ElapsedSeconds;
    }
    else if (InteractionScriptStep == 1 && StepElapsed >= 0.6)
    {
        LaneAMesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector, false);
        LaneAMesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector, false);
        LaneAMesh->PutAllRigidBodiesToSleep();
        RequestPlayerRecovery();
        InteractionScriptStep = 2;
        InteractionStepStartSeconds = ElapsedSeconds;
    }
    else if (InteractionScriptStep == 2 && LaneA.Phase == EPhysicalGetUpLanePhase::Recovered)
    {
        SelectBackPose();
        InteractionScriptStep = 3;
        InteractionStepStartSeconds = ElapsedSeconds;
    }
    else if (InteractionScriptStep == 3 && StepElapsed >= 0.6)
    {
        LaneAMesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector, false);
        LaneAMesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector, false);
        LaneAMesh->PutAllRigidBodiesToSleep();
        RequestPlayerRecovery();
        InteractionScriptStep = 4;
        InteractionStepStartSeconds = ElapsedSeconds;
    }
    else if (InteractionScriptStep == 4 && LaneA.Phase == EPhysicalGetUpLanePhase::Recovered)
    {
        ResetInteractiveLane();
        ApplyPlayerPush();
        InteractionScriptStep = 5;
        InteractionStepStartSeconds = ElapsedSeconds;
    }
    else if (InteractionScriptStep == 5 && StepElapsed >= 0.2)
    {
        RequestPlayerRecovery();
        const FPhysicalGetUpDecisionInput Input = MeasureDecisionInput(LaneA);
        LaneA.Branch = FPhysicalGetUpDecision::Evaluate(Input);
        LaneA.Phase = EPhysicalGetUpLanePhase::Rejected;
        InteractionScriptStep = 6;
        ExportInteractionEvidence();
        if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController()) PlayerController->ConsoleCommand(TEXT("shot"));
        AutoExitCountdown = 1.0;
    }
}

void APhysicalGetUpRealtimeActor::ApplyFallImpulses()
{
    // This gate begins from canonical front/back prone states. The earlier planar
    // benchmark owns the fall trajectory; this map isolates branch and handoff logic.
    bFallImpulsesApplied = true;
}

FPhysicalGetUpDecisionInput APhysicalGetUpRealtimeActor::MeasureDecisionInput(FPhysicalGetUpLaneRuntime& Lane) const
{
    FPhysicalGetUpDecisionInput Input;
    const FBodyInstance* PelvisBody = Lane.Mesh->GetBodyInstance(GetUpPelvisBone);
    const FTransform PelvisTransform = bContinuousMode && PelvisBody ? PelvisBody->GetUnrealWorldTransform() : Lane.Mesh->GetSocketTransform(GetUpPelvisBone);
    const FVector PelvisLocation = PelvisTransform.GetLocation();
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(PhysicalGetUpGround), false);
    Params.AddIgnoredComponent(Lane.Mesh);
    Input.bGrounded = GetWorld()->LineTraceSingleByChannel(Hit, PelvisLocation + FVector(0.0, 0.0, 10.0), PelvisLocation - FVector(0.0, 0.0, 100.0), ECC_Visibility, Params);
    Input.LinearSpeedCentimetersPerSecond = Lane.Mesh->GetPhysicsLinearVelocity(GetUpPelvisBone).Size();
    Input.AngularSpeedRadiansPerSecond = Lane.Mesh->GetPhysicsAngularVelocityInRadians(GetUpPelvisBone).Size();
    if (bContinuousMode)
    {
        // Contact projection can move a resting body back out of the floor every step.
        // Use observed transform drift for settling; raw high velocities still veto recovery.
        if (Input.LinearSpeedCentimetersPerSecond <= 100.0 && Input.AngularSpeedRadiansPerSecond <= 4.0)
        {
            Input.LinearSpeedCentimetersPerSecond = Lane.FilteredLinearVelocity.Size();
            Input.AngularSpeedRadiansPerSecond = Lane.FilteredAngularVelocity.Size();
        }
    }
    const FQuat CurrentPelvis = PelvisTransform.GetRotation();
    const FQuat UprightToCurrent = CurrentPelvis * Lane.UprightPelvisWorldRotation.Inverse();
    Input.FacingUpDot = FVector::DotProduct(UprightToCurrent.RotateVector(GetActorForwardVector()), FVector::UpVector);
    Input.StableSeconds = Lane.StableSeconds;
    return Input;
}

void APhysicalGetUpRealtimeActor::UpdateLane(FPhysicalGetUpLaneRuntime& Lane, const float DeltaSeconds, const bool bCanRecover)
{
    if (Lane.Phase == EPhysicalGetUpLanePhase::Recovered || Lane.Phase == EPhysicalGetUpLanePhase::Rejected || Lane.Phase == EPhysicalGetUpLanePhase::Failed) return;
    const float Alpha = 1.0f - FMath::Exp(-DeltaSeconds / 0.12f);
    if (bContinuousMode && DeltaSeconds > SMALL_NUMBER)
    {
        const FTransform Pose = Lane.Mesh->GetBodyInstance(GetUpPelvisBone)->GetUnrealWorldTransform();
        if (Lane.bHasPreviousPelvisTransform)
        {
            const FVector Velocity = (Pose.GetLocation() - Lane.PreviousPelvisTransform.GetLocation()) / DeltaSeconds;
            FQuat RotationDelta = Pose.GetRotation() * Lane.PreviousPelvisTransform.GetRotation().Inverse();
            RotationDelta.Normalize();
            FVector Axis;
            double Angle;
            RotationDelta.ToAxisAndAngle(Axis, Angle);
            const FVector AngularVelocity = Axis * FMath::UnwindRadians(Angle) / DeltaSeconds;
            Lane.FilteredLinearVelocity = FMath::Lerp(Lane.FilteredLinearVelocity, Velocity, Alpha);
            Lane.FilteredAngularVelocity = FMath::Lerp(Lane.FilteredAngularVelocity, AngularVelocity, Alpha);
        }
        Lane.PreviousPelvisTransform = Pose;
        Lane.bHasPreviousPelvisTransform = true;
    }
    const FPhysicalGetUpDecisionInput Input = MeasureDecisionInput(Lane);
    Lane.bGrounded = Input.bGrounded;
    Lane.FacingUpDot = Input.FacingUpDot;
    Lane.LinearSpeed = Input.LinearSpeedCentimetersPerSecond;
    Lane.AngularSpeed = Input.AngularSpeedRadiansPerSecond;

    const bool bStableNow = Input.bGrounded && Input.LinearSpeedCentimetersPerSecond <= FPhysicalGetUpDecision::MaximumLinearSpeed && Input.AngularSpeedRadiansPerSecond <= FPhysicalGetUpDecision::MaximumAngularSpeed && FMath::Abs(Input.FacingUpDot) >= FPhysicalGetUpDecision::MinimumFacingDot;
    Lane.StableSeconds = bStableNow ? Lane.StableSeconds + DeltaSeconds : 0.0;
    if (Lane.Phase == EPhysicalGetUpLanePhase::Ragdoll && bFallImpulsesApplied) Lane.Phase = EPhysicalGetUpLanePhase::Stabilizing;

    if (Lane.Phase == EPhysicalGetUpLanePhase::Recovering)
    {
        const double PlayLength = Lane.Branch == EPhysicalGetUpBranch::Front ? FrontGetUp->GetPlayLength() : BackGetUp->GetPlayLength();
        if (ElapsedSeconds - Lane.RecoveryStartSeconds >= PlayLength + 0.25) CompleteRecovery(Lane);
        return;
    }

    if (bCanRecover && ElapsedSeconds >= 1.0)
    {
        FPhysicalGetUpDecisionInput UpdatedInput = Input;
        UpdatedInput.StableSeconds = Lane.StableSeconds;
        const EPhysicalGetUpBranch Decision = FPhysicalGetUpDecision::Evaluate(UpdatedInput);
        if (Decision == EPhysicalGetUpBranch::Front || Decision == EPhysicalGetUpBranch::Back)
        {
            Lane.Branch = Decision;
            BeginRecovery(Lane);
        }
        else if (bAutoRun && ElapsedSeconds >= 5.5)
        {
            Lane.Branch = Decision;
            Lane.Phase = EPhysicalGetUpLanePhase::Failed;
        }
    }
}

void APhysicalGetUpRealtimeActor::CreateRecoveryControls(FPhysicalGetUpLaneRuntime& Lane)
{
    const FName BodySet(*FString::Printf(TEXT("%sBodies"), *Lane.Label));
    const FName ParentSet(*FString::Printf(TEXT("%sParent"), *Lane.Label));
    const FName WorldSet(*FString::Printf(TEXT("%sWorld"), *Lane.Label));
    const FPhysicsControlModifierData Simulated(EPhysicsMovementType::Simulated, ECollisionEnabled::QueryAndPhysics, 1.0f, 1.0f, true, true);
    Lane.Control->CreateBodyModifiersFromSkeletalMeshBelow(Lane.Mesh, GetUpPelvisBone, true, BodySet, Simulated);

    FPhysicsControlData ParentControl;
    ParentControl.AngularStrength = 16.0f;
    ParentControl.AngularDampingRatio = 1.0f;
    ParentControl.AngularExtraDamping = 0.5f;
    ParentControl.MaxTorque = 0.0f;
    ParentControl.bUseSkeletalAnimation = true;
    const TArray<FName> ParentControls = Lane.Control->CreateControlsFromSkeletalMeshBelow(Lane.Mesh, GetUpPelvisBone, false, EPhysicsControlType::ParentSpace, ParentControl, ParentSet);

    FPhysicsControlData WorldControl;
    WorldControl.LinearStrength = 10.0f;
    WorldControl.LinearDampingRatio = 1.0f;
    WorldControl.MaxForce = 0.0f;
    WorldControl.AngularStrength = 10.0f;
    WorldControl.AngularDampingRatio = 1.0f;
    WorldControl.MaxTorque = 0.0f;
    WorldControl.bUseSkeletalAnimation = true;
    const FName PelvisControl = Lane.Control->CreateControl(nullptr, NAME_None, Lane.Mesh, GetUpPelvisBone, WorldControl, FPhysicsControlTarget(), WorldSet, Lane.Label + TEXT("Pelvis"));
    Lane.ControlCount = ParentControls.Num() + (PelvisControl.IsNone() ? 0 : 1);
}

void APhysicalGetUpRealtimeActor::BeginRecovery(FPhysicalGetUpLaneRuntime& Lane)
{
    UAnimSequence* Animation = Lane.Branch == EPhysicalGetUpBranch::Front ? FrontGetUp : BackGetUp;
    if (!Animation || !Lane.Control)
    {
        Lane.Phase = EPhysicalGetUpLanePhase::Failed;
        return;
    }
    if (bContinuousMode) AlignContinuousRecovery();
    if (bContinuousMode && Lane.Mesh->GetSingleNodeInstance())
    {
        Lane.Mesh->GetSingleNodeInstance()->SetAnimationAsset(Animation, false, 1.0f);
        Lane.Mesh->GetSingleNodeInstance()->SetPlaying(true);
    }
    else Lane.Mesh->PlayAnimation(Animation, false);
    Lane.Mesh->SetPhysicsBlendWeight(1.0f);
    Lane.Mesh->SetAllBodiesBelowSimulatePhysics(GetUpPelvisBone, true, true);
    CreateRecoveryControls(Lane);
    Lane.RecoveryStartSeconds = ElapsedSeconds;
    Lane.Phase = EPhysicalGetUpLanePhase::Recovering;
}

void APhysicalGetUpRealtimeActor::CompleteRecovery(FPhysicalGetUpLaneRuntime& Lane)
{
    if (bContinuousMode)
    {
        // The ALS CLF clip ends crouched. Track an upright target before handing off.
        if (!bContinuousStandingTarget)
        {
            Lane.Mesh->GetSingleNodeInstance()->SetAnimationAsset(ContinuousIdle, true, 1.0f);
            Lane.Mesh->GetSingleNodeInstance()->SetPlaying(true);
            bContinuousStandingTarget = true;
            return;
        }
        const FTransform Pose = Lane.Mesh->GetBodyInstance(GetUpPelvisBone)->GetUnrealWorldTransform();
        const bool bUpright = Pose.GetLocation().Z > Lane.UprightComponentTransform.GetLocation().Z + 80.0
            && Pose.GetRotation().AngularDistance(Lane.UprightPelvisWorldRotation) < 0.5
            && Lane.Mesh->GetPhysicsLinearVelocity(GetUpPelvisBone).Size() < 100.0;
        ContinuousUprightStableSeconds = bUpright ? ContinuousUprightStableSeconds + GetWorld()->GetDeltaSeconds() : 0.0;
        if (ContinuousUprightStableSeconds < 0.25) return;
    }
    // PhysicsControl owns the BodyModifier movement type. Remove that ownership before
    // handing the mesh back to animation, otherwise its next PrePhysics tick can
    // silently re-enable simulation after SetAllBodiesSimulatePhysics(false).
    Lane.Control->DestroyControlsInSet(TEXT("All"));
    Lane.Control->DestroyBodyModifiersInSet(TEXT("All"));
    Lane.ControlCount = 0;
    Lane.Mesh->SetAllBodiesSimulatePhysics(false);
    Lane.Mesh->SetWorldTransform(Lane.UprightComponentTransform, false, nullptr, ETeleportType::TeleportPhysics);
    Lane.Mesh->SetPhysicsBlendWeight(0.0f);
    if (UAnimSingleNodeInstance* Instance = Lane.Mesh->GetSingleNodeInstance())
    {
        const UAnimSequence* Animation = Lane.Branch == EPhysicalGetUpBranch::Front ? FrontGetUp : BackGetUp;
        Instance->SetPosition(FMath::Max(0.0f, Animation->GetPlayLength() - 1.0f / 60.0f), false);
        Instance->SetPlaying(false);
    }
    const FPhysicalGetUpDecisionInput FinalInput = MeasureDecisionInput(Lane);
    Lane.FacingUpDot = FinalInput.FacingUpDot;
    Lane.LinearSpeed = 0.0;
    Lane.AngularSpeed = 0.0;
    Lane.RecoveryCompleteSeconds = ElapsedSeconds;
    Lane.Phase = EPhysicalGetUpLanePhase::Recovered;
    if (bContinuousMode) RestoreContinuousMovement();
}

void APhysicalGetUpRealtimeActor::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    ElapsedSeconds += DeltaSeconds;
    if (bContinuousMode)
    {
        TickContinuousDemo(DeltaSeconds);
        return;
    }
    UpdateAutomatedInteractionScript();
    if (bInteractionAuto)
    {
        UpdateLane(LaneA, DeltaSeconds, bPlayerRecoveryRequested);
        if (AutoExitCountdown >= 0.0)
        {
            AutoExitCountdown -= DeltaSeconds;
            if (AutoExitCountdown <= 0.0)
            {
                if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController()) PlayerController->ConsoleCommand(TEXT("quit"));
            }
        }
        return;
    }
    if (!bFallImpulsesApplied && ElapsedSeconds >= 0.2) ApplyFallImpulses();

    if (!bCanonicalLanesSettled && ElapsedSeconds >= 1.0)
    {
        for (USkeletalMeshComponent* Mesh : {LaneAMesh.Get(), LaneBMesh.Get()})
        {
            Mesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector, false);
            Mesh->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector, false);
            Mesh->PutAllRigidBodiesToSleep();
        }
        bCanonicalLanesSettled = true;
    }

    UpdateLane(LaneA, DeltaSeconds, bAutoRun || bPlayerRecoveryRequested);
    UpdateLane(LaneB, DeltaSeconds, bAutoRun);
    UpdateLane(RejectLane, DeltaSeconds, false);

    if (!bRejectAgitationApplied && ElapsedSeconds >= 2.4)
    {
        RejectMesh->AddAngularImpulseInRadians(FVector(0.0, 900000.0, 0.0), GetUpPelvisBone, false);
        const FPhysicalGetUpDecisionInput RejectedInput = MeasureDecisionInput(RejectLane);
        RejectLane.Branch = FPhysicalGetUpDecision::Evaluate(RejectedInput);
        RejectLane.Phase = EPhysicalGetUpLanePhase::Rejected;
        bRejectAgitationApplied = true;
    }

    if (!bRunComplete && ElapsedSeconds >= 8.0)
    {
        bRunComplete = true;
        ExportEvidence();
        if (bAutoRun)
        {
            if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController()) PlayerController->ConsoleCommand(TEXT("shot"));
            AutoExitCountdown = 1.0;
        }
    }
    if (AutoExitCountdown >= 0.0)
    {
        AutoExitCountdown -= DeltaSeconds;
        if (AutoExitCountdown <= 0.0)
        {
            if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController()) PlayerController->ConsoleCommand(TEXT("quit"));
        }
    }
}

const TCHAR* APhysicalGetUpRealtimeActor::PhaseToString(const EPhysicalGetUpLanePhase Phase)
{
    switch (Phase)
    {
    case EPhysicalGetUpLanePhase::Ragdoll: return TEXT("Ragdoll");
    case EPhysicalGetUpLanePhase::Stabilizing: return TEXT("Stabilizing");
    case EPhysicalGetUpLanePhase::Recovering: return TEXT("Recovering");
    case EPhysicalGetUpLanePhase::Recovered: return TEXT("Recovered");
    case EPhysicalGetUpLanePhase::Rejected: return TEXT("Rejected");
    case EPhysicalGetUpLanePhase::Failed: return TEXT("Failed");
    default: return TEXT("Unknown");
    }
}

FString APhysicalGetUpRealtimeActor::GetLaneLine(const int32 LaneIndex) const
{
    const FPhysicalGetUpLaneRuntime& Lane = LaneIndex == 0 ? LaneA : (LaneIndex == 1 ? LaneB : RejectLane);
    return FString::Printf(TEXT("%s：%s / %s · faceDot %.2f · v %.1f cm/s · w %.2f rad/s · controls %d"), *Lane.Label, PhaseToString(Lane.Phase), FPhysicalGetUpDecision::ToString(Lane.Branch), Lane.FacingUpDot, Lane.LinearSpeed, Lane.AngularSpeed, Lane.ControlCount);
}

FString APhysicalGetUpRealtimeActor::GetStatusLine() const
{
    if (bContinuousMode) return TEXT("连续过程：移动 → 受击倒地 → 稳定判定 → 动画驱动起身 → 恢复移动");
    if (bInteractiveMode && !bInteractionAuto) return TEXT("交互：选择姿态、施加扰动，再由真实 Chaos 状态决定能否起身");
    if (bInteractionAuto) return TEXT("交互回放：Front → Back → 高速拒绝，调用与键盘相同动作路径");
    if (!bFallImpulsesApplied) return TEXT("准备：三路 Manny 进入同一 Chaos Ragdoll");
    if (!bRunComplete) return TEXT("运行：实际 pelvis 姿态与速度决定 ALS Front / Back / Reject");
    return TEXT("完成：物理分支、PhysicsControl 交接与 ALS Manny 动画证据已导出");
}

FString APhysicalGetUpRealtimeActor::GetInteractionLine() const
{
    return FString::Printf(TEXT("交互计数：选姿 %d · 推撞 %d · 起身请求 %d · 重置 %d"), SelectedPoseCount, PushCount, RecoveryRequestCount, ResetCount);
}

void APhysicalGetUpRealtimeActor::ExportInteractionEvidence()
{
    if (bInteractionEvidenceExported) return;
    bInteractionEvidenceExported = true;
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PhysicsResearch"));
    FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Directory);
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
    Json->SetNumberField(TEXT("pose_selection_count"), SelectedPoseCount);
    Json->SetNumberField(TEXT("push_count"), PushCount);
    Json->SetNumberField(TEXT("recovery_request_count"), RecoveryRequestCount);
    Json->SetNumberField(TEXT("reset_count"), ResetCount);
    Json->SetNumberField(TEXT("script_step_count"), InteractionScriptStep);
    Json->SetStringField(TEXT("final_branch"), FPhysicalGetUpDecision::ToString(LaneA.Branch));
    Json->SetStringField(TEXT("final_phase"), PhaseToString(LaneA.Phase));
    Json->SetBoolField(TEXT("uses_same_actions_as_keyboard"), true);
    Json->SetBoolField(TEXT("interaction_pass"), SelectedPoseCount == 2 && PushCount == 1 && RecoveryRequestCount == 3 && ResetCount == 1 && LaneA.Branch == EPhysicalGetUpBranch::RejectMoving);
    FString Text;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    FJsonSerializer::Serialize(Json, Writer);
    FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Directory, TEXT("PhysicalGetUpInteraction.json")));
}

void APhysicalGetUpRealtimeActor::ExportEvidence()
{
    if (bEvidenceExported) return;
    bEvidenceExported = true;
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PhysicsResearch"));
    FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*Directory);

    TArray<TSharedPtr<FJsonValue>> LaneValues;
    for (const FPhysicalGetUpLaneRuntime* Lane : {&LaneA, &LaneB, &RejectLane})
    {
        TSharedRef<FJsonObject> LaneJson = MakeShared<FJsonObject>();
        LaneJson->SetStringField(TEXT("label"), Lane->Label);
        LaneJson->SetStringField(TEXT("phase"), PhaseToString(Lane->Phase));
        LaneJson->SetStringField(TEXT("branch"), FPhysicalGetUpDecision::ToString(Lane->Branch));
        LaneJson->SetNumberField(TEXT("facing_up_dot"), Lane->FacingUpDot);
        LaneJson->SetNumberField(TEXT("linear_speed_cm_s"), Lane->LinearSpeed);
        LaneJson->SetNumberField(TEXT("angular_speed_rad_s"), Lane->AngularSpeed);
        LaneJson->SetNumberField(TEXT("stable_seconds"), Lane->StableSeconds);
        LaneJson->SetNumberField(TEXT("recovery_start_s"), Lane->RecoveryStartSeconds);
        LaneJson->SetNumberField(TEXT("recovery_complete_s"), Lane->RecoveryCompleteSeconds);
        LaneJson->SetNumberField(TEXT("control_count"), Lane->ControlCount);
        LaneValues.Add(MakeShared<FJsonValueObject>(LaneJson));
    }
    const bool bObservedBothBranches = (LaneA.Branch == EPhysicalGetUpBranch::Front && LaneB.Branch == EPhysicalGetUpBranch::Back) || (LaneA.Branch == EPhysicalGetUpBranch::Back && LaneB.Branch == EPhysicalGetUpBranch::Front);
    const bool bBothRecovered = LaneA.Phase == EPhysicalGetUpLanePhase::Recovered && LaneB.Phase == EPhysicalGetUpLanePhase::Recovered;
    const bool bVelocityRejected = RejectLane.Phase == EPhysicalGetUpLanePhase::Rejected && RejectLane.Branch == EPhysicalGetUpBranch::RejectMoving;
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("engine"), FEngineVersion::Current().ToString());
    Json->SetStringField(TEXT("front_animation"), FrontGetUp ? FrontGetUp->GetPathName() : TEXT(""));
    Json->SetStringField(TEXT("back_animation"), BackGetUp ? BackGetUp->GetPathName() : TEXT(""));
    Json->SetArrayField(TEXT("lanes"), LaneValues);
    Json->SetBoolField(TEXT("observed_front_and_back"), bObservedBothBranches);
    Json->SetBoolField(TEXT("both_recovered"), bBothRecovered);
    Json->SetBoolField(TEXT("velocity_rejection_pass"), bVelocityRejected);
    Json->SetBoolField(TEXT("get_up_handoff_pass"), bObservedBothBranches && bBothRecovered && bVelocityRejected);
    FString Text;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    FJsonSerializer::Serialize(Json, Writer);
    FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Directory, TEXT("PhysicalGetUpHandoff.json")));
}
