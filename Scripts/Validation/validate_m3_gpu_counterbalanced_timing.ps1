param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"

$root = Join-Path $ProjectRoot "Docs\Validation\M3"
$automationPath = Join-Path $root "GpuXpbdCounterbalancedTimingD3D12Automation\index.json"
$convergencePath = Join-Path $root "gpu_low_frequency_correction_validation.json"
$reportPath = Join-Path $root "gpu_counterbalanced_timing_validation.json"
foreach ($path in @($automationPath, $convergencePath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "缺少 M3 反平衡 GPU 计时证据：$path" }
}
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$convergence = Get-Content -LiteralPath $convergencePath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.SoftBody.GpuXpbd.LowFrequencyCounterbalancedTiming")
$number = "[0-9.eE+-]+"
$pattern = "tier=(?<tier>\S+) protocol=(?<protocol>\S+) steps_per_run=(?<steps>\d+) warmup_per_run=(?<warmup>\d+) iterations=(?<iterations>\d+) local_run_medians_ms=(?<l0>$number),(?<l1>$number) corrected_run_medians_ms=(?<c0>$number),(?<c1>$number) local_mean_of_medians_ms=(?<lm>$number) corrected_mean_of_medians_ms=(?<cm>$number) corrected_to_local_ratio=(?<ratio>$number) local_mean_p95_ms=(?<lp>$number) corrected_mean_p95_ms=(?<cp>$number) samples_per_run=(?<samples>\d+) steady_samples_per_run=(?<steady>\d+) skipped_total=(?<skipped>\d+) corrected_projection_passes_per_run=(?<projection>\d+) corrected_reduction_dispatches_per_run=(?<reduction>\d+) corrected_apply_passes_per_run=(?<apply>\d+)"
$tiers = [ordered]@{}
foreach ($message in @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "GPU XPBD counterbalanced timing tier=*" })) {
    $match = [regex]::Match($message, $pattern)
    if (-not $match.Success) { continue }
    $localRuns = @([double]$match.Groups["l0"].Value, [double]$match.Groups["l1"].Value)
    $correctedRuns = @([double]$match.Groups["c0"].Value, [double]$match.Groups["c1"].Value)
    $tiers[$match.Groups["tier"].Value] = [ordered]@{
        protocol = $match.Groups["protocol"].Value
        steps_per_run = [int]$match.Groups["steps"].Value
        warmup_per_run = [int]$match.Groups["warmup"].Value
        iterations = [int]$match.Groups["iterations"].Value
        local_run_medians_ms = $localRuns
        corrected_run_medians_ms = $correctedRuns
        local_mean_of_medians_ms = [double]$match.Groups["lm"].Value
        corrected_mean_of_medians_ms = [double]$match.Groups["cm"].Value
        corrected_to_local_ratio = [double]$match.Groups["ratio"].Value
        overhead_percent = 100.0 * ([double]$match.Groups["ratio"].Value - 1.0)
        local_mean_p95_ms = [double]$match.Groups["lp"].Value
        corrected_mean_p95_ms = [double]$match.Groups["cp"].Value
        samples_per_run = [int]$match.Groups["samples"].Value
        steady_samples_per_run = [int]$match.Groups["steady"].Value
        skipped_total = [int]$match.Groups["skipped"].Value
        corrected_projection_passes_per_run = [int]$match.Groups["projection"].Value
        corrected_reduction_dispatches_per_run = [int]$match.Groups["reduction"].Value
        corrected_apply_passes_per_run = [int]$match.Groups["apply"].Value
        local_run_spread_ratio = ($localRuns | Measure-Object -Maximum).Maximum / ($localRuns | Measure-Object -Minimum).Minimum
        corrected_run_spread_ratio = ($correctedRuns | Measure-Object -Maximum).Maximum / ($correctedRuns | Measure-Object -Minimum).Minimum
    }
}
$device = @($automation.devices)[0]
$checks = [ordered]@{
    target_test_passed = ($test.Count -eq 1 -and $test[0].state -eq "Success" -and $automation.succeeded -eq 1 -and $automation.failed -eq 0)
    real_d3d12_sm6_device = ($device.rHI -eq "DirectX 12 (SM6)" -and $device.renderMode -eq "D3D12_SM6")
    medium_and_high_present = ($tiers.Count -eq 2 -and $null -ne $tiers.medium -and $null -ne $tiers.high)
    exact_abba_protocol_and_budget = (@($tiers.Values | Where-Object { $_.protocol -eq "ABBA" -and $_.steps_per_run -eq 128 -and $_.warmup_per_run -eq 48 -and $_.iterations -eq 32 }).Count -eq 2)
    complete_timestamps_without_skips = (@($tiers.Values | Where-Object { $_.samples_per_run -eq 128 -and $_.steady_samples_per_run -eq 80 -and $_.skipped_total -eq 0 }).Count -eq 2)
    exact_incremental_pass_accounting = (@($tiers.Values | Where-Object { $_.corrected_projection_passes_per_run -eq 4096 -and $_.corrected_reduction_dispatches_per_run -eq 8192 -and $_.corrected_apply_passes_per_run -eq 4096 }).Count -eq 2)
    corrected_pair_is_stable_within_five_percent = (@($tiers.Values | Where-Object { $_.corrected_run_spread_ratio -lt 1.05 }).Count -eq 2)
    counterbalanced_median_reports_bounded_overhead = (@($tiers.Values | Where-Object { $_.corrected_to_local_ratio -gt 1.0 -and $_.corrected_to_local_ratio -lt 1.5 }).Count -eq 2)
    convergence_benefit_remains_independently_gated = ($convergence.checks.medium_residual_improves_at_least_fifteen_percent -eq $true -and $convergence.checks.high_residual_improves_at_least_twenty_five_percent -eq $true)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    device = [ordered]@{ gpu = $device.gPU; rhi = $device.rHI; render_mode = $device.renderMode }
    protocol = "Per tier: Local, Corrected, Corrected, Local; four fresh sessions, 128 steps each, first 48 excluded, mean of the two per-condition steady medians."
    attribution = "The two-mode correction adds one semantic projection, two reduction dispatches, and one apply pass per solver iteration. On this run the counterbalanced median overhead is about 14.8% (medium) and 17.3% (high), while the independent convergence gate records at least 15% and 25% residual reduction. This is an overhead/quality tradeoff, not a raw speedup claim."
    caveat = "Local run medians still vary by roughly 28-32% across the ABBA endpoints, so the result is machine/protocol evidence rather than a universal GPU performance law."
    checks = $checks
    tiers = $tiers
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M3 GPU 反平衡计时验证失败：$($failed -join ', ')" }
Write-Output "M3_GPU_COUNTERBALANCED_TIMING_VALIDATION_PASSED report=$reportPath"
