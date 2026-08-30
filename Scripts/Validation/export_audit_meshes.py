import json
import os
import unreal


ASSETS = {
    "Assassin_Cloak": "/Game/Assassin/Mesh/Separated_Mesh/Armor/S_cloak.S_cloak",
    "Dragon_Nightmare": "/Game/FourEvilDragonsHP/Meshes/DragonTheNightmare/DragonTheNightmareSK.DragonTheNightmareSK",
    "Dragon_TerrorBringer": "/Game/FourEvilDragonsHP/Meshes/DragonTheTerrorBringer/DragonTheTerrorBringerSK.DragonTheTerrorBringerSK",
    "Dragon_Usurper": "/Game/FourEvilDragonsHP/Meshes/DragonTheUsurper/DragonTheUsurperSK.DragonTheUsurperSK",
    "Dragon_SoulEater": "/Game/FourEvilDragonsHP/Meshes/DrangonTheSoulEater/DragonTheSoulEaterSK.DragonTheSoulEaterSK",
}

EXPORT_DIR = os.path.join(unreal.Paths.project_saved_dir(), "AssetAuditExports")
REPORT_PATH = os.path.join(EXPORT_DIR, "export_report.json")
os.makedirs(EXPORT_DIR, exist_ok=True)

report = {"exported": [], "errors": []}
for label, asset_path in ASSETS.items():
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        report["errors"].append(f"Cannot load {asset_path}")
        continue

    filename = os.path.join(EXPORT_DIR, f"{label}.fbx")
    task = unreal.AssetExportTask()
    task.set_editor_property("object", asset)
    task.set_editor_property("filename", filename)
    task.set_editor_property("automated", True)
    task.set_editor_property("prompt", False)
    task.set_editor_property("replace_identical", True)
    task.set_editor_property("selected", False)
    task.set_editor_property("use_file_archive", False)
    task.set_editor_property("write_empty_files", False)
    success = unreal.Exporter.run_asset_export_task(task)
    if success and os.path.exists(filename):
        report["exported"].append(
            {"label": label, "asset": asset_path, "file": filename}
        )
    else:
        report["errors"].append(f"Export failed for {asset_path}")

with open(REPORT_PATH, "w", encoding="utf-8") as handle:
    json.dump(report, handle, ensure_ascii=False, indent=2)

unreal.log(f"SIGGRAPH mesh export audit complete: {REPORT_PATH}")
if report["errors"]:
    raise RuntimeError("; ".join(report["errors"]))
