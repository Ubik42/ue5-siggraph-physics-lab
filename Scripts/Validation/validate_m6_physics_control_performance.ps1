param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')))

$ErrorActionPreference = 'Stop'
$root = Join-Path $ProjectRoot 'Docs\Validation\M6'
$capture = Get-Content -LiteralPath (Join-Path $root 'physics_control_performance_capture.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$source = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Private\MeasuredPhysicsControlComponent.cpp') -Raw -Encoding UTF8
$actorSource = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Private\PhysicsControlCharacterRealtimeActor.cpp') -Raw -Encoding UTF8

function Get-NearestRank([double[]]$Values, [double]$Percentile) {
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 0) { return 0.0 }
    $index = [math]::Max(0, [math]::Min($sorted.Count - 1, [math]::Ceiling($Percentile * $sorted.Count) - 1))
    return [double]$sorted[$index]
}

$trialReports = @()
$pooledFrame = [Collections.Generic.List[double]]::new()
$pooledControl = [Collections.Generic.List[double]]::new()
$trialContractsPass = $true
$summaryMatchesCsv = $true
$physicalStateBounded = $true
foreach ($trial in 1..3) {
    $suffix = $trial.ToString('00')
    $runtime = Get-Content -LiteralPath (Join-Path $root "DrivenCharacterPerformance_Trial$suffix.json") -Raw -Encoding UTF8 | ConvertFrom-Json
    $samples = @(Import-Csv -LiteralPath (Join-Path $root "DrivenCharacterPerformance_Trial$suffix.csv"))
    $measured = @($samples | Select-Object -Skip 300)
    $frame = @($measured | ForEach-Object { [double]$_.frame_wall_ms })
    $control = @($measured | ForEach-Object { [double]$_.physics_control_update_ms })
    $frame | ForEach-Object { $pooledFrame.Add($_) }
    $control | ForEach-Object { $pooledControl.Add($_) }
    $finite = @($frame + $control | Where-Object { -not [double]::IsFinite($_) }).Count -eq 0
    $frameP95 = Get-NearestRank $frame 0.95
    $frameP99 = Get-NearestRank $frame 0.99
    $controlP95 = Get-NearestRank $control 0.95
    $controlP99 = Get-NearestRank $control 0.99
    $contract = $runtime.engine -eq '5.4.4-35576357+++UE5+Release-5.4' -and $runtime.scenario -eq 'SharedSpineImpulsePerformance' -and
        $runtime.performance_rhi -eq 'NullRHI' -and $runtime.fixed_step_hz -eq 60 -and $runtime.scene_rigid_body_count -eq 44 -and
        $runtime.research_control_count -eq 22 -and $samples.Count -eq 1800 -and $runtime.performance_warmup_samples -eq 300 -and
        $measured.Count -eq 1500 -and $runtime.performance_measurement_samples -eq 1500 -and $finite
    $summaryMatch = [math]::Abs($frameP95 - $runtime.frame_wall_p95_ms) -lt 1.0e-6 -and [math]::Abs($frameP99 - $runtime.frame_wall_p99_ms) -lt 1.0e-6 -and
        [math]::Abs($controlP95 - $runtime.physics_control_update_p95_ms) -lt 1.0e-6 -and [math]::Abs($controlP99 - $runtime.physics_control_update_p99_ms) -lt 1.0e-6
    $bounded = $runtime.baseline_final_pelvis_displacement_cm -lt 2000 -and $runtime.baseline_peak_downward_speed_cm_s -lt 600 -and
        $runtime.research_final_pelvis_displacement_cm -lt 10 -and $runtime.controller_advantage_pass
    $trialContractsPass = $trialContractsPass -and $contract
    $summaryMatchesCsv = $summaryMatchesCsv -and $summaryMatch
    $physicalStateBounded = $physicalStateBounded -and $bounded
    $trialReports += [ordered]@{
        trial = $trial
        contract_pass = $contract
        summary_matches_csv = $summaryMatch
        physical_state_bounded = $bounded
        frame_wall_p95_ms = $frameP95
        frame_wall_p99_ms = $frameP99
        physics_control_update_p95_ms = $controlP95
        physics_control_update_p99_ms = $controlP99
    }
}

$pooledFrameP50 = Get-NearestRank $pooledFrame.ToArray() 0.50
$pooledFrameP95 = Get-NearestRank $pooledFrame.ToArray() 0.95
$pooledFrameP99 = Get-NearestRank $pooledFrame.ToArray() 0.99
$pooledControlP50 = Get-NearestRank $pooledControl.ToArray() 0.50
$pooledControlP95 = Get-NearestRank $pooledControl.ToArray() 0.95
$pooledControlP99 = Get-NearestRank $pooledControl.ToArray() 0.99
$checks = [ordered]@{
    capture_has_three_owned_trials = [bool](@($capture.trials).Count -eq 3 -and @($capture.trials | Where-Object { $_.owned_pid -le 0 }).Count -eq 0)
    all_trial_contracts_pass = [bool]$trialContractsPass
    runtime_summaries_match_csv = [bool]$summaryMatchesCsv
    all_trials_keep_physical_state_bounded = [bool]$physicalStateBounded
    pooled_sample_count_is_4500 = [bool]($pooledFrame.Count -eq 4500 -and $pooledControl.Count -eq 4500)
    end_to_end_frame_p95_under_4ms = [bool]($pooledFrameP95 -lt 4.0)
    end_to_end_frame_p99_under_6ms = [bool]($pooledFrameP99 -lt 6.0)
    every_trial_frame_p99_under_8ms = [bool](@($trialReports | Where-Object { $_.frame_wall_p99_ms -ge 8.0 }).Count -eq 0)
    physics_control_p95_under_0_2ms = [bool]($pooledControlP95 -lt 0.2)
    physics_control_p99_under_0_3ms = [bool]($pooledControlP99 -lt 0.3)
    every_trial_control_p99_under_0_4ms = [bool](@($trialReports | Where-Object { $_.physics_control_update_p99_ms -ge 0.4 }).Count -eq 0)
    source_times_full_epic_component_tick = [bool]($source -match 'Super::TickComponent' -and $source -match 'Cycles64' -and $actorSource -match 'bPerformanceMode \? FVector\(100\.0, 100\.0, 0\.2\)')
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$result = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = ($failed.Count -eq 0 ? 'passed' : 'failed')
    protocol = 'Three isolated UE game processes, fixed 60 Hz simulation, NullRHI, 1800 samples each, first 300 discarded.'
    scope = 'End-to-end wall interval covers the isolated frame with 44 Chaos bodies; the separately attributed timer wraps Epic UPhysicsControlComponent::TickComponent for 22 controls/body modifiers.'
    claim_boundary = 'NullRHI removes rendering cost. Frame wall time is not named Chaos solver time, and it is not subtracted from the PhysicsControl timer. Results are machine-specific CPU evidence, not a cross-platform shipping budget.'
    pooled = [ordered]@{
        measurement_samples = $pooledFrame.Count
        frame_wall_p50_ms = $pooledFrameP50
        frame_wall_p95_ms = $pooledFrameP95
        frame_wall_p99_ms = $pooledFrameP99
        physics_control_update_p50_ms = $pooledControlP50
        physics_control_update_p95_ms = $pooledControlP95
        physics_control_update_p99_ms = $pooledControlP99
    }
    trials = $trialReports
    checks = $checks
    failed_checks = $failed
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root 'physics_control_performance_validation.json') -Encoding UTF8
$result | ConvertTo-Json -Depth 8
if ($failed.Count -gt 0) { exit 1 }
