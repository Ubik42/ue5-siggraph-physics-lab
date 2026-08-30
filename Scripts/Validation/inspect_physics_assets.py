import json
import os
import unreal


ROOTS = ["/Game/Assassin", "/Game/FourEvilDragonsHP"]
OUTPUT = os.path.join(
    unreal.Paths.project_dir(),
    "Docs",
    "Validation",
    "AddedAssets",
    "physics_asset_audit.json",
)


def object_path(value):
    if value is None:
        return ""
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


def safe_property(value, name, default=None):
    try:
        return value.get_editor_property(name)
    except Exception:
        return default


registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(ROOTS, True)
skeletal_mesh_subsystem = None
if hasattr(unreal, "SkeletalMeshEditorSubsystem"):
    skeletal_mesh_subsystem = unreal.get_editor_subsystem(
        unreal.SkeletalMeshEditorSubsystem
    )

report = {
    "engine_version": unreal.SystemLibrary.get_engine_version(),
    "project": unreal.Paths.get_project_file_path(),
    "roots": {},
    "warnings": [],
    "errors": [],
}

for root in ROOTS:
    assets = list(registry.get_assets_by_path(root, recursive=True))
    root_report = {
        "skeletal_meshes": [],
        "physics_assets": [],
        "skeletons": [],
        "animations": [],
        "maps": [],
    }

    for asset_data in assets:
        kind = str(asset_data.asset_class_path.asset_name)
        package = str(asset_data.package_name)

        if kind == "SkeletalMesh":
            mesh = asset_data.get_asset()
            if mesh is None:
                report["errors"].append(f"Cannot load SkeletalMesh {package}")
                continue

            skeleton = safe_property(mesh, "skeleton")
            physics_asset = safe_property(mesh, "physics_asset")
            materials = safe_property(mesh, "materials", []) or []
            clothing_assets = safe_property(mesh, "mesh_clothing_assets", []) or []
            clothing_asset_details = []
            for clothing_asset in clothing_assets:
                config_details = []
                configs = safe_property(clothing_asset, "cloth_configs", {}) or {}
                try:
                    config_items = configs.items()
                except Exception:
                    config_items = []
                for config_key, config_value in config_items:
                    config_details.append(
                        {
                            "key": str(config_key),
                            "class": config_value.get_class().get_name()
                            if config_value is not None
                            else "",
                        }
                    )
                clothing_lods = []
                lod_data = safe_property(clothing_asset, "lod_data", []) or []
                for lod_index, lod in enumerate(lod_data):
                    physical_mesh = safe_property(lod, "physical_mesh_data")
                    vertices = safe_property(physical_mesh, "vertices", []) or []
                    indices = safe_property(physical_mesh, "indices", []) or []
                    clothing_lods.append(
                        {
                            "lod_index": lod_index,
                            "physical_vertices": len(vertices),
                            "physical_triangles": len(indices) // 3,
                        }
                    )
                clothing_asset_details.append(
                    {
                        "path": object_path(clothing_asset),
                        "class": clothing_asset.get_class().get_name(),
                        "configs": config_details,
                        "lods": clothing_lods,
                    }
                )
            bounds = None
            try:
                box = mesh.get_bounds()
                bounds = {
                    "origin": [box.origin.x, box.origin.y, box.origin.z],
                    "extent": [box.box_extent.x, box.box_extent.y, box.box_extent.z],
                    "sphere_radius": box.sphere_radius,
                }
            except Exception:
                pass

            lod_count = None
            vertex_count_lod0 = None
            if skeletal_mesh_subsystem is not None:
                try:
                    lod_count = skeletal_mesh_subsystem.get_lod_count(mesh)
                except Exception:
                    pass
                try:
                    vertex_count_lod0 = skeletal_mesh_subsystem.get_num_vertices(mesh, 0)
                except Exception:
                    pass

            root_report["skeletal_meshes"].append(
                {
                    "package": package,
                    "skeleton": object_path(skeleton),
                    "physics_asset": object_path(physics_asset),
                    "material_slots": len(materials),
                    "clothing_asset_count": len(clothing_assets),
                    "clothing_assets": clothing_asset_details,
                    "lod_count": lod_count,
                    "vertex_count_lod0": vertex_count_lod0,
                    "bounds": bounds,
                }
            )
            if skeleton is None:
                report["warnings"].append(f"SkeletalMesh has no Skeleton: {package}")
            if physics_asset is None:
                report["warnings"].append(f"SkeletalMesh has no PhysicsAsset: {package}")

        elif kind == "PhysicsAsset":
            physics_asset = asset_data.get_asset()
            body_accessible = True
            constraint_accessible = True
            try:
                bodies = physics_asset.get_editor_property("skeletal_body_setups") or []
            except Exception:
                bodies = []
                body_accessible = False
            try:
                constraints = physics_asset.get_editor_property("constraint_setup") or []
            except Exception:
                constraints = []
                constraint_accessible = False
            root_report["physics_assets"].append(
                {
                    "package": package,
                    "body_count": len(bodies) if body_accessible else None,
                    "constraint_count": len(constraints)
                    if constraint_accessible
                    else None,
                    "body_array_exposed_to_python": body_accessible,
                    "constraint_array_exposed_to_python": constraint_accessible,
                }
            )

        elif kind == "Skeleton":
            skeleton = asset_data.get_asset()
            bone_count = None
            try:
                bone_count = len(skeleton.get_reference_pose())
            except Exception:
                pass
            root_report["skeletons"].append(
                {"package": package, "bone_count": bone_count}
            )

        elif kind == "AnimSequence":
            root_report["animations"].append(package)
        elif kind == "World":
            root_report["maps"].append(package)

    root_report["summary"] = {
        "skeletal_mesh_count": len(root_report["skeletal_meshes"]),
        "physics_asset_count": len(root_report["physics_assets"]),
        "skeleton_count": len(root_report["skeletons"]),
        "animation_count": len(root_report["animations"]),
        "map_count": len(root_report["maps"]),
        "clothing_asset_count": sum(
            mesh["clothing_asset_count"] for mesh in root_report["skeletal_meshes"]
        ),
    }
    report["roots"][root] = root_report

os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
with open(OUTPUT, "w", encoding="utf-8") as handle:
    json.dump(report, handle, ensure_ascii=False, indent=2)

unreal.log(f"SIGGRAPH physics asset audit complete: {OUTPUT}")
if report["errors"]:
    raise RuntimeError("; ".join(report["errors"]))
