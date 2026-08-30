#pragma once

#include "ContactClothGpuJacobi.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ContactClothGpuRealtimeActor.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UPhysicsExperimentComponent;
class UPointLightComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

UCLASS(DisplayName = "接触布料 GPU 逐帧运行")
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothGpuRealtimeActor : public AActor
{
	GENERATED_BODY()

public:
	AContactClothGpuRealtimeActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPhysicsExperimentComponent* GetExperimentComponent() const { return ExperimentComponent; }
	FContactClothGpuRealtimeStats GetGpuStats() const { return GpuSimulation.GetStats(); }
	int32 GetParticleCount() const { return LatestPositions.Num(); }
	int32 GetIterationCount() const { return Settings.Iterations; }
	int64 GetLatestReadbackStep() const { return LatestReadbackStep; }
	double GetMaximumEdgeErrorCm() const { return MaximumEdgeErrorCm; }
	double GetMaximumCapsulePenetrationCm() const { return MaximumCapsulePenetrationCm; }
	double GetBottomDisplacementCm() const { return BottomDisplacementCm; }
	int32 GetCollisionSphereCount() const { return CollisionSpheres.Num(); }
	int32 GetCollisionCapsuleCount() const { return CollisionCapsules.Num(); }
	int64 GetPoseUpdateCount() const { return PoseUpdateCount; }
	int32 GetModalModeCount() const { return Settings.ModalModeCount; }
	const FString& GetStatusText() const { return StatusText; }
	bool DidRunPass() const { return bRunComplete && bRunPassed; }

protected:
	UFUNCTION()
	void HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex);

private:
	void BuildInitialGrid();
	bool ConfigureModalSubspace();
	bool UpdateAssassinPoseColliders();
	void ProcessLatestReadback();
	void DrawClothDebug() const;
	void CompleteRun();
	void WriteRunReport() const;
	void CaptureScreenshotAndScheduleExit();
	void RequestOwnedProcessExit();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPhysicsExperimentComponent> ExperimentComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> FloorMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> CapsuleMarker;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> AssassinMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> DirectionalLight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> PointLight;

	FContactClothGpuRealtimeSimulation GpuSimulation;
	FContactClothGpuRealtimeSettings Settings;
	TArray<FVector3f> InitialPositions;
	TArray<FVector3f> LatestPositions;
	TArray<FContactClothGpuSphere> CollisionSpheres;
	TArray<FContactClothGpuCapsule> CollisionCapsules;
	FTimerHandle CaptureTimerHandle;
	FTimerHandle ExitTimerHandle;
	FString StatusText = TEXT("初始化持久 GPU Buffer");
	int64 LatestReadbackStep = 0;
	double MaximumEdgeErrorCm = 0.0;
	double MaximumCapsulePenetrationCm = 0.0;
	double BottomDisplacementCm = 0.0;
	int64 PoseUpdateCount = 0;
	bool bAwaitingFinalReadback = false;
	bool bRunComplete = false;
	bool bRunPassed = false;
	bool bCaptureRequested = false;
	bool bAutoQuitRequested = false;
	static constexpr int64 StepsToRun = 300;
};
