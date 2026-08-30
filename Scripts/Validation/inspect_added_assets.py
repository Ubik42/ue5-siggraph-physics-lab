import json
import os
import unreal


ROOTS = ["/Game/Assassin", "/Game/FourEvilDragonsHP"]
OUTPUT = os.path.join(
    unreal.Paths.project_dir(),
    "Docs",
    "Validation",
    "AddedAssets",
    "asset_registry_report.json",
)


def class_name(asset_data):
    class_path = getattr(asset_data, "asset_class_path", None)
    if class_path:
        return str(class_path.asset_name)
    return str(asset_data.asset_class)


registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(ROOTS, True)

report = {
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "project": unreal.Paths.get_project_file_path(),
    "roots": {},
    "errors": [],
}

for root in ROOTS:
    assets = list(registry.get_assets_by_path(root, recursive=True))
    classes = {}
    load_failures = []
    sample_assets = []

    for asset in assets:
        kind = class_name(asset)
        classes[kind] = classes.get(kind, 0) + 1
        if len(sample_assets) < 12:
            sample_assets.append(str(asset.package_name))

    representatives = []
    preferred_classes = {
        "SkeletalMesh",
        "Skeleton",
        "PhysicsAsset",
        "AnimSequence",
        "World",
        "Material",
        "MaterialInstanceConstant",
    }
    selected_classes = set()
    for asset in assets:
        kind = class_name(asset)
        if kind in preferred_classes and kind not in selected_classes:
            selected_classes.add(kind)
            representatives.append(asset)

    for asset in representatives:
        object_path = f"{asset.package_name}.{asset.asset_name}"
        loaded = asset.get_asset()
        if loaded is None:
            load_failures.append(object_path)

    report["roots"][root] = {
        "asset_count": len(assets),
        "class_counts": dict(sorted(classes.items())),
        "representative_load_count": len(representatives),
        "representative_load_failures": load_failures,
        "sample_packages": sample_assets,
    }
    if not assets:
        report["errors"].append(f"No assets registered below {root}")
    if load_failures:
        report["errors"].append(
            f"{len(load_failures)} representative assets failed to load below {root}"
        )

os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
with open(OUTPUT, "w", encoding="utf-8") as handle:
    json.dump(report, handle, ensure_ascii=False, indent=2)

unreal.log(f"SIGGRAPH asset audit complete: {OUTPUT}")
if report["errors"]:
    raise RuntimeError("; ".join(report["errors"]))
