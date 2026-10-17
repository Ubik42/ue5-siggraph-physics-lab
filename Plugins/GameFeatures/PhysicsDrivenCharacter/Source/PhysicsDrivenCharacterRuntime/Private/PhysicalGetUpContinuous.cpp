#include "PhysicalGetUpRealtimeActor.h"
#include "PhysicsPortfolioCapture.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsControlComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "Serialization/JsonSerializer.h"

void APhysicalGetUpRealtimeActor::BeginContinuousDemo()
{
    bContinuousAuto = FParse::Param(FCommandLine::Get(), TEXT("PhysicalGetUpContinuousAuto"));
    for (USkeletalMeshComponent* Mesh : {LaneAMesh.Get(), LaneBMesh.Get(), RejectMesh.Get()})
    {
        Mesh->SetHiddenInGame(true);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetComponentTickEnabled(false);
    }
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ContinuousCharacter = GetWorld()->SpawnActor<ACharacter>(FVector(20, 0, 90), FRotator::ZeroRotator, Spawn);
    check(ContinuousCharacter);
    ContinuousCharacter->GetCapsuleComponent()->SetCapsuleSize(34, 90);
    USkeletalMeshComponent* Mesh = ContinuousCharacter->GetMesh();
    Mesh->SetSkeletalMesh(LaneAMesh->GetSkeletalMeshAsset());
    Mesh->SetPhysicsAsset(LaneAMesh->GetPhysicsAsset());
    Mesh->SetRelativeLocation(FVector(0, 0, -90));
    Mesh->SetRelativeRotation(FRotator(0, -90, 0));
    Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Mesh->SetCollisionProfileName(TEXT("CharacterMesh"));
    ContinuousIdle = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Heroes/Mannequin/Animations/Locomotion/Unarmed/MM_Unarmed_Idle_Ready.MM_Unarmed_Idle_Ready"));
    ContinuousWalk = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Heroes/Mannequin/Animations/Locomotion/Unarmed/MM_Unarmed_Walk_Fwd.MM_Unarmed_Walk_Fwd"));
    check(ContinuousIdle && ContinuousWalk && FrontGetUp && BackGetUp);
    Mesh->PlayAnimation(ContinuousIdle, true);
    Mesh->TickAnimation(0, false);
    Mesh->RefreshBoneTransforms();
    LaneA = {Mesh, LaneAControl, TEXT("玩家角色")};
    LaneA.UprightPelvisWorldRotation = Mesh->GetSocketQuaternion(TEXT("pelvis"));
    LaneA.UprightComponentTransform = Mesh->GetComponentTransform();
    LaneA.Phase = EPhysicalGetUpLanePhase::Recovered;
    LaneAControl->RemoveTickPrerequisiteComponent(LaneAMesh);
    ContinuousCharacter->GetCharacterMovement()->MaxWalkSpeed = 120;
    ContinuousCharacter->GetCharacterMovement()->bOrientRotationToMovement = false;
    Camera->SetRelativeLocation(FVector(-320, -430, 235));
    Camera->SetRelativeRotation((FVector(110, 40, 70) - Camera->GetComponentLocation()).Rotation());
    Camera->SetFieldOfView(75);
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        PC->Possess(ContinuousCharacter);
        PC->bAutoManageActiveCameraTarget = false;
        PC->SetViewTarget(this);
        EnableInput(PC);
        InputComponent->BindKey(EKeys::P, IE_Pressed, this, &APhysicalGetUpRealtimeActor::PushContinuousCharacter);
        InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &APhysicalGetUpRealtimeActor::RequestPlayerRecovery);
    }
    bInteractiveMode = true;
}

void APhysicalGetUpRealtimeActor::MoveContinuousCharacter(float Value)
{
    if (LaneA.Phase == EPhysicalGetUpLanePhase::Recovered && !FMath::IsNearlyZero(Value))
        ContinuousCharacter->AddMovementInput(FVector::ForwardVector, Value);
}

void APhysicalGetUpRealtimeActor::PushContinuousCharacter()
{
    if (LaneA.Phase != EPhysicalGetUpLanePhase::Recovered) return;
    ContinuousCharacter->GetCharacterMovement()->StopMovementImmediately();
    ContinuousCharacter->GetCharacterMovement()->DisableMovement();
    ContinuousCharacter->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    LaneA.Mesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    LaneA.Mesh->PhysicsTransformUpdateMode = EPhysicsTransformUpdateMode::SimulationUpatesComponentTransform;
    LaneA.Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
    LaneA.Mesh->Stop();
    LaneA.Mesh->SetSimulatePhysics(true);
    LaneA.Mesh->ResetAllBodiesSimulatePhysics();
    LaneA.Mesh->SetAllBodiesPhysicsBlendWeight(1);
    int32 DrivenConstraints = 0;
    for (const FConstraintInstance* Constraint : LaneA.Mesh->Constraints)
    {
        const auto& Drive = Constraint->ProfileInstance.AngularDrive;
        if (Drive.SlerpDrive.bEnablePositionDrive || Drive.SlerpDrive.bEnableVelocityDrive ||
            Drive.SwingDrive.bEnablePositionDrive || Drive.SwingDrive.bEnableVelocityDrive ||
            Drive.TwistDrive.bEnablePositionDrive || Drive.TwistDrive.bEnableVelocityDrive) ++DrivenConstraints;
    }
    UE_LOG(LogTemp, Display, TEXT("ContinuousGetUp inherited driven constraints=%d"), DrivenConstraints);
    LaneA.Mesh->SetAllMotorsAngularPositionDrive(false, false);
    LaneA.Mesh->SetAllMotorsAngularVelocityDrive(false, false);
    LaneA.Mesh->SetAllMotorsAngularDriveParams(0, 0, 0);
    for (const USkeletalBodySetup* Setup : LaneA.Mesh->GetPhysicsAsset()->SkeletalBodySetups)
    {
        if (Setup)
        {
            if (FBodyInstance* Body = LaneA.Mesh->GetBodyInstance(Setup->BoneName))
            {
                Body->LinearDamping = 4.0f;
                Body->AngularDamping = 6.0f;
                Body->UpdateDampingProperties();
            }
        }
    }
    LaneA.Mesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
    LaneA.Mesh->WakeAllRigidBodies();
    LaneA.Mesh->AddImpulse(FVector(-240, 0, 30), TEXT("spine_03"), true);
    LaneA.FilteredLinearVelocity = LaneA.Mesh->GetPhysicsLinearVelocity(TEXT("pelvis"));
    LaneA.FilteredAngularVelocity = LaneA.Mesh->GetPhysicsAngularVelocityInRadians(TEXT("pelvis"));
    LaneA.bHasPreviousPelvisTransform = false;
    LaneA.Phase = EPhysicalGetUpLanePhase::Stabilizing;
    LaneA.Branch = EPhysicalGetUpBranch::Pending;
    LaneA.StableSeconds = 0;
    bContinuousPushed = true;
    bContinuousStandingTarget = false;
    ContinuousRecoveredAt = -1.0;
    ContinuousUprightStableSeconds = 0.0;
    ContinuousMovedAfterRecovery = 0;
    bPlayerRecoveryRequested = false;
    ++PushCount;
}

void APhysicalGetUpRealtimeActor::AlignContinuousRecovery()
{
    // Move only the animation target frame, preserving the current simulated bodies.
    const FVector Pelvis = LaneA.Mesh->GetBodyInstance(TEXT("pelvis"))->GetUnrealWorldTransform().GetLocation();
    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(ContinuousCharacter);
    const bool GroundHit = GetWorld()->LineTraceSingleByChannel(Hit, Pelvis + FVector(0,0,30), Pelvis - FVector(0,0,180), ECC_Visibility, Params);
    const FVector Floor(Pelvis.X, Pelvis.Y, GroundHit ? Hit.ImpactPoint.Z : 0);
    ContinuousCharacter->SetActorLocation(Floor + FVector(0,0,90), false, nullptr, ETeleportType::TeleportPhysics);
    LaneA.UprightComponentTransform = FTransform(FRotator(0,-90,0), Floor);
    struct FBodyState { FBodyInstance* Body; FTransform Transform; FVector Linear; FVector Angular; };
    TArray<FBodyState> Bodies;
    for (FBodyInstance* Body : LaneA.Mesh->Bodies)
        Bodies.Add({Body, Body->GetUnrealWorldTransform(), Body->GetUnrealWorldVelocity(), Body->GetUnrealWorldAngularVelocityInRadians()});
    // A moving root would feed the driven pelvis motion back into its own target.
    LaneA.Mesh->PhysicsTransformUpdateMode = EPhysicsTransformUpdateMode::ComponentTransformIsKinematic;
    LaneA.Mesh->SetWorldTransform(LaneA.UprightComponentTransform, false, nullptr, ETeleportType::TeleportPhysics);
    for (const FBodyState& State : Bodies)
    {
        State.Body->SetBodyTransform(State.Transform, ETeleportType::TeleportPhysics);
        State.Body->SetLinearVelocity(State.Linear, false);
        State.Body->SetAngularVelocityInRadians(State.Angular, false);
    }
}

void APhysicalGetUpRealtimeActor::RestoreContinuousMovement()
{
    LaneA.Mesh->AttachToComponent(ContinuousCharacter->GetCapsuleComponent(), FAttachmentTransformRules::KeepWorldTransform);
    LaneA.Mesh->SetRelativeLocationAndRotation(FVector(0,0,-90), FRotator(0,-90,0));
    LaneA.Mesh->SetCollisionProfileName(TEXT("CharacterMesh"));
    LaneA.Mesh->PlayAnimation(ContinuousIdle, true);
    bContinuousWalking = false;
    ContinuousCharacter->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    ContinuousCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    ContinuousRecoveredAt = ElapsedSeconds;
    ContinuousRecoveryPosition = ContinuousCharacter->GetActorLocation();
}

void APhysicalGetUpRealtimeActor::TickContinuousDemo(float DeltaSeconds)
{
    CapturePhysicsPortfolioFrame(ElapsedSeconds);
    if (!bContinuousAuto)
    {
        if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
            MoveContinuousCharacter((PC->IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::S) ? 1.f : 0.f));
    }
    if (bContinuousAuto)
    {
        if (!bContinuousPushed && ElapsedSeconds > 1 && ElapsedSeconds < 2) MoveContinuousCharacter(1);
        if (!bContinuousPushed && ElapsedSeconds >= 2) PushContinuousCharacter();
        if (!bContinuousRequested && ElapsedSeconds >= 2.15)
        {
            RequestPlayerRecovery();
            const auto Input = MeasureDecisionInput(LaneA);
            const auto Decision = FPhysicalGetUpDecision::Evaluate(Input);
            if (Decision != EPhysicalGetUpBranch::Front && Decision != EPhysicalGetUpBranch::Back) ++ContinuousRejectedRequests;
            bContinuousRequested = true;
        }
        if (ContinuousRecoveredAt >= 0 && ElapsedSeconds - ContinuousRecoveredAt >= 0.7 && ElapsedSeconds - ContinuousRecoveredAt < 2.2)
            MoveContinuousCharacter(1);
    }
    if (LaneA.Phase != EPhysicalGetUpLanePhase::Recovered)
    {
        UpdateLane(LaneA, DeltaSeconds, bPlayerRecoveryRequested);
        if (LaneA.Phase == EPhysicalGetUpLanePhase::Stabilizing)
            LaneA.Branch = FPhysicalGetUpDecision::Evaluate(MeasureDecisionInput(LaneA));
    }
    else
    {
        const bool Walking = ContinuousCharacter->GetVelocity().Size2D() > 3;
        if (Walking != bContinuousWalking)
        {
            LaneA.Mesh->PlayAnimation(Walking ? ContinuousWalk : ContinuousIdle, true);
            bContinuousWalking = Walking;
        }
        if (ContinuousRecoveredAt >= 0)
            ContinuousMovedAfterRecovery = FVector::Dist2D(ContinuousRecoveryPosition, ContinuousCharacter->GetActorLocation());
    }
    const FVector Position = ContinuousCharacter->GetActorLocation();
    const FVector Pelvis = LaneA.Mesh->GetBodyInstance(TEXT("pelvis"))->GetUnrealWorldTransform().GetLocation();
    ContinuousTrace += FString::Printf(TEXT("%.6f,%s,%s,%d,%.6f,%.6f,%.6f,%.6f,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n"),
        ElapsedSeconds, PhaseToString(LaneA.Phase), FPhysicalGetUpDecision::ToString(LaneA.Branch), LaneA.bGrounded,
        LaneA.FacingUpDot, LaneA.LinearSpeed, LaneA.AngularSpeed, LaneA.StableSeconds, LaneA.ControlCount,
        LaneA.Mesh->IsSimulatingPhysics(TEXT("pelvis")), int32(ContinuousCharacter->GetCharacterMovement()->MovementMode), Position.X, Position.Y, Position.Z,
        LaneA.Mesh->GetPhysicsLinearVelocity(TEXT("pelvis")).Size(), LaneA.Mesh->GetPhysicsAngularVelocityInRadians(TEXT("pelvis")).Size(), Pelvis.X, Pelvis.Y, Pelvis.Z);
    if (bContinuousAuto && ElapsedSeconds >= 18.5 && !bRunComplete)
    {
        bRunComplete = true;
        ExportContinuousEvidence();
        if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) PC->ConsoleCommand(TEXT("quit"));
    }
}

void APhysicalGetUpRealtimeActor::ExportContinuousEvidence()
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PhysicsResearch"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    FFileHelper::SaveStringToFile(ContinuousTrace, *FPaths::Combine(Directory, TEXT("PhysicalGetUpContinuous.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("branch"), FPhysicalGetUpDecision::ToString(LaneA.Branch));
    Json->SetNumberField(TEXT("recovery_start_s"), LaneA.RecoveryStartSeconds);
    Json->SetNumberField(TEXT("recovery_complete_s"), ContinuousRecoveredAt);
    Json->SetNumberField(TEXT("upright_stable_s"), ContinuousUprightStableSeconds);
    Json->SetNumberField(TEXT("moved_after_recovery_cm"), ContinuousMovedAfterRecovery);
    Json->SetNumberField(TEXT("early_request_rejected"), ContinuousRejectedRequests);
    Json->SetNumberField(TEXT("remaining_controls"), LaneA.ControlCount);
    Json->SetBoolField(TEXT("pelvis_simulating"), LaneA.Mesh->IsSimulatingPhysics(TEXT("pelvis")));
    Json->SetBoolField(TEXT("movement_walking"), ContinuousCharacter->GetCharacterMovement()->MovementMode == MOVE_Walking);
    Json->SetBoolField(TEXT("passed"), ContinuousRecoveredAt > 0 && ContinuousMovedAfterRecovery > 50 && LaneA.ControlCount == 0
        && !LaneA.Mesh->IsSimulatingPhysics(TEXT("pelvis")) && ContinuousCharacter->GetCharacterMovement()->MovementMode == MOVE_Walking);
    FString Text;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    FJsonSerializer::Serialize(Json, Writer);
    FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Directory, TEXT("PhysicalGetUpContinuous.json")));
    UE_LOG(LogTemp, Display, TEXT("ContinuousGetUp %s"), *Text);
}
