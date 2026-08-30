#include "PhysicsExperimentComponent.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace PhysicsResearch
{
FString EscapeCsvField(const FString& Value)
{
	FString Escaped = Value;
	Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
	return FString::Printf(TEXT("\"%s\""), *Escaped);
}
}

UPhysicsExperimentComponent::UPhysicsExperimentComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UPhysicsExperimentComponent::BeginPlay()
{
	Super::BeginPlay();
	ResetExperiment();

	if (bAutoStart)
	{
		StartExperiment();
	}
}

void UPhysicsExperimentComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (RunState != EPhysicsExperimentRunState::Running)
	{
		return;
	}

	const double FixedDelta = FMath::Max(StepConfig.FixedDeltaSeconds, UE_DOUBLE_SMALL_NUMBER);
	const int32 MaxCatchUpSteps = FMath::Max(StepConfig.MaxCatchUpSteps, 1);
	AccumulatorSeconds += FMath::Max(static_cast<double>(DeltaTime), 0.0);

	int32 StepsThisFrame = 0;
	while (RunState == EPhysicsExperimentRunState::Running && AccumulatorSeconds >= FixedDelta && StepsThisFrame < MaxCatchUpSteps)
	{
		ExecuteFixedStep(FixedDelta);
		AccumulatorSeconds -= FixedDelta;
		++StepsThisFrame;
	}

	if (RunState == EPhysicsExperimentRunState::Running && AccumulatorSeconds >= FixedDelta)
	{
		AccumulatorSeconds = FMath::Fmod(AccumulatorSeconds, FixedDelta);
		++Stats.DroppedCatchUpFrames;
	}
}

void UPhysicsExperimentComponent::StartExperiment()
{
	SetRunState(EPhysicsExperimentRunState::Running);
}

void UPhysicsExperimentComponent::StopExperiment()
{
	SetRunState(EPhysicsExperimentRunState::Stopped);
}

void UPhysicsExperimentComponent::PauseExperiment()
{
	if (RunState == EPhysicsExperimentRunState::Running)
	{
		SetRunState(EPhysicsExperimentRunState::Paused);
	}
}

void UPhysicsExperimentComponent::ResetExperiment()
{
	SetRunState(EPhysicsExperimentRunState::Stopped);
	AccumulatorSeconds = 0.0;
	AccumulatedStepWallMilliseconds = 0.0;
	Stats = FPhysicsExperimentStats();
	MetricSamples.Reset();
	DeterministicRandomStream.Initialize(DeterministicSeed);
}

void UPhysicsExperimentComponent::StepExperimentOnce()
{
	if (RunState != EPhysicsExperimentRunState::Running)
	{
		SetRunState(EPhysicsExperimentRunState::Paused);
	}

	const double FixedDelta = FMath::Max(StepConfig.FixedDeltaSeconds, UE_DOUBLE_SMALL_NUMBER);
	ExecuteFixedStep(FixedDelta);
}

void UPhysicsExperimentComponent::RecordMetric(FName MetricName, double Value, const FString& Unit)
{
	if (MetricName.IsNone() || !FMath::IsFinite(Value))
	{
		return;
	}

	FPhysicsMetricSample& Sample = MetricSamples.Emplace_GetRef();
	Sample.MetricName = MetricName;
	Sample.Value = Value;
	Sample.Unit = Unit;
	Sample.StepIndex = Stats.StepIndex;
	Sample.SimulatedSeconds = Stats.SimulatedSeconds;

	const int32 SafeLimit = FMath::Max(MaxMetricSamples, 1);
	const int32 Overflow = MetricSamples.Num() - SafeLimit;
	if (Overflow > 0)
	{
		MetricSamples.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
	Stats.RecordedMetricSamples = MetricSamples.Num();
}

bool UPhysicsExperimentComponent::ExportMetrics(
	const FString& RequestedBaseName,
	EPhysicsMetricExportFormat Format,
	FString& OutAbsolutePath) const
{
	FString SafeBaseName = FPaths::GetBaseFilename(FPaths::GetCleanFilename(RequestedBaseName));
	if (SafeBaseName.IsEmpty())
	{
		SafeBaseName = TEXT("PhysicsMetrics");
	}

	const FString OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PhysicsResearch"));
	if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
	{
		return false;
	}

	const FString Extension = Format == EPhysicsMetricExportFormat::Csv ? TEXT("csv") : TEXT("json");
	OutAbsolutePath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(OutputDirectory, SafeBaseName + TEXT(".") + Extension));

	if (Format == EPhysicsMetricExportFormat::Csv)
	{
		FString Csv = TEXT("metric,value,unit,step_index,simulated_seconds\n");
		for (const FPhysicsMetricSample& Sample : MetricSamples)
		{
			Csv += FString::Printf(
				TEXT("%s,%.17g,%s,%lld,%.17g\n"),
				*PhysicsResearch::EscapeCsvField(Sample.MetricName.ToString()),
				Sample.Value,
				*PhysicsResearch::EscapeCsvField(Sample.Unit),
				Sample.StepIndex,
				Sample.SimulatedSeconds);
		}
		return FFileHelper::SaveStringToFile(Csv, *OutAbsolutePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	TArray<TSharedPtr<FJsonValue>> JsonSamples;
	JsonSamples.Reserve(MetricSamples.Num());
	for (const FPhysicsMetricSample& Sample : MetricSamples)
	{
		TSharedRef<FJsonObject> JsonSample = MakeShared<FJsonObject>();
		JsonSample->SetStringField(TEXT("metric"), Sample.MetricName.ToString());
		JsonSample->SetNumberField(TEXT("value"), Sample.Value);
		JsonSample->SetStringField(TEXT("unit"), Sample.Unit);
		JsonSample->SetNumberField(TEXT("step_index"), static_cast<double>(Sample.StepIndex));
		JsonSample->SetNumberField(TEXT("simulated_seconds"), Sample.SimulatedSeconds);
		JsonSamples.Add(MakeShared<FJsonValueObject>(JsonSample));
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("case"), StaticEnum<EPhysicsResearchCase>()->GetNameStringByValue(static_cast<int64>(ExperimentCase)));
	RootObject->SetStringField(TEXT("backend"), StaticEnum<EPhysicsSolverBackend>()->GetNameStringByValue(static_cast<int64>(SolverBackend)));
	RootObject->SetNumberField(TEXT("deterministic_seed"), DeterministicSeed);
	RootObject->SetArrayField(TEXT("samples"), JsonSamples);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		return false;
	}
	return FFileHelper::SaveStringToFile(Json, *OutAbsolutePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

double UPhysicsExperimentComponent::GetNextRandomFraction()
{
	return static_cast<double>(DeterministicRandomStream.FRand());
}

void UPhysicsExperimentComponent::ExecuteFixedStep(double FixedDeltaSeconds)
{
	const uint64 StartCycles = FPlatformTime::Cycles64();
	++Stats.StepIndex;
	Stats.SimulatedSeconds += FixedDeltaSeconds;
	AdvanceExperiment(FixedDeltaSeconds, Stats.StepIndex);
	OnExperimentStep.Broadcast(FixedDeltaSeconds, Stats.StepIndex);

	Stats.LastStepWallMilliseconds = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles);
	AccumulatedStepWallMilliseconds += Stats.LastStepWallMilliseconds;
	Stats.AverageStepWallMilliseconds = AccumulatedStepWallMilliseconds / static_cast<double>(Stats.StepIndex);
}

void UPhysicsExperimentComponent::AdvanceExperiment_Implementation(double FixedDeltaSeconds, int64 StepIndex)
{
}

void UPhysicsExperimentComponent::SetRunState(EPhysicsExperimentRunState NewState)
{
	if (RunState == NewState)
	{
		return;
	}
	RunState = NewState;
	OnExperimentStateChanged.Broadcast(RunState);
}
