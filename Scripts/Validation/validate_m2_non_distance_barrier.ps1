param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\NonDistanceBarrierAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\non_distance_barrier_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) {
    throw "缺少 NDB 自动化报告：$automationPath"
}

$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.NonDistanceBarrierSchedule" })[0]
$messages = if ($null -ne $test) { @($test.entries | ForEach-Object { $_.event.message }) } else { @() }
$metricMessage = @($messages | Where-Object { $_ -like "NDB schedule:*" })[0]
$metricMatch = [regex]::Match($metricMessage, "base=(?<base>[0-9.]+) multiplier=(?<multiplier>[0-9.]+) maximum=(?<maximum>[0-9.]+) max_activation=(?<activation>\d+) saturated_updates=(?<saturated>\d+)")

$checks = [ordered]@{
    test_discovered = ($null -ne $test)
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    metrics_present = $metricMatch.Success
    paper_multiplier_k2 = ($metricMatch.Success -and [double]$metricMatch.Groups["multiplier"].Value -eq 2.0)
    consecutive_activation_observed = ($metricMatch.Success -and [int]$metricMatch.Groups["activation"].Value -eq 5)
    safety_weight_cap_observed = ($metricMatch.Success -and [double]$metricMatch.Groups["maximum"].Value -eq 48.0)
    saturation_counted = ($metricMatch.Success -and [int64]$metricMatch.Groups["saturated"].Value -ge 2)
}

$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        base_weight = if ($metricMatch.Success) { [double]$metricMatch.Groups["base"].Value } else { $null }
        activation_multiplier = if ($metricMatch.Success) { [double]$metricMatch.Groups["multiplier"].Value } else { $null }
        maximum_weight = if ($metricMatch.Success) { [double]$metricMatch.Groups["maximum"].Value } else { $null }
        maximum_activation_count = if ($metricMatch.Success) { [int]$metricMatch.Groups["activation"].Value } else { $null }
        saturated_weight_updates = if ($metricMatch.Success) { [int64]$metricMatch.Groups["saturated"].Value } else { $null }
        automation_report = $automationPath
    }
}

$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") {
    throw "NDB 验证失败，详见：$reportPath"
}
Write-Output "M2_NON_DISTANCE_BARRIER_VALIDATION_PASSED report=$reportPath"
