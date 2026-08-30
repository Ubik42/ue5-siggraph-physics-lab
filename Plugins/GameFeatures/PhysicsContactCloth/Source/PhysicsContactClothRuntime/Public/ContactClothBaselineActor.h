#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ContactClothBaselineActor.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UPhysicsExperimentComponent;
class UPointLightComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

UCLASS(DisplayName = "接触布料 Chaos 基线")
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothBaselineActor : public AActor
{
	GENERATED_BODY()

public:
	AContactClothBaselineActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPhysicsExperimentComponent* GetExperimentComponent() const { return ExperimentComponent; }
	USkeletalMeshComponent* GetAssassinMesh() const { return AssassinMesh; }
	bool IsAssetLoaded() const { return bAssetLoaded; }
	bool IsClothSimulationCreated() const { return bClothSimulationCreated; }
	bool HasRuntimeClothData() const { return bHasRuntimeClothData; }
	int32 GetClothingAssetCount() const { return ClothingAssetCount; }
	int32 GetSimulatedParticleCount() const { return SimulatedParticleCount; }
	double GetMaximumObservedDisplacementCm() const { return MaximumObservedDisplacementCm; }
	const FString& GetGateStatusText() const { return GateStatusText; }

protected:
	UFUNCTION()
	void HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex);

private:
	void ConfigureAssassinAsset();
	void GatherClothEvidence(int64 StepIndex);
	void CompleteAssetGate();
	void CaptureScreenshotAndScheduleExit();
	void RequestOwnedProcessExit();
	void WriteAssetGateReport(bool bPassed) const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPhysicsExperimentComponent> ExperimentComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> AssassinMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> FloorMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> ContactObstacle;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> DirectionalLight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> PointLight;

	TMap<int32, TArray<FVector3f, TAlignedHeapAllocator<16>>> ReferencePositions;
	FTimerHandle CaptureTimerHandle;
	FTimerHandle ExitTimerHandle;
	FString GateStatusText = TEXT("等待加载 Assassin 资产");
	FString LastJsonExportPath;
	FString LastCsvExportPath;
	bool bAssetLoaded = false;
	bool bClothSimulationCreated = false;
	bool bHasRuntimeClothData = false;
	bool bRunComplete = false;
	bool bCaptureRequested = false;
	bool bAutoQuitRequested = false;
	int32 ClothingAssetCount = 0;
	int32 SimulatedParticleCount = 0;
	double MaximumObservedDisplacementCm = 0.0;
	static constexpr int64 WarmupSteps = 30;
	static constexpr int64 StepsToRun = 240;
};
