#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContactClothAssetConfigurationTest,
	"PhysicsResearch.ContactCloth.AssetConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContactClothAssetConfigurationTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Assassin/Mesh/SKM_Assassin_Skin1.SKM_Assassin_Skin1"));
	TestNotNull(TEXT("能够加载 Assassin Skin1 Skeletal Mesh"), Mesh);
	if (!Mesh)
	{
		return false;
	}

	TestTrue(TEXT("Skeletal Mesh 声明活动 Clothing Asset"), Mesh->HasActiveClothingAssets());
	TestTrue(TEXT("至少绑定一份 Clothing Asset"), Mesh->GetMeshClothingAssets().Num() > 0);
	return true;
}

#endif
