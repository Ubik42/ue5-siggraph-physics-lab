param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\ResidualReusePolicyAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\residual_reuse_policy_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) {
    throw "缺少残差复用安全策略报告：$automationPath"
}
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.ResidualReuseSafetyPolicy" })[0]
$message = @($test.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "Residual reuse safety:*" })[0]
$match = [regex]::Match($message, "accepted=(?<accepted>\d+) topology=(?<topology>\d+) residual=(?<residual>\d+) ccd=(?<ccd>\d+) nonfinite=(?<nonfinite>\d+) scale=(?<scale>[0-9.]+)")
$checks = [ordered]@{
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    d3d12_sm6 = (@($automation.devices | Where-Object { $_.renderMode -eq "D3D12_SM6" }).Count -eq 1)
    metrics_present = $match.Success
    one_safe_sample_accepted = ($match.Success -and [int]$match.Groups["accepted"].Value -eq 1)
    topology_fallback_covered = ($match.Success -and [int]$match.Groups["topology"].Value -eq 1)
    residual_fallback_covered = ($match.Success -and [int]$match.Groups["residual"].Value -eq 1)
    ccd_fallback_covered = ($match.Success -and [int]$match.Groups["ccd"].Value -eq 1)
    nonfinite_fallback_covered = ($match.Success -and [int]$match.Groups["nonfinite"].Value -eq 1)
    configured_scale_preserved = ($match.Success -and [math]::Abs([double]$match.Groups["scale"].Value - 0.4) -lt 0.0001)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        automation_report = $automationPath
        policy_scope = "CPU truth contract; GPU reduction integration pending"
    }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "残差复用安全策略验证失败：$reportPath" }
Write-Output "M2_RESIDUAL_REUSE_POLICY_VALIDATION_PASSED report=$reportPath"
