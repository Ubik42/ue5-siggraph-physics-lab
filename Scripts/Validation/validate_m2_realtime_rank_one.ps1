param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\RealtimeRankOneAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\realtime_rank_one_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少实时 rank-one 自动化报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.GpuRealtimeRankOneCollisionSystem" })[0]
$message = @($test.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "Realtime rank-one system:*" })[0]
$match = [regex]::Match($message, "baseline_penetration_cm=(?<baseline>[0-9.]+) rank_one_penetration_cm=(?<rank>[0-9.]+) systems=(?<systems>\d+) active_pairs=(?<pairs>\d+) max_activation=(?<activation>\d+) render_submission_ms=(?<submission>[0-9.]+)")
$baseline = if ($match.Success) { [double]$match.Groups["baseline"].Value } else { [double]::NaN }
$rankOne = if ($match.Success) { [double]$match.Groups["rank"].Value } else { [double]::NaN }
$checks = [ordered]@{
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    metrics_present = $match.Success
    persistent_system_executed = ($match.Success -and [int64]$match.Groups["systems"].Value -eq 1)
    penetration_reduced = ($match.Success -and $rankOne -lt $baseline - 1.e-4)
    activation_readback_present = ($match.Success -and [int]$match.Groups["pairs"].Value -gt 0 -and [int]$match.Groups["activation"].Value -eq 1)
    render_submission_timing_present = ($match.Success -and [double]$match.Groups["submission"].Value -gt 0.0)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        baseline_penetration_cm = if ($match.Success) { $baseline } else { $null }
        rank_one_penetration_cm = if ($match.Success) { $rankOne } else { $null }
        penetration_reduction_cm = if ($match.Success) { $baseline - $rankOne } else { $null }
        systems_executed = if ($match.Success) { [int64]$match.Groups["systems"].Value } else { $null }
        active_contact_pairs = if ($match.Success) { [int]$match.Groups["pairs"].Value } else { $null }
        maximum_ndb_activation_count = if ($match.Success) { [int]$match.Groups["activation"].Value } else { $null }
        render_submission_ms = if ($match.Success) { [double]$match.Groups["submission"].Value } else { $null }
        timing_scope = "CPU render-thread RDG construction and submission; not GPU kernel time"
        automation_report = $automationPath
    }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "实时 rank-one 验证失败：$reportPath" }
Write-Output "M2_REALTIME_RANK_ONE_VALIDATION_PASSED report=$reportPath"
