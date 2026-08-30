import json
import os

import unreal


SOURCE_MESH = "/Game/AdvancedLocomotionV4/CharacterAssets/MannequinSkeleton/Meshes/Mannequin"
TARGET_MESH = "/Game/Characters/Heroes/Mannequin/Meshes/SKM_Manny"
SOURCE_ANIMATIONS = [
    "/Game/AdvancedLocomotionV4/CharacterAssets/MannequinSkeleton/AnimationExamples/Actions/ALS_CLF_GetUp_Front",
    "/Game/AdvancedLocomotionV4/CharacterAssets/MannequinSkeleton/AnimationExamples/Actions/ALS_CLF_GetUp_Back",
]
OUTPUT_FOLDER = "/PhysicsDrivenCharacter/Animations/ALS"
SOURCE_RIG_PATH = f"{OUTPUT_FOLDER}/IK_ALS_UE4Mannequin"
TARGET_RIG_PATH = f"{OUTPUT_FOLDER}/IK_Lyra_Manny"
RETARGETER_PATH = f"{OUTPUT_FOLDER}/RTG_ALS_To_LyraManny"
OUTPUT_PATHS = [
    f"{OUTPUT_FOLDER}/ALS_CLF_GetUp_Front_Manny",
    f"{OUTPUT_FOLDER}/ALS_CLF_GetUp_Back_Manny",
]
REPORT_PATH = os.path.join(
    unreal.Paths.project_dir(), "Docs", "Validation", "M6", "als_getup_retarget.json"
)


def load_required(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise RuntimeError(f"Required asset missing: {path}")
    return asset


def create_or_load(path, asset_class, factory_class):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path), False
    package_path, asset_name = path.rsplit("/", 1)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, package_path, asset_class, factory_class()
    )
    if asset is None:
        raise RuntimeError(f"Failed to create asset: {path}")
    return asset, True


errors = []
report = {
    "source_mesh": SOURCE_MESH,
    "target_mesh": TARGET_MESH,
    "source_animations": SOURCE_ANIMATIONS,
    "source_rig": SOURCE_RIG_PATH,
    "target_rig": TARGET_RIG_PATH,
    "retargeter": RETARGETER_PATH,
    "outputs": [],
    "errors": errors,
}

try:
    source_mesh = load_required(SOURCE_MESH)
    target_mesh = load_required(TARGET_MESH)
    source_animations = [load_required(path) for path in SOURCE_ANIMATIONS]

    source_rig, source_rig_created = create_or_load(
        SOURCE_RIG_PATH, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory
    )
    source_controller = unreal.IKRigController.get_controller(source_rig)
    if not source_controller.set_skeletal_mesh(source_mesh):
        raise RuntimeError("Source IK Rig rejected ALS Mannequin mesh")
    source_auto = source_controller.apply_auto_generated_retarget_definition()

    target_rig, target_rig_created = create_or_load(
        TARGET_RIG_PATH, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory
    )
    target_controller = unreal.IKRigController.get_controller(target_rig)
    if not target_controller.set_skeletal_mesh(target_mesh):
        raise RuntimeError("Target IK Rig rejected Manny mesh")
    target_auto = target_controller.apply_auto_generated_retarget_definition()

    retargeter, retargeter_created = create_or_load(
        RETARGETER_PATH, unreal.IKRetargeter, unreal.IKRetargetFactory
    )
    retarget_controller = unreal.IKRetargeterController.get_controller(retargeter)
    retarget_controller.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
    retarget_controller.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    retarget_controller.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, source_mesh)
    retarget_controller.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, target_mesh)
    retarget_controller.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)

    source_chain_names = [str(chain.chain_name) for chain in source_controller.get_retarget_chains()]
    target_chain_names = [str(chain.chain_name) for chain in target_controller.get_retarget_chains()]
    # UE4 Mannequin has no metacarpal chains. Fuzzy mapping otherwise assigns each Manny
    # metacarpal to the similarly named finger and distorts the hand hierarchy.
    for chain_name in target_chain_names:
        if chain_name.endswith("Metacarpal"):
            retarget_controller.set_source_chain("None", chain_name)
    chain_mappings = [
        {
            "target": chain_name,
            "source": str(retarget_controller.get_source_chain(chain_name)),
        }
        for chain_name in target_chain_names
    ]

    unreal.EditorAssetLibrary.save_loaded_asset(source_rig)
    unreal.EditorAssetLibrary.save_loaded_asset(target_rig)
    unreal.EditorAssetLibrary.save_loaded_asset(retargeter)

    report["rig_creation"] = {
        "source_created": source_rig_created,
        "target_created": target_rig_created,
        "retargeter_created": retargeter_created,
        "source_auto_definition": source_auto,
        "target_auto_definition": target_auto,
        "source_root": str(source_controller.get_retarget_root()),
        "target_root": str(target_controller.get_retarget_root()),
        "source_chain_count": len(source_controller.get_retarget_chains()),
        "target_chain_count": len(target_controller.get_retarget_chains()),
        "source_chains": source_chain_names,
        "target_chains": target_chain_names,
        "chain_mappings": chain_mappings,
        "mapped_target_chain_count": sum(1 for item in chain_mappings if item["source"] != "None"),
    }

    # These are personal derived outputs, so deterministic rebuild is safer than leaving
    # stale animation data after chain-map or retarget-pose changes.
    for path in OUTPUT_PATHS:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            unreal.EditorAssetLibrary.delete_asset(path)
    source_data = [unreal.EditorAssetLibrary.find_asset_data(path) for path in SOURCE_ANIMATIONS]
    new_assets = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
            source_data,
            source_mesh,
            target_mesh,
            retargeter,
            "",
            "",
            "",
            "_Manny",
            False,
    )
    if len(new_assets) != len(SOURCE_ANIMATIONS):
        raise RuntimeError(
            f"Retarget produced {len(new_assets)} assets for {len(SOURCE_ANIMATIONS)} inputs"
        )
    for result, destination in zip(new_assets, OUTPUT_PATHS):
        source_package = str(result.package_name)
        if source_package != destination and not unreal.EditorAssetLibrary.rename_asset(
            source_package, destination
        ):
            raise RuntimeError(f"Failed to move retargeted asset {source_package} to {destination}")

    for path in OUTPUT_PATHS:
        animation = load_required(path)
        unreal.EditorAssetLibrary.save_loaded_asset(animation)
        skeleton = animation.get_editor_property("skeleton")
        report["outputs"].append(
            {
                "path": path,
                "class": animation.get_class().get_name(),
                "skeleton": skeleton.get_path_name() if skeleton else "",
                "play_length_seconds": animation.get_play_length(),
                "rate_scale": animation.get_editor_property("rate_scale"),
            }
        )
except Exception as exc:
    errors.append(str(exc))

os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
with open(REPORT_PATH, "w", encoding="utf-8") as handle:
    json.dump(report, handle, ensure_ascii=False, indent=2)

unreal.log(f"SIGGRAPH M6 ALS get-up retarget complete: {REPORT_PATH}")
if errors:
    raise RuntimeError("; ".join(errors))
