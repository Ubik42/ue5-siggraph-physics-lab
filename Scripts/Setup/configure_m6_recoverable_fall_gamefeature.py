import json
import os
import unreal

ASSET_PATH = "/PhysicsDrivenCharacter/PhysicsDrivenCharacter"
REPORT_PATH = os.path.join(unreal.Paths.project_dir(), "Docs", "Validation", "M6", "recoverable_fall_gamefeature_setup.json")

report = {"asset": ASSET_PATH, "existing_actions": [], "saved": False, "errors": []}
try:
    data = unreal.load_asset(ASSET_PATH)
    if not data:
        raise RuntimeError("GameFeatureData asset not found")
    actions = list(data.get_editor_property("actions"))
    report["existing_actions"] = [action.get_class().get_name() for action in actions if action]
    add_components_class = getattr(unreal, "PhysicsDrivenCharacterAddComponentsAction")
    actor_class = unreal.load_class(None, "/Script/LyraGame.LyraCharacter")
    component_class = unreal.load_class(None, "/Script/PhysicsDrivenCharacterRuntime.RecoverableFallComponent")

    action = None
    for candidate in actions:
        if candidate and candidate.get_class() == add_components_class.static_class():
            action = candidate
            break
    if action is None:
        action = unreal.new_object(add_components_class, outer=data, name="AddRecoverableFallComponent")
        actions.append(action)

    data.set_editor_property("actions", actions)
    unreal.EditorAssetLibrary.save_loaded_asset(data, only_if_is_dirty=False)
    report.update({
        "saved": True,
        "actor_class": actor_class.get_path_name(),
        "component_class": component_class.get_path_name(),
        "client_component": True,
        "server_component": True,
        "final_actions": [candidate.get_class().get_name() for candidate in actions if candidate],
    })
except Exception as exc:
    report["errors"].append(str(exc))

os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
with open(REPORT_PATH, "w", encoding="utf-8") as handle:
    json.dump(report, handle, ensure_ascii=False, indent=2)
unreal.log(json.dumps(report, ensure_ascii=False))
if report["errors"]:
    raise RuntimeError("; ".join(report["errors"]))
