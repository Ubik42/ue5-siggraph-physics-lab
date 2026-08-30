#pragma once

#include "CoreMinimal.h"
#include "PhysicsResearchTypes.generated.h"

UENUM(BlueprintType)
enum class EPhysicsResearchCase : uint8
{
	ContactCloth UMETA(DisplayName = "无穿透接触与布料"),
	MultiscaleSoftBody UMETA(DisplayName = "多尺度软体"),
	FlowMapFluid UMETA(DisplayName = "Flow Map 流体"),
	DifferentiableInverseDesign UMETA(DisplayName = "可微物理反向设计"),
	PhysicsDrivenCharacter UMETA(DisplayName = "物理控制角色")
};

UENUM(BlueprintType)
enum class EPhysicsSolverBackend : uint8
{
	ChaosBaseline UMETA(DisplayName = "Chaos 基线"),
	ResearchSolver UMETA(DisplayName = "论文复现求解器"),
	/** 用于 Chaos 没有同类求解器时的诚实数值 A/B；不得显示为 Chaos。 */
	NumericalBaseline UMETA(DisplayName = "数值基线（非 Chaos）")
};

UENUM(BlueprintType)
enum class EPhysicsExperimentRunState : uint8
{
	Stopped UMETA(DisplayName = "已停止"),
	Running UMETA(DisplayName = "运行中"),
	Paused UMETA(DisplayName = "已暂停")
};

UENUM(BlueprintType)
enum class EPhysicsMetricExportFormat : uint8
{
	Csv UMETA(DisplayName = "CSV"),
	Json UMETA(DisplayName = "JSON")
};

USTRUCT(BlueprintType)
struct PHYSICSRESEARCH_API FPhysicsStepConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "步进", meta = (ClampMin = "0.0001", Units = "s"))
	double FixedDeltaSeconds = 1.0 / 60.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "步进", meta = (ClampMin = "1", ClampMax = "64"))
	int32 Substeps = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "求解", meta = (ClampMin = "1", ClampMax = "1024"))
	int32 SolverIterations = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "步进", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxCatchUpSteps = 4;
};

USTRUCT(BlueprintType)
struct PHYSICSRESEARCH_API FPhysicsExperimentStats
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "统计")
	int64 StepIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "统计", meta = (Units = "s"))
	double SimulatedSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "统计")
	int32 DroppedCatchUpFrames = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "统计")
	int32 RecordedMetricSamples = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "统计", meta = (Units = "ms"))
	double LastStepWallMilliseconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "统计", meta = (Units = "ms"))
	double AverageStepWallMilliseconds = 0.0;
};

USTRUCT(BlueprintType)
struct PHYSICSRESEARCH_API FPhysicsMetricSample
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "指标")
	FName MetricName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "指标")
	double Value = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "指标")
	FString Unit;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "指标")
	int64 StepIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "指标", meta = (Units = "s"))
	double SimulatedSeconds = 0.0;
};
