import json
import os

import unreal


MAP_PACKAGE = "/PhysicsInverseDesign/Maps/L_InverseSpringChaosReplay"
REPORT_PATH = os.path.join(
    unreal.Paths.project_dir(), "Docs", "Validation", "M5", "inverse_spring_map_setup.json"
)
MAP_FILE = os.path.join(
    unreal.Paths.project_dir(),
    "Plugins",
    "GameFeatures",
    "PhysicsInverseDesign",
    "Content",
    "Maps",
    "L_InverseSpringChaosReplay.umap",
)
report = {"map": MAP_PACKAGE, "actor": "", "game_mode": "", "saved": False, "errors": []}

if unreal.EditorAssetLibrary.does_asset_exist(MAP_PACKAGE) or os.path.isfile(MAP_FILE):
    if not unreal.EditorLevelLibrary.load_level(MAP_PACKAGE):
        report["errors"].append("EditorLevelLibrary.load_level failed")
    else:
        for existing_actor in unreal.EditorLevelLibrary.get_all_level_actors():
            if isinstance(existing_actor, unreal.InverseSpringRealtimeActor):
                unreal.EditorLevelLibrary.destroy_actor(existing_actor)
elif not unreal.EditorLevelLibrary.new_level(MAP_PACKAGE):
    report["errors"].append("EditorLevelLibrary.new_level failed")

if not report["errors"]:
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.InverseSpringRealtimeActor,
        unreal.Vector(0.0, 0.0, 0.0),
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    if actor is None:
        report["errors"].append("Failed to spawn InverseSpringRealtimeActor")
    else:
        actor.set_actor_label("M5_可微弹簧反演与Chaos重放")
        report["actor"] = actor.get_path_name()

    world = unreal.EditorLevelLibrary.get_editor_world()
    if world is None:
        report["errors"].append("Editor world unavailable")
    else:
        world.get_world_settings().set_editor_property(
            "default_game_mode", unreal.InverseSpringRealtimeGameMode
        )
        report["game_mode"] = "/Script/PhysicsInverseDesignRuntime.InverseSpringRealtimeGameMode"

    report["saved"] = unreal.EditorLevelLibrary.save_current_level()
    if not report["saved"]:
        report["errors"].append("Failed to save current level")

os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
with open(REPORT_PATH, "w", encoding="utf-8") as handle:
    json.dump(report, handle, ensure_ascii=False, indent=2)

unreal.log(f"SIGGRAPH M5 inverse spring map setup complete: {REPORT_PATH}")
if report["errors"]:
    raise RuntimeError("; ".join(report["errors"]))
