#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PhysicsLabM1Actor.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UPhysicsExperimentComponent;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

UCLASS(DisplayName = "M1 共享实验验证 Actor")
class PHYSICSRESEARCH_API APhysicsLabM1Actor final : public AActor
{
	GENERATED_BODY()

public:
	APhysicsLabM1Actor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "物理实验")
	UPhysicsExperimentComponent* GetExperimentComponent() const { return ExperimentComponent; }

	UFUNCTION(BlueprintPure, Category = "物理实验")
	bool IsRunComplete() const { return bRunComplete; }

	UFUNCTION(BlueprintPure, Category = "物理实验")
	bool IsPortfolioHubMode() const { return bPortfolioHubMode; }

	UFUNCTION(BlueprintPure, Category = "物理实验")
	const FString& GetHubStatus() const { return HubStatus; }

	UFUNCTION(BlueprintPure, Category = "物理实验")
	const FString& GetLastJsonExportPath() const { return LastJsonExportPath; }

private:
	UFUNCTION()
	void HandleExperimentStep(double FixedDeltaSeconds, int64 StepIndex);

	void HandleInput();
	void LaunchPortfolioCase(int32 CaseIndex);
	void DrawExperimentDebug() const;
	void CompleteRun();
	void CaptureScreenshotAndScheduleExit();
	void RequestOwnedProcessExit();

	UPROPERTY(VisibleAnywhere, Category = "物理实验")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "物理实验")
	TObjectPtr<UPhysicsExperimentComponent> ExperimentComponent;

	UPROPERTY(VisibleAnywhere, Category = "物理实验|展示")
	TObjectPtr<UStaticMeshComponent> FloorMesh;

	UPROPERTY(VisibleAnywhere, Category = "物理实验|展示")
	TObjectPtr<UStaticMeshComponent> ProbeMesh;

	UPROPERTY(VisibleAnywhere, Category = "物理实验|展示")
	TObjectPtr<UStaticMeshComponent> ObstacleMeshA;

	UPROPERTY(VisibleAnywhere, Category = "物理实验|展示")
	TObjectPtr<UStaticMeshComponent> ObstacleMeshB;

	UPROPERTY(VisibleAnywhere, Category = "物理实验|展示")
	TObjectPtr<UCameraComponent> CameraComponent;

	UPROPERTY(VisibleAnywhere, Category = "物理实验|展示")
	TObjectPtr<UDirectionalLightComponent> DirectionalLight;

	UPROPERTY(VisibleAnywhere, Category = "物理实验|展示")
	TObjectPtr<UPointLightComponent> AccentLight;

	UPROPERTY(EditAnywhere, Category = "物理实验", meta = (ClampMin = "1"))
	int32 StepsToRun = 180;

	UPROPERTY(EditAnywhere, Category = "物理实验|展示")
	bool bEnableDebugDraw = true;

	bool bRunComplete = false;
	bool bCaptureRequested = false;
	bool bAutoQuitRequested = false;
	bool bPortfolioHubMode = false;
	FString HubStatus = TEXT("请选择 1–5 进入作品");
	FString LastJsonExportPath;
	FString LastCsvExportPath;
	FTimerHandle CaptureTimerHandle;
	FTimerHandle ExitTimerHandle;
};
