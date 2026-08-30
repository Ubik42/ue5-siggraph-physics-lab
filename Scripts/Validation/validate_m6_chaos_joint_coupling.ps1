param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')))

$ErrorActionPreference = 'Stop'
$root = Join-Path $ProjectRoot 'Docs\Validation\M6'
$runtime = Get-Content -LiteralPath (Join-Path $root 'DrivenCharacterJointCoupling.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$samples = @(Import-Csv -LiteralPath (Join-Path $root 'DrivenCharacterJointCoupling.csv'))
$source = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Private\PhysicsControlCharacterRealtimeActor.cpp') -Raw -Encoding UTF8

$columns = @(
    'baseline_hip_error_deg', 'baseline_knee_error_deg', 'baseline_torso_error_deg',
    'research_hip_error_deg', 'research_knee_error_deg', 'research_torso_error_deg')
$peaks = [ordered]@{}
foreach ($column in $columns) { $peaks[$column] = 0.0 }
$allFinite = $true
$timeStrict = $true
$previousTime = -1.0
foreach ($sample in $samples) {
    $time = [double]$sample.time_s
    if (-not [double]::IsFinite($time) -or $time -le $previousTime) { $timeStrict = $false }
    $previousTime = $time
    foreach ($column in $columns) {
        $value = [double]$sample.$column
        if (-not [double]::IsFinite($value)) { $allFinite = $false }
        $peaks[$column] = [math]::Max([double]$peaks[$column], $value)
    }
}

$baselineTransfer = [math]::Max([double]$peaks.baseline_hip_error_deg, [double]$peaks.baseline_knee_error_deg) / [double]$peaks.baseline_torso_error_deg
$researchTransfer = [math]::Max([double]$peaks.research_hip_error_deg, [double]$peaks.research_knee_error_deg) / [double]$peaks.research_torso_error_deg
$checks = [ordered]@{
    exact_engine_and_scenario = [bool]($runtime.engine -eq '5.4.4-35576357+++UE5+Release-5.4' -and $runtime.scenario -eq 'SharedSpineImpulse')
    same_real_manny_multibody_contract = [bool]($runtime.physics_body_count -eq 22 -and $runtime.research_control_count -eq 22)
    same_distal_impulse_applied = [bool]($runtime.shared_impulse_applied -and $runtime.shared_impulse_y_kg_cm_per_s -eq 5200)
    csv_has_360_finite_fixed_steps = [bool]($samples.Count -eq 360 -and $runtime.sample_count -eq 360 -and $allFinite -and $timeStrict -and [math]::Abs([double]$samples[-1].time_s - 6.0) -lt 0.02)
    csv_recomputes_baseline_joint_peaks = [bool]([math]::Abs([double]$peaks.baseline_hip_error_deg - $runtime.baseline_peak_hip_error_deg) -lt 1.0e-6 -and [math]::Abs([double]$peaks.baseline_knee_error_deg - $runtime.baseline_peak_knee_error_deg) -lt 1.0e-6 -and [math]::Abs([double]$peaks.baseline_torso_error_deg - $runtime.baseline_peak_torso_error_deg) -lt 1.0e-6)
    csv_recomputes_research_joint_peaks = [bool]([math]::Abs([double]$peaks.research_hip_error_deg - $runtime.research_peak_hip_error_deg) -lt 1.0e-6 -and [math]::Abs([double]$peaks.research_knee_error_deg - $runtime.research_peak_knee_error_deg) -lt 1.0e-6 -and [math]::Abs([double]$peaks.research_torso_error_deg - $runtime.research_peak_torso_error_deg) -lt 1.0e-6)
    csv_recomputes_transfer_ratios = [bool]([math]::Abs($baselineTransfer - $runtime.baseline_proximal_to_torso_transfer_ratio) -lt 1.0e-9 -and [math]::Abs($researchTransfer - $runtime.research_proximal_to_torso_transfer_ratio) -lt 1.0e-9)
    physics_control_reduces_hip_peak = [bool]($runtime.research_peak_hip_error_deg -lt $runtime.baseline_peak_hip_error_deg * 0.5)
    physics_control_reduces_knee_peak = [bool]($runtime.research_peak_knee_error_deg -lt $runtime.baseline_peak_knee_error_deg * 0.5)
    physics_control_reduces_torso_peak = [bool]($runtime.research_peak_torso_error_deg -lt $runtime.baseline_peak_torso_error_deg * 0.5)
    physics_control_reduces_proximal_transfer = [bool]($researchTransfer -lt $baselineTransfer * 0.6)
    source_measures_parent_relative_joint_rotations = [bool]($source -match 'Parent\.Inverse\(\) \* Child' -and $source -match 'thigh_l' -and $source -match 'calf_l' -and $source -match 'spine_03' -and $source -match 'spine_02')
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$result = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = ($failed.Count -eq 0 ? 'passed' : 'failed')
    engine = 'UE 5.4.4 CL 35576357'
    timestep = 'UE -BENCHMARK -FPS=60, 360 PostPhysics samples over 6 seconds'
    scope = 'Real Manny + PA_Mannequin under the same spine_03 impulse: native Chaos joint constraints alone versus the same system with Epic PhysicsControl parent-space controls.'
    claim_boundary = 'This is a controller-layer A/B, not uncoupled rigid bodies: the baseline still contains Chaos Physics Asset constraints. Parent-relative angular errors isolate local joint deformation from whole-character rotation; the reported ratio is a response ratio, not force or energy transmission.'
    joints = [ordered]@{ hip = 'thigh_l relative to pelvis'; knee = 'calf_l relative to thigh_l'; torso = 'spine_03 relative to spine_02' }
    recomputed = [ordered]@{
        peaks_deg = $peaks
        baseline_proximal_to_torso_transfer_ratio = $baselineTransfer
        research_proximal_to_torso_transfer_ratio = $researchTransfer
    }
    runtime = $runtime
    checks = $checks
    failed_checks = $failed
}
$result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $root 'chaos_joint_coupling_validation.json') -Encoding UTF8
$result | ConvertTo-Json -Depth 10
if ($failed.Count -gt 0) { exit 1 }
