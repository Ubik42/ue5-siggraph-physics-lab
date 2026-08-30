#include "PhysicalGetUpGameMode.h"

#include "PhysicalGetUpHUD.h"

APhysicalGetUpGameMode::APhysicalGetUpGameMode()
{
    HUDClass = APhysicalGetUpHUD::StaticClass();
    DefaultPawnClass = nullptr;
}
