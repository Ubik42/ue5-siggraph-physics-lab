import json
import os
import unreal


FEATURES = [
    "PhysicsContactCloth",
    "PhysicsSoftBody",
    "PhysicsFlowMap",
    "PhysicsInverseDesign",
    "PhysicsDrivenCharacter",
]

OUTPUT = os.path.join(
    unreal.Paths.project_dir(),
    "Docs",
    "Validation",
    "GameFeatures",
    "game_feature_data_setup.json",
)

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
report = {"created": [], "existing": [], "errors": []}

for feature in FEATURES:
    package_path = f"/{feature}"
    object_path = f"{package_path}/{feature}.{feature}"
    existing = unreal.EditorAssetLibrary.load_asset(object_path)
    if existing is not None:
        report["existing"].append(object_path)
        continue

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.GameFeatureData)
    created = asset_tools.create_asset(
        asset_name=feature,
        package_path=package_path,
        asset_class=unreal.GameFeatureData,
        factory=factory,
    )
    if created is None:
        report["errors"].append(f"Failed to create {object_path}")
        continue

    unreal.EditorAssetLibrary.save_loaded_asset(created, only_if_is_dirty=False)
    report["created"].append(object_path)

os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
with open(OUTPUT, "w", encoding="utf-8") as handle:
    json.dump(report, handle, ensure_ascii=False, indent=2)

unreal.log(f"SIGGRAPH GameFeatureData setup complete: {OUTPUT}")
if report["errors"]:
    raise RuntimeError("; ".join(report["errors"]))
