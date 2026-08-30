#pragma once

#include "ContactClothCpuSolver.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ContactClothCpuReferenceActor.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UPhysicsExperimentComponent;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

UCLASS(DisplayName = "接触布料 CPU 数学参考")
class PHYSICSCONTACTCLOTHRUNTIME_API AContactClothCpuReferenceActor : public AActor
{
	GENERATED_BODY()

public:
	AContactClothCpuReferenceActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPhysicsExperimentComponent* GetExperimentComponent() const { return ExperimentComponent; }
	const FContactClothCpuStepStats& GetLastStepStats() const { return LastStepStats; }
	int32 GetParticleCount() const { return Solver.GetParticles().Num(); }
	int32 GetConstraintCount() const { return Solver.GetConstraints().Num(); }
	int64 GetTotalCollisionProjections() const { return TotalCollisionProjections; }
	const FString& GetStatusText() const { return StatusText; }

protected:
	UFUNCTION()
	void HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex);

private:
	void DrawSolverDebug() const;
	void CompleteRun();
	void WriteRunReport(bool bPassed) const;
	void CaptureScreenshotAndScheduleExit();
	void RequestOwnedProcessExit();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPhysicsExperimentComponent> ExperimentComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> FloorMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> SphereColliderMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> DirectionalLight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> PointLight;

	FContactClothCpuSolver Solver;
	FContactClothSphereCollider SphereCollider;
	FContactClothCpuStepStats LastStepStats;
	FTimerHandle CaptureTimerHandle;
	FTimerHandle ExitTimerHandle;
	FString StatusText = TEXT("初始化 CPU local-global 参考");
	FString LastJsonExportPath;
	FString LastCsvExportPath;
	int64 TotalCollisionProjections = 0;
	double MaximumObservedStretchError = 0.0;
	double MaximumObservedPenetrationCm = 0.0;
	bool bRunComplete = false;
	bool bCaptureRequested = false;
	bool bAutoQuitRequested = false;
	static constexpr int64 StepsToRun = 300;
};
