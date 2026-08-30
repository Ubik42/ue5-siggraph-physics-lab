param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))

$ErrorActionPreference = "Stop"
$root = Join-Path $ProjectRoot "Docs\Validation\M6"
$runtimeJsonPath = Join-Path $root "DrivenCharacterRealtime.json"
$runtimeCsvPath = Join-Path $root "DrivenCharacterRealtime.csv"
$mapReportPath = Join-Path $root "physics_control_character_map_setup.json"
$screenshotPath = Join-Path $root "Screenshots\Case05_PhysicsControl_Manny.png"
$mapAssetPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Content\Maps\L_PhysicsControlCharacter.umap"
$sourcePath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Private\PhysicsControlCharacterRealtimeActor.cpp"

$runtime = Get-Content -LiteralPath $runtimeJsonPath -Raw -Encoding UTF8 | ConvertFrom-Json
$samples = @(Import-Csv -LiteralPath $runtimeCsvPath)
$mapReport = Get-Content -LiteralPath $mapReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$last = $samples[-1]

$allFinite = $true
$maxBaselinePose = 0.0
$maxResearchPose = 0.0
$timeStrict = $true
$previousTime = -1.0
foreach ($sample in $samples) {
    $values = @(
        [double]$sample.time_s,
        [double]$sample.baseline_pose_error_deg,
        [double]$sample.research_pose_error_deg,
        [double]$sample.baseline_pelvis_displacement_cm,
        [double]$sample.research_pelvis_displacement_cm
    )
    foreach ($value in $values) {
        if ([double]::IsNaN($value) -or [double]::IsInfinity($value)) { $allFinite = $false }
    }
    if ($values[0] -le $previousTime) { $timeStrict = $false }
    $previousTime = $values[0]
    $maxBaselinePose = [math]::Max($maxBaselinePose, $values[1])
    $maxResearchPose = [math]::Max($maxResearchPose, $values[2])
}

$recomputedPoseRatio = [double]$last.research_pose_error_deg / [double]$last.baseline_pose_error_deg
$recomputedPelvisRatio = [double]$last.research_pelvis_displacement_cm / [double]$last.baseline_pelvis_displacement_cm
$checks = [ordered]@{
    map_saved_without_errors = ([bool]$mapReport.saved -and @($mapReport.errors).Count -eq 0)
    map_asset_exists = (Test-Path -LiteralPath $mapAssetPath)
    exact_engine_build = ($runtime.engine -eq '5.4.4-35576357+++UE5+Release-5.4')
    real_manny_multibody_contract = ([int]$runtime.physics_body_count -eq 22 -and [int]$runtime.research_control_count -eq 22)
    two_real_chaos_paths_named = ($runtime.baseline_backend -eq 'ChaosPassiveRagdoll' -and $runtime.research_backend -eq 'ChaosPhysicsControlActive')
    same_impulse_was_applied = ([bool]$runtime.shared_impulse_applied -and [double]$runtime.shared_impulse_y_kg_cm_per_s -eq 5200.0)
    csv_has_exact_finite_fixed_rate_samples = ($samples.Count -eq 360 -and [int]$runtime.sample_count -eq 360 -and $allFinite -and $timeStrict -and [math]::Abs([double]$last.time_s - 6.0) -lt 0.02)
    csv_recomputes_final_metrics = (
        [math]::Abs([double]$last.baseline_pose_error_deg - [double]$runtime.baseline_final_pose_error_deg) -lt 1.0e-6 -and
        [math]::Abs([double]$last.research_pose_error_deg - [double]$runtime.research_final_pose_error_deg) -lt 1.0e-6 -and
        [math]::Abs([double]$last.baseline_pelvis_displacement_cm - [double]$runtime.baseline_final_pelvis_displacement_cm) -lt 1.0e-6 -and
        [math]::Abs([double]$last.research_pelvis_displacement_cm - [double]$runtime.research_final_pelvis_displacement_cm) -lt 1.0e-6
    )
    csv_recomputes_max_pose_errors = ([math]::Abs($maxBaselinePose - [double]$runtime.baseline_max_pose_error_deg) -lt 1.0e-6 -and [math]::Abs($maxResearchPose - [double]$runtime.research_max_pose_error_deg) -lt 1.0e-6)
    active_control_reduces_final_pose_error_by_half = ([double]$runtime.research_final_pose_error_deg -lt [double]$runtime.baseline_final_pose_error_deg * 0.5)
    active_control_keeps_pelvis_near_reference = ([double]$runtime.research_final_pelvis_displacement_cm -lt 10.0 -and [double]$runtime.baseline_final_pelvis_displacement_cm -gt 100.0)
    exported_ratios_are_independently_recomputed = ([math]::Abs($recomputedPoseRatio - [double]$runtime.final_pose_error_ratio_research_over_baseline) -lt 1.0e-9 -and [math]::Abs($recomputedPelvisRatio - [double]$runtime.final_pelvis_displacement_ratio_research_over_baseline) -lt 1.0e-9)
    controller_advantage_contract_passed = ([bool]$runtime.controller_advantage_pass)
    source_uses_real_physics_asset_and_physics_control = ($source -match 'PA_Mannequin' -and $source -match 'CreateBodyModifiersFromSkeletalMeshBelow' -and $source -match 'CreateControlsFromSkeletalMeshBelow' -and $source -match 'CreateControl\(')
    source_resolves_tick_phase_before_impulse = ($source -match 'SetBodyModifiersInSetMovementType' -and $source -match 'SetAllBodiesBelowSimulatePhysics' -and $source -match 'AddImpulse')
    screenshot_exists_and_is_nontrivial = ((Test-Path -LiteralPath $screenshotPath) -and (Get-Item -LiteralPath $screenshotPath).Length -gt 100000)
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = $(if ($failed.Count -eq 0) { 'passed' } else { 'failed' })
    engine = 'UE 5.4.4 CL 35576357'
    rhi = 'D3D12 SM6'
    scope = 'Manny + PA_Mannequin real Chaos multibody comparison: passive ragdoll versus Epic PhysicsControl active pose/pelvis control under the same impulse.'
    claim_boundary = 'This proves an engine-native multibody control slice and measurable perturbation rejection. It does not yet prove ALS get-up, player interaction, Lyra replicated recovery state, or a neural policy.'
    checks = $checks
    recomputed = [ordered]@{
        samples = $samples.Count
        final_pose_error_ratio_research_over_baseline = $recomputedPoseRatio
        final_pelvis_displacement_ratio_research_over_baseline = $recomputedPelvisRatio
        baseline_max_pose_error_deg = $maxBaselinePose
        research_max_pose_error_deg = $maxResearchPose
    }
    runtime = $runtime
    screenshot = 'Docs/Validation/M6/Screenshots/Case05_PhysicsControl_Manny.png'
    manual_visual_inspection = 'passed: Chinese HUD is fully visible; passive Manny is prone while PhysicsControl Manny remains near its reference location; both meshes and the lit ground plane are visible.'
    preserved_first_failure = 'Docs/Validation/M6/PhysicsControlTargetFailure/index.json'
    failed_checks = $failed
}
$reportPath = Join-Path $root "physics_control_realtime_validation.json"
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 8
if ($failed.Count -gt 0) { exit 1 }
