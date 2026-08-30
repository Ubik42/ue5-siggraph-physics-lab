#include "PhysicsLabM1GameMode.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/SpectatorPawn.h"
#include "PhysicsExperimentComponent.h"
#include "PhysicsLabM1Actor.h"

void APhysicsLabHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}

	const APhysicsLabM1Actor* LabActor = FindLabActor();
	const UPhysicsExperimentComponent* Experiment = LabActor ? LabActor->GetExperimentComponent() : nullptr;
	const FPhysicsExperimentStats Stats = Experiment ? Experiment->GetExperimentStats() : FPhysicsExperimentStats();
	if (LabActor && LabActor->IsPortfolioHubMode())
	{
		DrawRect(FLinearColor(0.008f, 0.014f, 0.03f, 0.96f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
		DrawRect(FLinearColor(0.0f, 0.62f, 0.95f, 1.0f), 46.0f, 42.0f, 8.0f, 92.0f);
		UFont* LargeFont = GEngine ? GEngine->GetLargeFont() : nullptr;
		UFont* MediumFont = GEngine ? GEngine->GetMediumFont() : nullptr;
		DrawText(TEXT("SIGGRAPH PHYSICS LAB"), FLinearColor::White, 76.0f, 46.0f, LargeFont, 1.3f, false);
		DrawText(TEXT("五个 UE 物理论文工程复现 · Chaos 对照 · 可复算证据"), FLinearColor(0.35f, 0.82f, 1.0f), 78.0f, 96.0f, MediumFont, 1.0f, false);

		struct FHubCard { const TCHAR* Key; const TCHAR* Title; const TCHAR* Detail; FLinearColor Color; };
		const FHubCard Cards[] = {
			{ TEXT("1"), TEXT("无穿透接触与 GPU 布料"), TEXT("Assassin · Chaos Cloth / 缩减接触 / NDB / CCD"), FLinearColor(0.96f, 0.30f, 0.18f) },
			{ TEXT("2"), TEXT("GPU 多尺度软体"), TEXT("Dragon · XPBD / 低频模态 / 动态表面 / 接触消融"), FLinearColor(0.72f, 0.30f, 1.0f) },
			{ TEXT("3"), TEXT("Flow Map 流体"), TEXT("Short PIC/APIC / Particle Flow Map / Chaos 运动边界"), FLinearColor(0.05f, 0.72f, 1.0f) },
			{ TEXT("4"), TEXT("可微物理与反向设计"), TEXT("梯度检查 / 参数反演 / Chaos 同参数重放 / 不可辨识失败"), FLinearColor(0.18f, 0.92f, 0.58f) },
			{ TEXT("5"), TEXT("物理驱动角色"), TEXT("Manny / PhysicsControl / ALS 交接 / Lyra 权威网络"), FLinearColor(1.0f, 0.70f, 0.16f) }
		};
		float Y = 158.0f;
		for (const FHubCard& Card : Cards)
		{
			DrawRect(FLinearColor(0.025f, 0.045f, 0.075f, 0.96f), 54.0f, Y, Canvas->ClipX - 108.0f, 104.0f);
			DrawRect(Card.Color, 54.0f, Y, 7.0f, 104.0f);
			DrawRect(FLinearColor(Card.Color.R, Card.Color.G, Card.Color.B, 0.18f), 76.0f, Y + 18.0f, 62.0f, 62.0f);
			DrawText(Card.Key, Card.Color, 96.0f, Y + 31.0f, LargeFont, 1.0f, false);
			DrawText(Card.Title, FLinearColor::White, 162.0f, Y + 17.0f, MediumFont, 1.08f, false);
			DrawText(Card.Detail, FLinearColor(0.65f, 0.74f, 0.86f), 162.0f, Y + 58.0f, MediumFont, 0.86f, false);
			DrawText(TEXT("已验收"), FLinearColor(0.35f, 1.0f, 0.62f), Canvas->ClipX - 155.0f, Y + 38.0f, MediumFont, 0.9f, false);
			Y += 116.0f;
		}
		DrawText(TEXT("数字键 1–5 打开正式作品  ·  H 打开 Case05 ALS 交接  ·  Esc 返回/退出"), FLinearColor(0.95f, 0.78f, 0.28f), 76.0f, Canvas->ClipY - 62.0f, MediumFont, 0.92f, false);
		DrawText(LabActor->GetHubStatus(), FLinearColor(0.35f, 0.82f, 1.0f), Canvas->ClipX - 410.0f, 102.0f, MediumFont, 0.9f, false);
		return;
	}

	DrawRect(FLinearColor(0.01f, 0.02f, 0.04f, 0.88f), 28.0f, 26.0f, 620.0f, 245.0f);
	DrawRect(FLinearColor(0.0f, 0.72f, 0.92f, 1.0f), 28.0f, 26.0f, 7.0f, 245.0f);

	UFont* LargeFont = GEngine ? GEngine->GetLargeFont() : nullptr;
	UFont* MediumFont = GEngine ? GEngine->GetMediumFont() : nullptr;
	DrawText(TEXT("SIGGRAPH 物理实验室 · M1 共享实验框架"), FLinearColor::White, 50.0f, 45.0f, LargeFont, 1.0f, false);

	const FString StateText = Experiment
		? StaticEnum<EPhysicsExperimentRunState>()->GetDisplayNameTextByValue(static_cast<int64>(Experiment->GetRunState())).ToString()
		: TEXT("未找到实验 Actor");
	DrawText(FString::Printf(TEXT("状态：%s    后端：Chaos 基线"), *StateText), FLinearColor(0.25f, 0.9f, 1.0f), 50.0f, 92.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("步数：%lld    模拟时间：%.3f 秒    指标样本：%d"), Stats.StepIndex, Stats.SimulatedSeconds, Stats.RecordedMetricSamples), FLinearColor::White, 50.0f, 126.0f, MediumFont, 1.0f, false);
	DrawText(FString::Printf(TEXT("单步耗时：%.4f ms    平均：%.4f ms"), Stats.LastStepWallMilliseconds, Stats.AverageStepWallMilliseconds), FLinearColor(0.72f, 0.78f, 0.88f), 50.0f, 160.0f, MediumFont, 1.0f, false);
	DrawText(TEXT("空格 暂停/继续   N 单步   R 重置   E 导出   D 调试显示"), FLinearColor(0.95f, 0.72f, 0.2f), 50.0f, 210.0f, MediumFont, 0.9f, false);
}

APhysicsLabM1Actor* APhysicsLabHUD::FindLabActor() const
{
	if (!GetWorld())
	{
		return nullptr;
	}
	for (TActorIterator<APhysicsLabM1Actor> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

APhysicsLabM1GameMode::APhysicsLabM1GameMode()
{
	HUDClass = APhysicsLabHUD::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	bStartPlayersAsSpectators = true;
}
