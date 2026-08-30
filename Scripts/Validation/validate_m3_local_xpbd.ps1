param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M3\LocalXpbdMaterialAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M3\local_xpbd_validation.json"
$curvePath = Join-Path $ProjectRoot "Docs\Validation\M3\local_xpbd_convergence.csv"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少局部 XPBD 自动化报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.SoftBody.LocalXpbd.Cantilever")
$messages = @($test[0].entries | ForEach-Object { $_.event.message })
$summaryMessage = @($messages | Where-Object { $_ -like "Local XPBD cantilever:*" })[0]
$curveMessage = @($messages | Where-Object { $_ -like "Local XPBD convergence curve:*" })[0]
$number = "[0-9.eE+-]+"
$pattern = "steps=(?<steps>\d+) iterations=(?<iterations>\d+) vertices=(?<vertices>\d+) tets=(?<tets>\d+) distance_constraints=(?<distance>\d+) volume_constraints=(?<volume>\d+) residual_rms=(?<residual>$number) edge_relative_max=(?<edge>$number) volume_relative_mean=(?<volmean>$number) volume_relative_max=(?<volmax>$number) total_volume_ratio=(?<ratio>$number) tip_displacement_cm=(?<tip>$number) fixed_drift_cm=(?<drift>$number) first_step_one_iter_residual=(?<one>$number) first_step_full_iter_residual=(?<full>$number) curve_r1=(?<r1>$number) curve_r8=(?<r8>$number) curve_r16=(?<r16>$number) curve_r32=(?<r32>$number) curve_r64=(?<r64>$number) mean_step_ms=(?<meanms>$number) maximum_step_ms=(?<maxms>$number)"
$match = [regex]::Match($summaryMessage, $pattern)
$curve = if ($curveMessage) { @(($curveMessage -replace '^Local XPBD convergence curve:\s*', '') -split ',' | ForEach-Object { [double]$_ }) } else { @() }
$curveMonotone = $curve.Count -eq 64
for ($index = 1; $index -lt $curve.Count; ++$index) {
    if ($curve[$index] -gt $curve[$index - 1] + 1e-12) { $curveMonotone = $false }
}
function Metric([string]$Name) { if ($match.Success) { [double]$match.Groups[$Name].Value } else { [double]::NaN } }
function CountMetric([string]$Name) { if ($match.Success) { [int]$match.Groups[$Name].Value } else { -1 } }
$checks = [ordered]@{
    target_test_passed = ($test.Count -eq 1 -and $test[0].state -eq "Success")
    combined_local_suite_zero_failures = ([int]$automation.succeeded -eq 2 -and [int]$automation.failed -eq 0)
    zero_test_warnings_and_errors = ([int]$test[0].warnings -eq 0 -and [int]$test[0].errors -eq 0)
    summary_metrics_present = $match.Success
    exact_low_tier_contract = ($match.Success -and (CountMetric "vertices") -eq 45 -and (CountMetric "tets") -eq 96 -and (CountMetric "distance") -eq 180 -and (CountMetric "volume") -eq 96)
    fixed_budget_completed = ($match.Success -and (CountMetric "steps") -eq 120 -and (CountMetric "iterations") -eq 64)
    full_budget_beats_one_iteration = ($match.Success -and (Metric "full") -lt (Metric "one"))
    full_curve_is_monotone = ($curveMonotone -and $curve[-1] -lt $curve[0])
    residual_bounded = ($match.Success -and (Metric "residual") -lt 0.15)
    edge_error_bounded = ($match.Success -and (Metric "edge") -lt 0.25)
    volume_error_bounded = ($match.Success -and (Metric "volmean") -lt 0.10 -and (Metric "volmax") -lt 0.20)
    total_volume_ratio_bounded = ($match.Success -and (Metric "ratio") -gt 0.75 -and (Metric "ratio") -lt 1.25)
    cantilever_tip_moves_down = ($match.Success -and (Metric "tip") -gt 1.0 -and (Metric "tip") -lt 200.0)
    fixed_face_zero_drift = ($match.Success -and (Metric "drift") -lt 1e-9)
    cpu_step_cost_recorded = ($match.Success -and (Metric "meanms") -gt 0.0 -and (Metric "meanms") -lt 20.0 -and (Metric "maxms") -lt 50.0)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$curveRows = for ($index = 0; $index -lt $curve.Count; ++$index) {
    [pscustomobject]@{ iteration = $index + 1; residual_rms = $curve[$index] }
}
$curveRows | Export-Csv -LiteralPath $curvePath -NoTypeInformation -Encoding UTF8
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    checks = $checks
    metrics = if ($match.Success) { [ordered]@{
        steps = CountMetric "steps"; iterations = CountMetric "iterations"; vertices = CountMetric "vertices"; tetrahedra = CountMetric "tets"
        distance_constraints = CountMetric "distance"; volume_constraints = CountMetric "volume"
        residual_rms = Metric "residual"; maximum_edge_relative_error = Metric "edge"
        mean_volume_relative_error = Metric "volmean"; maximum_volume_relative_error = Metric "volmax"
        total_volume_ratio = Metric "ratio"; tip_displacement_cm = Metric "tip"; fixed_drift_cm = Metric "drift"
        first_step_one_iteration_residual = Metric "one"; first_step_full_iteration_residual = Metric "full"
        mean_cpu_step_ms = Metric "meanms"; maximum_cpu_step_ms = Metric "maxms"
    } } else { $null }
    convergence_curve_csv = "Docs/Validation/M3/local_xpbd_convergence.csv"
    convergence_curve_samples = $curve.Count
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M3 局部 XPBD 验证失败：$($failed -join ', ')" }
Write-Output "M3_LOCAL_XPBD_VALIDATION_PASSED report=$reportPath"
