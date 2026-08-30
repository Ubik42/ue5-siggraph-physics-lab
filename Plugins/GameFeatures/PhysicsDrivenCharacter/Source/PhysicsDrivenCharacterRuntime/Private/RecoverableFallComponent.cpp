#include "RecoverableFallComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Character/LyraHealthComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayTagContainer.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "NativeGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace RecoverableFallTags
{
UE_DEFINE_GAMEPLAY_TAG_STATIC(Status, "Status.PhysicsRecoverableFall");
UE_DEFINE_GAMEPLAY_TAG_STATIC(Falling, "Status.PhysicsRecoverableFall.Falling");
UE_DEFINE_GAMEPLAY_TAG_STATIC(Settling, "Status.PhysicsRecoverableFall.Settling");
UE_DEFINE_GAMEPLAY_TAG_STATIC(Recovering, "Status.PhysicsRecoverableFall.Recovering");
}

namespace
{
bool bNetworkProbeExitScheduled = false;
}

URecoverableFallComponent::URecoverableFallComponent()
{
    SetIsReplicatedByDefault(true);
}

void URecoverableFallComponent::BeginPlay()
{
    Super::BeginPlay();
    const ACharacter* Character = Cast<ACharacter>(GetOwner());
    const bool bHasHealth = ULyraHealthComponent::FindHealthComponent(GetOwner()) != nullptr;
    const bool bHasASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()) != nullptr;
    UE_LOG(LogTemp, Display, TEXT("RecoverableFallInjection Owner=%s Class=%s Authority=%d NetMode=%d Health=%d ASC=%d Movement=%d"),
        *GetNameSafe(GetOwner()), *GetNameSafe(GetOwner() ? GetOwner()->GetClass() : nullptr), GetOwner() && GetOwner()->HasAuthority() ? 1 : 0,
        GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1, bHasHealth ? 1 : 0, bHasASC ? 1 : 0,
        Character && Character->GetCharacterMovement() ? 1 : 0);
    if (FParse::Param(FCommandLine::Get(), TEXT("PhysicsDrivenCharacterNetworkProbe"))) ScheduleNetworkProbe();
}

void URecoverableFallComponent::ScheduleNetworkProbe()
{
    TWeakObjectPtr<URecoverableFallComponent> WeakThis(this);
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis]()
        {
            if (!WeakThis.IsValid()) return;
            FTimerHandle Handle;
            WeakThis->GetWorld()->GetTimerManager().SetTimer(Handle, [WeakThis]()
            {
                if (WeakThis.IsValid()) WeakThis->BeginRecoverableFall();
            }, 2.0f, false);
            FTimerHandle GroundHandle;
            WeakThis->GetWorld()->GetTimerManager().SetTimer(GroundHandle, [WeakThis]()
            {
                if (WeakThis.IsValid()) WeakThis->UpdateServerObservation(true, -0.8, 0.0, 0.0, 0.0);
            }, 2.5f, false);
            FTimerHandle StableHandle;
            WeakThis->GetWorld()->GetTimerManager().SetTimer(StableHandle, [WeakThis]()
            {
                if (WeakThis.IsValid()) WeakThis->UpdateServerObservation(true, -0.8, 0.0, 0.0, 0.5);
            }, 4.0f, false);
            FTimerHandle FinishHandle;
            WeakThis->GetWorld()->GetTimerManager().SetTimer(FinishHandle, [WeakThis]()
            {
                if (WeakThis.IsValid()) WeakThis->FinishRecovery();
            }, 5.0f, false);
        });
    }
    else
    {
        FTimerHandle RequestHandle;
        GetWorld()->GetTimerManager().SetTimer(RequestHandle, [WeakThis]()
        {
            if (!WeakThis.IsValid()) return;
            const APawn* Pawn = Cast<APawn>(WeakThis->GetOwner());
            if (Pawn && Pawn->IsLocallyControlled())
            {
                UE_LOG(LogTemp, Display, TEXT("RecoverableFallNetworkProbe ClientRequest Owner=%s"), *GetNameSafe(Pawn));
                WeakThis->ServerRequestRecovery();
            }
        }, 3.0f, false);
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("PhysicsDrivenCharacterNetworkProbeAutoExit")) && !bNetworkProbeExitScheduled)
    {
        bNetworkProbeExitScheduled = true;
        FTimerHandle ExitHandle;
        const float ExitDelay = GetWorld()->GetNetMode() == NM_DedicatedServer ? 60.0f : 20.0f;
        TWeakObjectPtr<UWorld> WeakWorld(GetWorld());
        GetWorld()->GetTimerManager().SetTimer(ExitHandle, [WeakWorld]()
        {
            if (!WeakWorld.IsValid()) return;
            GEngine->Exec(WeakWorld.Get(), TEXT("NETPROFILE DISABLE"));
            UE_LOG(LogTemp, Display, TEXT("RecoverableFallNetworkProbe CleanExit NetMode=%d"), static_cast<int32>(WeakWorld->GetNetMode()));
            FPlatformMisc::RequestExit(false);
        }, ExitDelay, false);
    }
}

void URecoverableFallComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(URecoverableFallComponent, FallState);
    DOREPLIFETIME(URecoverableFallComponent, RecoveryBranch);
}

bool URecoverableFallComponent::IsDeathActive() const
{
    const ULyraHealthComponent* Health = ULyraHealthComponent::FindHealthComponent(GetOwner());
    return Health && Health->IsDeadOrDying();
}

bool URecoverableFallComponent::BeginRecoverableFall()
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || IsDeathActive()) return false;
    if (FallState != ERecoverableFallState::Balanced && FallState != ERecoverableFallState::Recovered) return false;
    RecoveryBranch = EPhysicalGetUpBranch::Pending;
    bRecoveryRequested = false;
    SetFallState(ERecoverableFallState::Falling);
    return true;
}

void URecoverableFallComponent::UpdateServerObservation(const bool bGrounded, const double FacingUpDot, const double LinearSpeedCmPerSecond, const double AngularSpeedRadPerSecond, const double StableSeconds)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    FRecoverableFallAuthorityInput Input;
    Input.bHasAuthority = true;
    Input.bDeathActive = IsDeathActive();
    Input.bGrounded = bGrounded;
    Input.bRecoveryRequested = bRecoveryRequested;
    Input.FacingUpDot = FacingUpDot;
    Input.LinearSpeed = LinearSpeedCmPerSecond;
    Input.AngularSpeed = AngularSpeedRadPerSecond;
    Input.StableSeconds = StableSeconds;
    EPhysicalGetUpBranch NewBranch = EPhysicalGetUpBranch::Pending;
    const ERecoverableFallState NewState = FRecoverableFallAuthorityPolicy::Evaluate(FallState, Input, NewBranch);
    if (NewBranch == EPhysicalGetUpBranch::Front || NewBranch == EPhysicalGetUpBranch::Back) RecoveryBranch = NewBranch;
    SetFallState(NewState);
}

void URecoverableFallComponent::ServerRequestRecovery_Implementation()
{
    if (IsDeathActive() || (FallState != ERecoverableFallState::Falling && FallState != ERecoverableFallState::Settling)) return;
    bRecoveryRequested = true;
    UE_LOG(LogTemp, Display, TEXT("RecoverableFallNetworkProbe ServerAcceptedRequest Owner=%s State=%s"), *GetNameSafe(GetOwner()), FRecoverableFallAuthorityPolicy::ToString(FallState));
}

bool URecoverableFallComponent::FinishRecovery()
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || IsDeathActive() || FallState != ERecoverableFallState::Recovering) return false;
    SetFallState(ERecoverableFallState::Recovered);
    bRecoveryRequested = false;
    return true;
}

void URecoverableFallComponent::FailRecovery()
{
    if (GetOwner() && GetOwner()->HasAuthority()) SetFallState(ERecoverableFallState::Failed);
}

void URecoverableFallComponent::SetFallState(const ERecoverableFallState NewState)
{
    if (FallState == NewState) return;
    const ERecoverableFallState OldState = FallState;
    FallState = NewState;
    UE_LOG(LogTemp, Display, TEXT("RecoverableFallNetworkProbe ServerTransition Owner=%s %s->%s Branch=%s"), *GetNameSafe(GetOwner()),
        FRecoverableFallAuthorityPolicy::ToString(OldState), FRecoverableFallAuthorityPolicy::ToString(NewState), FPhysicalGetUpDecision::ToString(RecoveryBranch));
    ApplyStateToLyra(OldState, NewState);
    OnFallStateChanged.Broadcast(OldState, NewState);
    if (AActor* Owner = GetOwner()) Owner->ForceNetUpdate();
}

void URecoverableFallComponent::OnRep_FallState(const ERecoverableFallState OldState)
{
    UE_LOG(LogTemp, Display, TEXT("RecoverableFallNetworkProbe ClientRep Owner=%s %s->%s Branch=%s"), *GetNameSafe(GetOwner()),
        FRecoverableFallAuthorityPolicy::ToString(OldState), FRecoverableFallAuthorityPolicy::ToString(FallState), FPhysicalGetUpDecision::ToString(RecoveryBranch));
    ApplyStateToLyra(OldState, FallState);
    OnFallStateChanged.Broadcast(OldState, FallState);
}

void URecoverableFallComponent::ApplyStateToLyra(const ERecoverableFallState OldState, const ERecoverableFallState NewState)
{
    UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
    if (ASC)
    {
        ASC->SetLooseGameplayTagCount(RecoverableFallTags::Status, NewState == ERecoverableFallState::Balanced || NewState == ERecoverableFallState::Recovered ? 0 : 1);
        ASC->SetLooseGameplayTagCount(RecoverableFallTags::Falling, NewState == ERecoverableFallState::Falling ? 1 : 0);
        ASC->SetLooseGameplayTagCount(RecoverableFallTags::Settling, NewState == ERecoverableFallState::Settling ? 1 : 0);
        ASC->SetLooseGameplayTagCount(RecoverableFallTags::Recovering, NewState == ERecoverableFallState::Recovering ? 1 : 0);
    }
    ACharacter* Character = Cast<ACharacter>(GetOwner());
    if (!Character) return;
    UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
    if (!Movement) return;
    if (NewState == ERecoverableFallState::Falling) Movement->DisableMovement();
    if (NewState == ERecoverableFallState::Recovered && !IsDeathActive()) Movement->SetMovementMode(MOVE_Walking);
}
