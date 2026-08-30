#include "PhysicsControlCharacterGameMode.h"

#include "PhysicsControlCharacterHUD.h"

APhysicsControlCharacterGameMode::APhysicsControlCharacterGameMode()
{
    HUDClass = APhysicsControlCharacterHUD::StaticClass();
    DefaultPawnClass = nullptr;
}
