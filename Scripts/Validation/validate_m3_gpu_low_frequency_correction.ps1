param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M3\GpuXpbdLowFrequencyParallelD3D12Automation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M3\gpu_low_frequency_correction_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少 GPU 低频消融报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.SoftBody.GpuXpbd.LowFrequencyAblation")
$number = "[0-9.eE+-]+"
$pattern = "tier=(?<tier>\S+) steps=(?<steps>\d+) iterations=(?<iterations>\d+) local_residual=(?<lr>$number) corrected_residual=(?<cr>$number) residual_ratio=(?<rr>$number) local_edge_max=(?<le>$number) corrected_edge_max=(?<ce>$number) local_volume_mean=(?<lv>$number) corrected_volume_mean=(?<cv>$number) local_tip_cm=(?<lt>$number) corrected_tip_cm=(?<ct>$number) local_fixed_drift_cm=(?<ld>$number) corrected_fixed_drift_cm=(?<cd>$number) local_timing_samples=(?<lts>\d+) corrected_timing_samples=(?<cts>\d+) local_timing_skipped=(?<lskip>\d+) corrected_timing_skipped=(?<cskip>\d+) local_steady_median_ms=(?<lm>$number) corrected_steady_median_ms=(?<cm>$number) timing_ratio=(?<tr>$number) local_steady_p95_ms=(?<lp>$number) corrected_steady_p95_ms=(?<cp>$number) projection_passes=(?<pp>\d+) reduction_dispatches=(?<rd>\d+) apply_passes=(?<ap>\d+) modes=(?<modes>\d+)"
$tiers = [ordered]@{}
foreach ($message in @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "GPU XPBD low frequency ablation tier=*" })) {
    $match = [regex]::Match($message, $pattern)
    if (-not $match.Success) { continue }
    $tiers[$match.Groups["tier"].Value] = [ordered]@{
        steps = [int]$match.Groups["steps"].Value; iterations = [int]$match.Groups["iterations"].Value
        local_residual_rms = [double]$match.Groups["lr"].Value; corrected_residual_rms = [double]$match.Groups["cr"].Value; residual_ratio = [double]$match.Groups["rr"].Value
        local_maximum_edge_relative_error = [double]$match.Groups["le"].Value; corrected_maximum_edge_relative_error = [double]$match.Groups["ce"].Value
        local_mean_volume_relative_error = [double]$match.Groups["lv"].Value; corrected_mean_volume_relative_error = [double]$match.Groups["cv"].Value
        local_tip_cm = [double]$match.Groups["lt"].Value; corrected_tip_cm = [double]$match.Groups["ct"].Value
        local_fixed_drift_cm = [double]$match.Groups["ld"].Value; corrected_fixed_drift_cm = [double]$match.Groups["cd"].Value
        local_timing_samples = [int]$match.Groups["lts"].Value; corrected_timing_samples = [int]$match.Groups["cts"].Value
        local_timing_skipped = [int]$match.Groups["lskip"].Value; corrected_timing_skipped = [int]$match.Groups["cskip"].Value
        local_steady_median_ms = [double]$match.Groups["lm"].Value; corrected_steady_median_ms = [double]$match.Groups["cm"].Value; timing_ratio = [double]$match.Groups["tr"].Value
        local_steady_p95_ms = [double]$match.Groups["lp"].Value; corrected_steady_p95_ms = [double]$match.Groups["cp"].Value
        projection_passes = [int]$match.Groups["pp"].Value; reduction_dispatches = [int]$match.Groups["rd"].Value; apply_passes = [int]$match.Groups["ap"].Value; modes = [int]$match.Groups["modes"].Value
    }
}
$device = @($automation.devices)[0]
$checks = [ordered]@{
    target_test_passed = ($test.Count -eq 1 -and $test[0].state -eq "Success")
    zero_failures_warnings_errors = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0 -and [int]$test[0].warnings -eq 0 -and [int]$test[0].errors -eq 0)
    real_d3d12_sm6_device = ($device.rHI -eq "DirectX 12 (SM6)" -and $device.renderMode -eq "D3D12_SM6")
    exact_medium_high_contract = ($tiers.Count -eq 2 -and $null -ne $tiers.medium -and $null -ne $tiers.high)
    identical_budget = (@($tiers.Values | Where-Object { $_.steps -eq 120 -and $_.iterations -eq 32 }).Count -eq 2)
    exact_two_mode_parallel_reduction_dispatches = (@($tiers.Values | Where-Object { $_.projection_passes -eq 3840 -and $_.reduction_dispatches -eq 7680 -and $_.apply_passes -eq 3840 -and $_.modes -eq 2 }).Count -eq 2)
    complete_nonblocking_timestamps = (@($tiers.Values | Where-Object { $_.local_timing_samples -eq 120 -and $_.corrected_timing_samples -eq 120 -and $_.local_timing_skipped -eq 0 -and $_.corrected_timing_skipped -eq 0 }).Count -eq 2)
    fixed_faces_zero_drift = (@($tiers.Values | Where-Object { $_.local_fixed_drift_cm -lt 0.001 -and $_.corrected_fixed_drift_cm -lt 0.001 }).Count -eq 2)
    residual_edge_volume_all_improve = (@($tiers.Values | Where-Object { $_.corrected_residual_rms -lt $_.local_residual_rms -and $_.corrected_maximum_edge_relative_error -lt $_.local_maximum_edge_relative_error -and $_.corrected_mean_volume_relative_error -lt $_.local_mean_volume_relative_error }).Count -eq 2)
    medium_residual_improves_at_least_fifteen_percent = ($tiers.medium.residual_ratio -lt 0.85)
    high_residual_improves_at_least_twenty_five_percent = ($tiers.high.residual_ratio -lt 0.75)
    finite_positive_timing_observed = (@($tiers.Values | Where-Object { $_.local_steady_median_ms -gt 0.0 -and $_.corrected_steady_median_ms -gt 0.0 -and $_.local_steady_p95_ms -gt 0.0 -and $_.corrected_steady_p95_ms -gt 0.0 }).Count -eq 2)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    device = [ordered]@{ gpu = $device.gPU; rhi = $device.rHI; render_mode = $device.renderMode }
    method = "two analytic global modes; per-iteration 64-thread group partial reduction, tiny final reduction, and vertex apply"
    checks = $checks
    tiers = $tiers
    timing_interpretation = "This report gates convergence and pass accounting. Efficiency attribution is provided separately by gpu_counterbalanced_timing_validation.json using fresh-session ABBA ordering and longer per-session warmup."
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M3 GPU 低频校正验证失败：$($failed -join ', ')" }
Write-Output "M3_GPU_LOW_FREQUENCY_VALIDATION_PASSED report=$reportPath"
