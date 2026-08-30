import json
import os

import unreal


SOURCE = os.path.join(
    unreal.Paths.project_saved_dir(),
    "DerivedAssets",
    "Case02",
    "SM_TerrorBringer_SoftBodyCage.obj",
)
DESTINATION = "/PhysicsSoftBody/DragonDerived"
ASSET_NAME = "SM_TerrorBringer_SoftBodyCage"
OBJECT_PATH = f"{DESTINATION}/{ASSET_NAME}.{ASSET_NAME}"
REPORT = os.path.join(
    unreal.Paths.project_dir(),
    "Docs",
    "Validation",
    "M3",
    "dragon_softbody_cage_import.json",
)


if not os.path.isfile(SOURCE):
    raise RuntimeError(f"缺少派生 Dragon 外壳 OBJ：{SOURCE}")

options = unreal.FbxImportUI()
options.import_as_skeletal = False
options.import_mesh = True
options.import_materials = False
options.import_textures = False
options.static_mesh_import_data.combine_meshes = True
options.static_mesh_import_data.generate_lightmap_u_vs = True
options.static_mesh_import_data.auto_generate_collision = False

task = unreal.AssetImportTask()
task.filename = SOURCE
task.destination_path = DESTINATION
task.destination_name = ASSET_NAME
task.automated = True
task.replace_existing = True
task.save = True
task.options = options
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

asset = unreal.EditorAssetLibrary.load_asset(OBJECT_PATH)
if not asset or not isinstance(asset, unreal.StaticMesh):
    raise RuntimeError(f"派生 Dragon 外壳导入失败：{OBJECT_PATH}; imported={task.imported_object_paths}")
asset.set_editor_property("allow_cpu_access", True)
if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
    raise RuntimeError(f"派生 Dragon 外壳保存失败：{OBJECT_PATH}")

bounds = asset.get_bounds()
report = {
    "result": "passed",
    "source": SOURCE,
    "object_path": OBJECT_PATH,
    "class": asset.get_class().get_name(),
    "imported_object_paths": list(task.imported_object_paths),
    "material_slots": len(asset.static_materials),
    "allow_cpu_access": bool(asset.get_editor_property("allow_cpu_access")),
    "bounds_origin_cm": [bounds.origin.x, bounds.origin.y, bounds.origin.z],
    "bounds_extent_cm": [bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z],
    "license_boundary": "Local derivative of /Game/FourEvilDragonsHP; do not redistribute independently of the Fab source license.",
}
os.makedirs(os.path.dirname(REPORT), exist_ok=True)
with open(REPORT, "w", encoding="utf-8") as stream:
    json.dump(report, stream, ensure_ascii=False, indent=2)
unreal.log(f"CASE02_DRAGON_CAGE_IMPORT_PASSED {json.dumps(report, ensure_ascii=False)}")
