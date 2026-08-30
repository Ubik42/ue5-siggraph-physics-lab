param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')))

$ErrorActionPreference = 'Stop'
$root = Join-Path $ProjectRoot 'Docs\Validation\M6'
$runtime = Get-Content -LiteralPath (Join-Path $root 'DrivenCharacterDropMapping.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$samples = @(Import-Csv -LiteralPath (Join-Path $root 'DrivenCharacterDropMapping.csv'))
$source = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Private\PhysicsControlCharacterRealtimeActor.cpp') -Raw -Encoding UTF8

$allFinite = $true
$timeStrict = $true
$previousTime = -1.0
$maxBaselinePose = 0.0
$maxResearchPose = 0.0
$peakBaselineDownward = 0.0
$peakResearchDownward = 0.0
$minBaselineHeight = [double]::PositiveInfinity
$minResearchHeight = [double]::PositiveInfinity
foreach ($sample in $samples) {
    $values = @(
        [double]$sample.time_s, [double]$sample.baseline_pose_error_deg, [double]$sample.research_pose_error_deg,
        [double]$sample.baseline_pelvis_displacement_cm, [double]$sample.research_pelvis_displacement_cm,
        [double]$sample.baseline_pelvis_height_cm, [double]$sample.research_pelvis_height_cm,
        [double]$sample.baseline_pelvis_vertical_speed_cm_s, [double]$sample.research_pelvis_vertical_speed_cm_s)
    foreach ($value in $values) { if (-not [double]::IsFinite($value)) { $allFinite = $false } }
    if ($values[0] -le $previousTime) { $timeStrict = $false }
    $previousTime = $values[0]
    $maxBaselinePose = [math]::Max($maxBaselinePose, $values[1])
    $maxResearchPose = [math]::Max($maxResearchPose, $values[2])
    $minBaselineHeight = [math]::Min($minBaselineHeight, $values[5])
    $minResearchHeight = [math]::Min($minResearchHeight, $values[6])
    $peakBaselineDownward = [math]::Max($peakBaselineDownward, -$values[7])
    $peakResearchDownward = [math]::Max($peakResearchDownward, -$values[8])
}
$first = $samples[0]
$last = $samples[-1]
$baselineDrop = [double]$first.baseline_pelvis_height_cm - $minBaselineHeight
$researchDrop = [double]$first.research_pelvis_height_cm - $minResearchHeight
$theorySpeed = [math]::Sqrt(2.0 * 980.0 * [double]$runtime.drop_height_cm)
$failureSummaries = @(Get-ChildItem -LiteralPath $root -Directory -Filter 'DropMapping*Failure' | ForEach-Object {
    Join-Path $_.FullName 'DrivenCharacterDropMapping.json'
} | Where-Object { Test-Path -LiteralPath $_ })

$checks = [ordered]@{
    exact_engine_and_scenario = [bool]($runtime.engine -eq '5.4.4-35576357+++UE5+Release-5.4' -and $runtime.scenario -eq 'HeightDropMapping')
    real_manny_multibody_contract = [bool]($runtime.physics_body_count -eq 22 -and $runtime.research_control_count -eq 22)
    shared_height_and_no_impulse = [bool]($runtime.drop_height_cm -eq 60 -and $runtime.shared_impulse_y_kg_cm_per_s -eq 0)
    csv_has_360_finite_fixed_steps = [bool]($samples.Count -eq 360 -and $runtime.sample_count -eq 360 -and $allFinite -and $timeStrict -and [math]::Abs([double]$last.time_s - 6.0) -lt 0.02)
    passive_speed_matches_si_truth_scale = [bool]([math]::Abs($peakBaselineDownward - $theorySpeed) -lt 30.0)
    active_speed_is_bounded = [bool]($peakResearchDownward -gt 100.0 -and $peakResearchDownward -lt 700.0)
    csv_recomputes_peak_speeds = [bool]([math]::Abs($peakBaselineDownward - $runtime.baseline_peak_downward_speed_cm_s) -lt 1.0e-6 -and [math]::Abs($peakResearchDownward - $runtime.research_peak_downward_speed_cm_s) -lt 1.0e-6)
    csv_recomputes_height_drops = [bool]([math]::Abs($baselineDrop - $runtime.baseline_pelvis_drop_cm) -lt 1.0e-6 -and [math]::Abs($researchDrop - $runtime.research_pelvis_drop_cm) -lt 1.0e-6)
    active_height_drop_is_bounded = [bool]($researchDrop -gt 45.0 -and $researchDrop -lt 100.0)
    passive_ragdoll_collapse_is_observed = [bool]$runtime.passive_ragdoll_collapse_observed
    active_landing_contract_passes = [bool]$runtime.research_bounded_landing_pass
    active_pose_error_is_below_half_baseline = [bool]($runtime.research_final_pose_error_deg -lt $runtime.baseline_final_pose_error_deg * 0.5)
    csv_recomputes_final_and_max_pose = [bool]([math]::Abs([double]$last.baseline_pose_error_deg - $runtime.baseline_final_pose_error_deg) -lt 1.0e-6 -and [math]::Abs([double]$last.research_pose_error_deg - $runtime.research_final_pose_error_deg) -lt 1.0e-6 -and [math]::Abs($maxBaselinePose - $runtime.baseline_max_pose_error_deg) -lt 1.0e-6 -and [math]::Abs($maxResearchPose - $runtime.research_max_pose_error_deg) -lt 1.0e-6)
    controller_advantage_gate_passes = [bool]$runtime.controller_advantage_pass
    source_uses_strict_drop_lock_and_ballistic_target = [bool]($source -match 'SetPhysicsBlendWeight\(1\.0f\)' -and $source -match 'SetAllBodiesBelowSimulatePhysics' -and $source -match '0\.5 \* 980\.0 \* FallTime \* FallTime' -and $source -match 'SetControlTargetPosition')
    six_failed_protocol_iterations_are_preserved = [bool]($failureSummaries.Count -eq 6)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$result = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = ($failed.Count -eq 0 ? 'passed' : 'failed')
    engine = 'UE 5.4.4 CL 35576357'
    timestep = 'UE -BENCHMARK -FPS=60, 360 PostPhysics samples over 6 seconds'
    scope = 'Real Manny + PA_Mannequin height-drop mapping: passive Chaos ragdoll collapse versus PhysicsControl pose retention under one 60 cm release.'
    claim_boundary = 'This maps height, pelvis speed, drop distance and multibody pose stability into real Chaos. It does not claim that PhysicsControl force equals the one-DOF spring force, and it does not report contact force because UE 5.4 PhysicsControl does not expose a per-control force history here.'
    theory = [ordered]@{ drop_height_cm = 60.0; ideal_freefall_speed_cm_s = $theorySpeed }
    recomputed = [ordered]@{ baseline_peak_downward_speed_cm_s=$peakBaselineDownward; research_peak_downward_speed_cm_s=$peakResearchDownward; baseline_pelvis_drop_cm=$baselineDrop; research_pelvis_drop_cm=$researchDrop; baseline_max_pose_error_deg=$maxBaselinePose; research_max_pose_error_deg=$maxResearchPose }
    runtime = $runtime
    preserved_failure_summaries = @($failureSummaries | ForEach-Object { [IO.Path]::GetRelativePath($ProjectRoot, $_).Replace('\','/') })
    checks = $checks
    failed_checks = $failed
}
$result | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $root 'chaos_drop_mapping_validation.json') -Encoding UTF8
$result | ConvertTo-Json -Depth 10
if ($failed.Count -gt 0) { exit 1 }
