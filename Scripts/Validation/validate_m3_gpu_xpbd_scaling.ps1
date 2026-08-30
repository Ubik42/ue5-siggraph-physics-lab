param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M3\GpuXpbdScalingD3D12Automation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M3\gpu_xpbd_scaling_validation.json"
$csvPath = Join-Path $ProjectRoot "Docs\Validation\M3\gpu_xpbd_timing_samples.csv"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少 GPU XPBD 三档扩展性报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.SoftBody.GpuXpbd.ResolutionScaling")
$messages = @($test[0].entries | ForEach-Object { $_.event.message })
$number = "[0-9.eE+-]+"
$pattern = "tier=(?<tier>\S+) cells=(?<cells>\S+) vertices=(?<vertices>\d+) edges=(?<edges>\d+) tets=(?<tets>\d+) steps=(?<steps>\d+) iterations=(?<iterations>\d+) timing_samples=(?<samples>\d+) timing_skipped=(?<skipped>\d+) steady_samples=(?<steady>\d+) mean_ms=(?<mean>$number) median_ms=(?<median>$number) p95_ms=(?<p95>$number) p99_ms=(?<p99>$number) max_ms=(?<max>$number) steady_mean_ms=(?<smean>$number) steady_median_ms=(?<smedian>$number) steady_p95_ms=(?<sp95>$number) steady_p99_ms=(?<sp99>$number) steady_max_ms=(?<smax>$number) residual=(?<residual>$number) edge_max=(?<edge>$number) volume_mean=(?<volume>$number) fixed_drift_cm=(?<drift>$number)"
$tiers = [ordered]@{}
foreach ($message in @($messages | Where-Object { $_ -like "GPU XPBD scaling tier=*" })) {
    $match = [regex]::Match($message, $pattern)
    if ($match.Success) {
        $tiers[$match.Groups["tier"].Value] = [ordered]@{
            cells = $match.Groups["cells"].Value
            vertices = [int]$match.Groups["vertices"].Value; edges = [int]$match.Groups["edges"].Value; tetrahedra = [int]$match.Groups["tets"].Value
            steps = [int]$match.Groups["steps"].Value; iterations = [int]$match.Groups["iterations"].Value
            timing_samples = [int]$match.Groups["samples"].Value; timing_skipped = [int]$match.Groups["skipped"].Value; steady_samples = [int]$match.Groups["steady"].Value
            mean_ms = [double]$match.Groups["mean"].Value; median_ms = [double]$match.Groups["median"].Value
            p95_ms = [double]$match.Groups["p95"].Value; p99_ms = [double]$match.Groups["p99"].Value; maximum_ms = [double]$match.Groups["max"].Value
            steady_mean_ms = [double]$match.Groups["smean"].Value; steady_median_ms = [double]$match.Groups["smedian"].Value
            steady_p95_ms = [double]$match.Groups["sp95"].Value; steady_p99_ms = [double]$match.Groups["sp99"].Value; steady_maximum_ms = [double]$match.Groups["smax"].Value
            residual_rms = [double]$match.Groups["residual"].Value; maximum_edge_relative_error = [double]$match.Groups["edge"].Value
            mean_volume_relative_error = [double]$match.Groups["volume"].Value; fixed_drift_cm = [double]$match.Groups["drift"].Value
        }
    }
}
$sampleRows = @()
foreach ($message in @($messages | Where-Object { $_ -like "GPU XPBD timing samples tier=*" })) {
    $match = [regex]::Match($message, "tier=(?<tier>\S+):\s*(?<samples>.*)$")
    if (-not $match.Success) { continue }
    foreach ($sample in ($match.Groups["samples"].Value -split ',')) {
        $pair = $sample -split ':'
        if ($pair.Count -eq 2) {
            $step = [int]$pair[0]
            $sampleRows += [pscustomobject]@{ tier = $match.Groups["tier"].Value; step = $step; gpu_step_ms = [double]$pair[1]; is_steady = ($step -gt 6) }
        }
    }
}
function TierMatches([string]$Name, [string]$Cells, [int]$Vertices, [int]$Edges, [int]$Tets) {
    $tier = $tiers[$Name]
    return $null -ne $tier -and $tier.cells -eq $Cells -and $tier.vertices -eq $Vertices -and $tier.edges -eq $Edges -and $tier.tetrahedra -eq $Tets
}
$device = @($automation.devices)[0]
$checks = [ordered]@{
    target_test_passed = ($test.Count -eq 1 -and $test[0].state -eq "Success")
    zero_failures_warnings_errors = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0 -and [int]$test[0].warnings -eq 0 -and [int]$test[0].errors -eq 0)
    real_d3d12_sm6_device = ($device.rHI -eq "DirectX 12 (SM6)" -and $device.renderMode -eq "D3D12_SM6")
    exact_three_tier_contract = ((TierMatches "low" "4x2x2" 45 180 96) -and (TierMatches "medium" "8x4x4" 225 1152 768) -and (TierMatches "high" "16x8x8" 1377 8160 6144))
    identical_step_iteration_budget = (@($tiers.Values | Where-Object { $_.steps -eq 24 -and $_.iterations -eq 32 }).Count -eq 3)
    complete_nonblocking_timestamps = (@($tiers.Values | Where-Object { $_.timing_samples -eq 24 -and $_.timing_skipped -eq 0 -and $_.steady_samples -eq 18 }).Count -eq 3)
    raw_csv_has_all_samples = ($sampleRows.Count -eq 72)
    fixed_faces_zero_drift = (@($tiers.Values | Where-Object { $_.fixed_drift_cm -lt 0.001 }).Count -eq 3)
    finite_positive_timing = (@($tiers.Values | Where-Object { $_.steady_median_ms -gt 0.0 -and $_.steady_p95_ms -gt 0.0 -and $_.steady_p99_ms -gt 0.0 }).Count -eq 3)
    median_cost_strictly_increases = ($tiers.low.steady_median_ms -lt $tiers.medium.steady_median_ms -and $tiers.medium.steady_median_ms -lt $tiers.high.steady_median_ms)
    high_median_at_least_twenty_percent_above_low = ($tiers.high.steady_median_ms -gt $tiers.low.steady_median_ms * 1.2)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$sampleRows | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    device = [ordered]@{ gpu = $device.gPU; rhi = $device.rHI; render_mode = $device.renderMode }
    timing_scope = "absolute D3D12 timestamp around one complete fixed-step RDG graph; query polling never waits for GPU"
    warmup_steps_excluded = 6
    checks = $checks
    tiers = $tiers
    raw_timing_csv = "Docs/Validation/M3/gpu_xpbd_timing_samples.csv"
    interpretation = "Low and medium tiers remain partly dispatch/parallelism bound; high tier raises the steady median without claiming linear scaling. P95/P99/max are retained independently."
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M3 GPU XPBD 扩展性验证失败：$($failed -join ', ')" }
Write-Output "M3_GPU_XPBD_SCALING_VALIDATION_PASSED report=$reportPath csv=$csvPath"
