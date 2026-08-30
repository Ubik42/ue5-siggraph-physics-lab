param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\CompleteCcdAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\complete_ccd_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) {
    throw "缺少完整 CCD 自动化报告：$automationPath"
}

$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.CompleteCcdBoundary" })[0]
$messages = if ($null -ne $test) { @($test.entries | ForEach-Object { $_.event.message }) } else { @() }
$metricMessage = @($messages | Where-Object { $_ -like "Complete CCD boundary:*" })[0]
$metricMatch = [regex]::Match($metricMessage, "sphere_toi=(?<sphere>[0-9.]+) capsule_toi=(?<capsule>[0-9.]+) cap_toi=(?<cap>[0-9.]+) discrete_endpoint_penetration=(?<penetration>[0-9.]+)")

$checks = [ordered]@{
    test_discovered = ($null -ne $test)
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    metrics_present = $metricMatch.Success
    sphere_toi_exact = ($metricMatch.Success -and [math]::Abs([double]$metricMatch.Groups["sphere"].Value - 0.45) -lt 1e-9)
    capsule_cylinder_toi_exact = ($metricMatch.Success -and [math]::Abs([double]$metricMatch.Groups["capsule"].Value - 0.45) -lt 1e-9)
    capsule_cap_toi_exact = ($metricMatch.Success -and [math]::Abs([double]$metricMatch.Groups["cap"].Value - 0.625) -lt 1e-9)
    discrete_endpoints_miss_tunneling = ($metricMatch.Success -and [double]$metricMatch.Groups["penetration"].Value -eq 0.0)
}

$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        sphere_toi = if ($metricMatch.Success) { [double]$metricMatch.Groups["sphere"].Value } else { $null }
        capsule_cylinder_toi = if ($metricMatch.Success) { [double]$metricMatch.Groups["capsule"].Value } else { $null }
        capsule_cap_toi = if ($metricMatch.Success) { [double]$metricMatch.Groups["cap"].Value } else { $null }
        discrete_endpoint_penetration = if ($metricMatch.Success) { [double]$metricMatch.Groups["penetration"].Value } else { $null }
        automation_report = $automationPath
    }
}

$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") {
    throw "完整 CCD 验证失败，详见：$reportPath"
}
Write-Output "M2_COMPLETE_CCD_VALIDATION_PASSED report=$reportPath"
