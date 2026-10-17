"""Check local file prerequisites, without loading assets or launching UE."""
import argparse
import json
from pathlib import Path

REQUIRED = [
    'SIGGRAPHPhysicsLab.uproject', 'Source/LyraEditor.Target.cs',
    'Source/LyraGame/LyraGame.Build.cs', 'Source/LyraEditor/LyraEditor.Build.cs',
    'Plugins/CommonGame/CommonGame.uplugin',
    'Plugins/CommonLoadingScreen/CommonLoadingScreen.uplugin',
    'Plugins/GameFeatures/ShooterCore/ShooterCore.uplugin',
    'Plugins/PhysicsResearch/PhysicsResearch.uplugin',
    'Plugins/GameFeatures/PhysicsDrivenCharacter/PhysicsDrivenCharacter.uplugin',
    'Plugins/GameFeatures/PhysicsDrivenCharacter/Content/Maps/L_PhysicsControlCharacter.umap',
    'Content/Characters/Heroes/Mannequin/Meshes/SKM_Manny.uasset',
    'Content/Characters/Heroes/Mannequin/Rig/PA_Mannequin.uasset',
    'Content/B_LyraGameInstance.uasset',
]

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--project-root', type=Path, required=True)
    parser.add_argument('--engine-root', type=Path, required=True)
    args = parser.parse_args()
    missing = [name for name in REQUIRED if not (args.project_root / name).is_file()]
    engine = args.engine_root / 'Engine'
    required_engine = ['Build/Build.version', 'Build/BatchFiles/Build.bat',
                       'Binaries/Win64/UnrealEditor.exe',
                       'Plugins/Experimental/PhysicsControl/PhysicsControl.uplugin']
    missing_engine = [name for name in required_engine if not (engine / name).is_file()]
    version = json.loads((engine / 'Build/Build.version').read_text()) if (engine / 'Build/Build.version').exists() else {}
    matches_version = [version.get(k) for k in ('MajorVersion','MinorVersion','PatchVersion')] == [5,4,4]
    print(json.dumps({'scope': 'Known file prerequisites only; does not validate asset dependencies, C++ build or runtime.',
                      'missing_project_files': missing, 'missing_engine_files': missing_engine,
                      'engine_5_4_4': matches_version, 'engine_changelist': version.get('Changelist'),
                      'prerequisites_present': not missing and not missing_engine and matches_version}, ensure_ascii=False, indent=2))
    raise SystemExit(0 if not missing and not missing_engine and matches_version else 1)
