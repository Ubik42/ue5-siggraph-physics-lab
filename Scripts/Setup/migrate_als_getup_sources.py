import json
import os

import unreal


PACKAGES = [
    "/Game/AdvancedLocomotionV4/CharacterAssets/MannequinSkeleton/AnimationExamples/Actions/ALS_CLF_GetUp_Front",
    "/Game/AdvancedLocomotionV4/CharacterAssets/MannequinSkeleton/AnimationExamples/Actions/ALS_CLF_GetUp_Back",
]
DESTINATION_CONTENT = "D:/3D/UE Projects/SIGGRAPHPhysicsLab/Content"
REPORT_PATH = "D:/3D/UE Projects/SIGGRAPHPhysicsLab/Docs/Validation/M6/als_getup_migration.json"

source_assets = []
errors = []
for package_name in PACKAGES:
    asset_data = unreal.EditorAssetLibrary.find_asset_data(package_name)
    if not asset_data.is_valid():
        errors.append(f"Missing source package: {package_name}")
        continue
    source_assets.append(
        {
            "package": package_name,
            "asset_class": str(asset_data.asset_class_path.asset_name),
            "asset_name": str(asset_data.asset_name),
        }
    )

if not errors:
    options = unreal.MigrationOptions()
    options.prompt = False
    options.ignore_dependencies = False
    options.asset_conflict = unreal.AssetMigrationConflict.SKIP
    unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(
        PACKAGES, DESTINATION_CONTENT, options
    )

migrated_files = []
for package_name in PACKAGES:
    relative = package_name.removeprefix("/Game/") + ".uasset"
    filename = os.path.join(DESTINATION_CONTENT, *relative.split("/"))
    if os.path.isfile(filename):
        migrated_files.append(
            {"package": package_name, "file": filename, "size_bytes": os.path.getsize(filename)}
        )
    else:
        errors.append(f"Migration output missing: {filename}")

report = {
    "source_project": unreal.Paths.get_project_file_path(),
    "destination_content": DESTINATION_CONTENT,
    "requested_packages": PACKAGES,
    "source_assets": source_assets,
    "migrated_files": migrated_files,
    "dependencies_included": True,
    "errors": errors,
}
os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
with open(REPORT_PATH, "w", encoding="utf-8") as handle:
    json.dump(report, handle, ensure_ascii=False, indent=2)

unreal.log(f"SIGGRAPH M6 ALS get-up migration complete: {REPORT_PATH}")
if errors:
    raise RuntimeError("; ".join(errors))
