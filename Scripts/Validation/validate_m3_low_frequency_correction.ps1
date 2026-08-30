param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M3\LowFrequencyAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M3\low_frequency_correction_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少低频校正自动化报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.SoftBody.LowFrequency.CantileverAblation")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "Low-frequency cantilever ablation:*" })[0]
$number = "[0-9.eE+-]+"
$pattern = "steps=(?<steps>\d+) iterations=(?<iterations>\d+) strength=(?<strength>$number) interval=(?<interval>\d+) modes=(?<modes>\d+) passes_last_step=(?<passes>\d+) max_global_correction_cm=(?<maxcorrection>$number) local_first_residual=(?<localfirst>$number) global_first_residual=(?<globalfirst>$number) local_final_residual=(?<localfinal>$number) global_final_residual=(?<globalfinal>$number) residual_ratio=(?<ratio>$number) local_edge_max=(?<localedge>$number) global_edge_max=(?<globaledge>$number) local_volume_mean=(?<localvolume>$number) global_volume_mean=(?<globalvolume>$number) local_tip_cm=(?<localtip>$number) global_tip_cm=(?<globaltip>$number) local_fixed_drift=(?<localdrift>$number) global_fixed_drift=(?<globaldrift>$number)"
$match = [regex]::Match($message, $pattern)
function Metric([string]$Name) { if ($match.Success) { [double]$match.Groups[$Name].Value } else { [double]::NaN } }
function CountMetric([string]$Name) { if ($match.Success) { [int]$match.Groups[$Name].Value } else { -1 } }
$checks = [ordered]@{
    target_test_passed = ($test.Count -eq 1 -and $test[0].state -eq "Success")
    isolated_suite_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    zero_test_warnings_and_errors = ([int]$test[0].warnings -eq 0 -and [int]$test[0].errors -eq 0)
    summary_metrics_present = $match.Success
    exact_equal_budget = ($match.Success -and (CountMetric "steps") -eq 120 -and (CountMetric "iterations") -eq 32)
    two_modes_run_every_iteration = ($match.Success -and (CountMetric "modes") -eq 2 -and (CountMetric "passes") -eq 32 -and (CountMetric "interval") -eq 1)
    final_residual_improves_at_least_five_percent = ($match.Success -and (Metric "globalfinal") -lt (Metric "localfinal") * 0.95 -and (Metric "ratio") -lt 0.95)
    edge_error_improves_and_is_bounded = ($match.Success -and (Metric "globaledge") -lt (Metric "localedge") -and (Metric "globaledge") -lt 0.35)
    volume_error_improves_and_is_bounded = ($match.Success -and (Metric "globalvolume") -lt (Metric "localvolume") -and (Metric "globalvolume") -lt 0.15)
    fixed_face_zero_drift = ($match.Success -and (Metric "localdrift") -lt 1e-9 -and (Metric "globaldrift") -lt 1e-9)
    correction_is_finite_and_bounded = ($match.Success -and (Metric "maxcorrection") -gt 0.0 -and (Metric "maxcorrection") -lt 2.0)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    checks = $checks
    method = [ordered]@{
        name = "two-mode global low-frequency correction"
        basis = @("normalized dominant-axis linear mode", "normalized dominant-axis quadratic mode")
        boundary = "engineering subset inspired by global-response methods; not full MGPBD, UA-AMG, or JGS2"
    }
    metrics = if ($match.Success) { [ordered]@{
        steps = CountMetric "steps"; iterations_per_step = CountMetric "iterations"; strength = Metric "strength"
        correction_interval = CountMetric "interval"; mode_count = CountMetric "modes"; passes_last_step = CountMetric "passes"
        maximum_global_correction_cm = Metric "maxcorrection"
        local_first_step_residual_rms = Metric "localfirst"; corrected_first_step_residual_rms = Metric "globalfirst"
        local_final_residual_rms = Metric "localfinal"; corrected_final_residual_rms = Metric "globalfinal"; residual_ratio = Metric "ratio"
        local_maximum_edge_relative_error = Metric "localedge"; corrected_maximum_edge_relative_error = Metric "globaledge"
        local_mean_volume_relative_error = Metric "localvolume"; corrected_mean_volume_relative_error = Metric "globalvolume"
        local_tip_displacement_cm = Metric "localtip"; corrected_tip_displacement_cm = Metric "globaltip"
        local_fixed_drift_cm = Metric "localdrift"; corrected_fixed_drift_cm = Metric "globaldrift"
    } } else { $null }
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M3 低频校正验证失败：$($failed -join ', ')" }
Write-Output "M3_LOW_FREQUENCY_CORRECTION_VALIDATION_PASSED report=$reportPath"
