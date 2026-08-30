#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PhysicsResearchTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPhysicsResearchConfigDefaultsTest,
	"PhysicsResearch.Configuration.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPhysicsResearchConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const FPhysicsStepConfig Config;
	TestTrue(TEXT("固定步长必须为正数"), Config.FixedDeltaSeconds > 0.0);
	TestTrue(TEXT("子步数量必须为正数"), Config.Substeps > 0);
	TestTrue(TEXT("求解迭代次数必须为正数"), Config.SolverIterations > 0);
	TestTrue(TEXT("每帧追赶步数必须为正数"), Config.MaxCatchUpSteps > 0);
	return true;
}

#endif
