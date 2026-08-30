import json
import os
import unreal


MAP_PACKAGE = "/PhysicsContactCloth/Maps/L_ChaosCloth_AssetGate"
REPORT_PATH = os.path.join(
    unreal.Paths.project_dir(),
    "Docs",
    "Validation",
    "M2",
    "map_setup.json",
)

report = {"map": MAP_PACKAGE, "actor": "", "saved": False, "errors": []}

if unreal.EditorAssetLibrary.does_asset_exist(MAP_PACKAGE):
    unreal.EditorAssetLibrary.delete_asset(MAP_PACKAGE)

if not unreal.EditorLevelLibrary.new_level(MAP_PACKAGE):
    report["errors"].append("EditorLevelLibrary.new_level failed")
else:
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.ContactClothBaselineActor,
        unreal.Vector(0.0, 0.0, 0.0),
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    if actor is None:
        report["errors"].append("Failed to spawn ContactClothBaselineActor")
    else:
        actor.set_actor_label("M2_Assassin_ChaosCloth_资产运行门")
        report["actor"] = actor.get_path_name()

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world is None:
        report["errors"].append("Editor world unavailable")
    else:
        world.get_world_settings().set_editor_property(
            "default_game_mode", unreal.ContactClothBaselineGameMode
        )

    report["saved"] = unreal.EditorLevelLibrary.save_current_level()
    if not report["saved"]:
        report["errors"].append("Failed to save current level")

os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
with open(REPORT_PATH, "w", encoding="utf-8") as handle:
    json.dump(report, handle, ensure_ascii=False, indent=2)

unreal.log(f"SIGGRAPH M2 cloth asset gate map setup complete: {REPORT_PATH}")
if report["errors"]:
    raise RuntimeError("; ".join(report["errors"]))
