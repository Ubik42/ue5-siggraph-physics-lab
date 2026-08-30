#pragma once

#include "ContactClothGpuJacobi.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ContactClothGpuValidationActor.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UPhysicsExperimentComponent;
class UPointLightComponent;
class USceneComponent;

UCLASS(DisplayName = "接触布料 GPU Jacobi 验证")
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothGpuValidationActor : public AActor
{
	GENERATED_BODY()

public:
	AContactClothGpuValidationActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPhysicsExperimentComponent* GetExperimentComponent() const { return ExperimentComponent; }
	const FContactClothGpuJacobiResult& GetGpuResult() const { return GpuResult; }
	int32 GetParticleCount() const { return InputPositions.Num(); }
	int32 GetIterations() const { return Settings.Iterations; }
	bool DidValidationPass() const { return bValidationPassed; }
	const FString& GetStatusText() const { return StatusText; }

private:
	void BuildInputGrid();
	void DrawGrid(TConstArrayView<FVector3f> Positions, const FVector& DisplayOffset, const FColor& LineColor) const;
	void DrawValidationDebug() const;
	void WriteRunReport() const;
	void CaptureScreenshotAndScheduleExit();
	void RequestOwnedProcessExit();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPhysicsExperimentComponent> ExperimentComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> DirectionalLight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> PointLight;

	FContactClothGpuJacobiSettings Settings;
	FContactClothGpuJacobiResult GpuResult;
	TArray<FVector3f> InputPositions;
	FTimerHandle CaptureTimerHandle;
	FTimerHandle ExitTimerHandle;
	FString StatusText = TEXT("等待 GPU Jacobi 调度");
	bool bValidationPassed = false;
	bool bCaptureRequested = false;
	bool bAutoQuitRequested = false;
};
