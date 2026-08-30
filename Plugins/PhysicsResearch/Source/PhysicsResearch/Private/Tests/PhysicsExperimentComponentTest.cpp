#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "PhysicsExperimentComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPhysicsExperimentLifecycleTest,
	"PhysicsResearch.Experiment.LifecycleAndMetrics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPhysicsExperimentLifecycleTest::RunTest(const FString& Parameters)
{
	UPhysicsExperimentComponent* Component = NewObject<UPhysicsExperimentComponent>();
	TestNotNull(TEXT("能够创建实验组件"), Component);
	if (!Component)
	{
		return false;
	}

	Component->bAutoStart = false;
	Component->DeterministicSeed = 2026;
	Component->ResetExperiment();
	TestEqual(TEXT("重置后停止"), Component->GetRunState(), EPhysicsExperimentRunState::Stopped);
	TestEqual(TEXT("重置后步数为零"), Component->GetExperimentStats().StepIndex, static_cast<int64>(0));

	const double FirstRandom = Component->GetNextRandomFraction();
	Component->ResetExperiment();
	TestEqual(TEXT("相同种子产生相同首个随机数"), Component->GetNextRandomFraction(), FirstRandom);

	Component->StepExperimentOnce();
	TestEqual(TEXT("单步后保持暂停"), Component->GetRunState(), EPhysicsExperimentRunState::Paused);
	TestEqual(TEXT("单步推进一次"), Component->GetExperimentStats().StepIndex, static_cast<int64>(1));

	Component->RecordMetric(TEXT("ProbeError"), 0.125, TEXT("cm"));
	TestEqual(TEXT("记录一个指标"), Component->GetExperimentStats().RecordedMetricSamples, 1);

	FString ExportPath;
	TestTrue(
		TEXT("能够导出 JSON 指标"),
		Component->ExportMetrics(TEXT("AutomationLifecycle"), EPhysicsMetricExportFormat::Json, ExportPath));
	TestTrue(TEXT("导出文件存在"), IFileManager::Get().FileExists(*ExportPath));
	IFileManager::Get().Delete(*ExportPath, false, true);

	Component->StartExperiment();
	TestEqual(TEXT("开始后运行"), Component->GetRunState(), EPhysicsExperimentRunState::Running);
	Component->PauseExperiment();
	TestEqual(TEXT("暂停生效"), Component->GetRunState(), EPhysicsExperimentRunState::Paused);
	Component->StopExperiment();
	TestEqual(TEXT("停止生效"), Component->GetRunState(), EPhysicsExperimentRunState::Stopped);

	return true;
}

#endif
