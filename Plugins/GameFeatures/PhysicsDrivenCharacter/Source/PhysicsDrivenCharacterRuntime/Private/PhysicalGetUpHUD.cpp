#include "PhysicalGetUpHUD.h"

#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "PhysicalGetUpRealtimeActor.h"

void APhysicalGetUpHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
    const APhysicalGetUpRealtimeActor* Experiment = nullptr;
    for (TActorIterator<APhysicalGetUpRealtimeActor> It(GetWorld()); It; ++It) { Experiment = *It; break; }
    if (!Experiment) return;
    const float X = 34.0f;
    float Y = 28.0f;
    auto Line = [&](const FString& Text, const FLinearColor Color = FLinearColor::White, const float Scale = 1.0f)
    {
        DrawText(Text, Color, X, Y, nullptr, Scale, false);
        Y += 24.0f * Scale;
    };
    Line(TEXT("Case 05 · Chaos 姿态分支 → PhysicsControl → ALS 起身"), FLinearColor(0.2f, 0.85f, 1.0f), 1.25f);
    Line(Experiment->GetStatusLine(), Experiment->IsRunComplete() ? FLinearColor::Green : FLinearColor::Yellow);
    Line(Experiment->GetLaneLine(0));
    Line(Experiment->GetLaneLine(1));
    Line(Experiment->GetLaneLine(2), FLinearColor(1.0f, 0.65f, 0.25f));
    Y += 8.0f;
    if (Experiment->IsInteractiveMode())
    {
        Line(TEXT("按键：1 俯卧 · 2 仰卧 · P 施加物理扰动 · Space 请求起身 · R 重置当前姿态"), FLinearColor(0.35f, 1.0f, 0.55f), 0.9f);
        Line(Experiment->GetInteractionLine(), FLinearColor(0.7f, 0.95f, 0.75f), 0.85f);
    }
    Line(TEXT("判据：grounded + v≤18 cm/s + ω≤0.9 rad/s + 稳定 0.35 s；侧躺/高速必须拒绝"), FLinearColor(0.8f, 0.85f, 0.9f), 0.85f);
    Line(TEXT("归因：ALS 提供动作；Epic PhysicsControl 提供物理驱动；分支、相位、阈值与证据为个人实现"), FLinearColor(0.8f, 0.85f, 0.9f), 0.85f);
}
