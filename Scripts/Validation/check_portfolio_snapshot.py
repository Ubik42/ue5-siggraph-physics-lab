"""Offline check of saved source and A/B data. Does not run Unreal Engine."""
import csv
import hashlib
import json
import math
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'Docs/Validation/20261017'

def digest(path, text=False):
    content = path.read_text(encoding='utf-8-sig').replace('\r\n', '\n').encode() if text else path.read_bytes()
    return hashlib.sha256(content).hexdigest()

def verify(root=ROOT, data=DATA):
    manifest = json.loads((data / 'source-manifest.json').read_text(encoding='utf-8'))
    problems = []
    for relative, expected in manifest['files'].items():
        path = root / relative
        if not path.is_file() or digest(path, text=True) != expected:
            problems.append(f'Source missing or changed: {relative}')
    evidence = json.loads((data / 'comparison.json').read_text(encoding='utf-8'))
    for relative, expected in evidence['files'].items():
        path = data / relative
        if not path.is_file() or digest(path) != expected:
            problems.append(f'Data missing or changed: {relative}')
    rows = list(csv.DictReader((data / 'DrivenCharacterRealtime.csv').read_text(encoding='utf-8-sig').splitlines()))
    summary = json.loads((data / 'DrivenCharacterRealtime.json').read_text(encoding='utf-8-sig'))
    historical = json.loads((root / 'Docs/Validation/M6/DrivenCharacterPortfolio.json').read_text(encoding='utf-8-sig'))
    fields = {
        'baseline_pose_error_deg': 'baseline_final_pose_error_deg',
        'research_pose_error_deg': 'research_final_pose_error_deg',
        'baseline_pelvis_displacement_cm': 'baseline_final_pelvis_displacement_cm',
        'research_pelvis_displacement_cm': 'research_final_pelvis_displacement_cm',
    }
    assert len(rows) == summary['sample_count'] == 360, 'Sample count'
    assert summary['scenario'] == 'SharedSpineImpulse'
    assert summary['shared_impulse_applied'] and summary['shared_impulse_y_kg_cm_per_s'] == 5200
    assert summary['physics_body_count'] == summary['research_control_count'] == 22
    assert summary['scene_rigid_body_count'] == 44
    assert all(math.isfinite(float(r[k])) for r in rows for k in ('time_s', *fields)), 'Non-finite data'
    assert abs(float(rows[0]['time_s']) - 1 / 60) < 1e-7
    # UE accumulates a float fixed DeltaSeconds: this recording ends at 6.000000313.
    assert all(abs(float(row['time_s']) - (index + 1) / 60) < 1e-6 for index, row in enumerate(rows)), 'Cumulative fixed time'
    assert all(abs(float(b['time_s']) - float(a['time_s']) - 1 / 60) < 1e-7 for a, b in zip(rows, rows[1:])), 'Step interval'
    final = {}
    for column, key in fields.items():
        value = float(rows[-1][column])
        assert abs(value - summary[key]) < 1e-6, f'Summary: {column}'
        assert abs(value - historical[key]) < 1e-6, f'Historical: {column}'
        final[column] = value
    getup = data / 'GetUp'
    getup_evidence = json.loads((getup / 'getup-evidence.json').read_text(encoding='utf-8'))
    for name, expected in getup_evidence['files'].items():
        assert digest(getup / name) == expected, f'Get-up fingerprint: {name}'
    getup_summary = json.loads((getup / 'PhysicalGetUpContinuous.json').read_text(encoding='utf-8-sig'))
    getup_rows = list(csv.DictReader((getup / 'PhysicalGetUpContinuous.csv').read_text(encoding='utf-8-sig').splitlines()))
    assert len(getup_rows) == getup_evidence['samples'] == 1110
    assert all(abs(float(row['time_s']) - (index + 1)/60) < 2e-6 for index, row in enumerate(getup_rows))
    assert all(math.isfinite(float(row[key])) for row in getup_rows for key in ('time_s','raw_speed_cm_s','raw_angular_rad_s','pelvis_x','pelvis_y','pelvis_z'))
    assert getup_summary['passed'] and getup_summary['upright_stable_s'] >= .25
    assert getup_summary['moved_after_recovery_cm'] > 50
    assert any(row['phase']=='Recovering' and row['controls']=='22' and row['simulating']=='1' for row in getup_rows)
    restored = [row for row in getup_rows if float(row['time_s']) > getup_summary['recovery_complete_s'] + .1]
    assert restored and all(row['simulating']=='0' and row['controls']=='0' and row['movement_mode']=='1' for row in restored)
    # Check only new/updated reading entry points. Older reports are historical.
    pages = ['README.md', 'Docs/Development/CASE05_REPRODUCTION.md',
             'Docs/Cases/Case05_物理驱动角色.md', 'Docs/Validation/20261017/README.md', 'Docs/Development/CONTINUOUS_GETUP.md']
    for relative in pages:
        page = root / relative
        for target in re.findall(r'\]\(([^)]+)\)', page.read_text(encoding='utf-8-sig')):
            if re.match(r'^[a-z]+://|^#', target):
                continue
            if not (page.parent / target.split('#')[0]).is_file():
                problems.append(f'Broken local link in {relative}: {target}')
    result = {'scope': 'Saved source hashes, 360 impulse samples, 1110 get-up samples and reading links; no engine launch.',
              'source_files': len(manifest['files']), 'samples': len(rows),
              'getup_samples': len(getup_rows), 'getup_movement_cm':getup_summary['moved_after_recovery_cm'],
              'final': final, 'problems': problems, 'passed': not problems}
    assert not problems, json.dumps(result, ensure_ascii=False)
    return result

if __name__ == '__main__':
    print(json.dumps(verify(), ensure_ascii=False, indent=2))
