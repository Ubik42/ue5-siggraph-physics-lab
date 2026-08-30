#pragma once

#include "Components/ActorComponent.h"
#include "PhysicsResearchTypes.h"
#include "PhysicsExperimentComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPhysicsExperimentStepSignature, double, FixedDeltaSeconds, int64, StepIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPhysicsExperimentStateSignature, EPhysicsExperimentRunState, NewState);

UCLASS(ClassGroup = (PhysicsResearch), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent, DisplayName = "物理实验组件"))
class PHYSICSRESEARCH_API UPhysicsExperimentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPhysicsExperimentComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category = "物理实验")
	void StartExperiment();

	UFUNCTION(BlueprintCallable, Category = "物理实验")
	void StopExperiment();

	UFUNCTION(BlueprintCallable, Category = "物理实验")
	void PauseExperiment();

	UFUNCTION(BlueprintCallable, Category = "物理实验")
	void ResetExperiment();

	UFUNCTION(BlueprintCallable, Category = "物理实验")
	void StepExperimentOnce();

	UFUNCTION(BlueprintPure, Category = "物理实验")
	bool IsExperimentRunning() const { return RunState == EPhysicsExperimentRunState::Running; }

	UFUNCTION(BlueprintPure, Category = "物理实验")
	EPhysicsExperimentRunState GetRunState() const { return RunState; }

	UFUNCTION(BlueprintPure, Category = "物理实验")
	const FPhysicsExperimentStats& GetExperimentStats() const { return Stats; }

	UFUNCTION(BlueprintPure, Category = "物理实验|指标")
	TArray<FPhysicsMetricSample> GetMetricSamples() const { return MetricSamples; }

	UFUNCTION(BlueprintCallable, Category = "物理实验|指标")
	void RecordMetric(FName MetricName, double Value, const FString& Unit);

	UFUNCTION(BlueprintCallable, Category = "物理实验|指标")
	bool ExportMetrics(
		const FString& RequestedBaseName,
		EPhysicsMetricExportFormat Format,
		FString& OutAbsolutePath) const;

	UFUNCTION(BlueprintCallable, Category = "物理实验|确定性")
	double GetNextRandomFraction();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "物理实验")
	EPhysicsResearchCase ExperimentCase = EPhysicsResearchCase::ContactCloth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "物理实验")
	EPhysicsSolverBackend SolverBackend = EPhysicsSolverBackend::ChaosBaseline;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "物理实验")
	FPhysicsStepConfig StepConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "物理实验")
	bool bAutoStart = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "物理实验|确定性")
	int32 DeterministicSeed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "物理实验|指标", meta = (ClampMin = "1", ClampMax = "1000000"))
	int32 MaxMetricSamples = 8192;

	UPROPERTY(BlueprintAssignable, Category = "物理实验")
	FPhysicsExperimentStepSignature OnExperimentStep;

	UPROPERTY(BlueprintAssignable, Category = "物理实验")
	FPhysicsExperimentStateSignature OnExperimentStateChanged;

protected:
	UFUNCTION(BlueprintNativeEvent, Category = "物理实验")
	void AdvanceExperiment(double FixedDeltaSeconds, int64 StepIndex);

private:
	void ExecuteFixedStep(double FixedDeltaSeconds);
	void SetRunState(EPhysicsExperimentRunState NewState);

	UPROPERTY(VisibleInstanceOnly, Category = "物理实验")
	FPhysicsExperimentStats Stats;

	UPROPERTY(VisibleInstanceOnly, Category = "物理实验")
	EPhysicsExperimentRunState RunState = EPhysicsExperimentRunState::Stopped;

	UPROPERTY(VisibleInstanceOnly, Category = "物理实验|指标")
	TArray<FPhysicsMetricSample> MetricSamples;

	double AccumulatorSeconds = 0.0;
	double AccumulatedStepWallMilliseconds = 0.0;
	FRandomStream DeterministicRandomStream;
};
