param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\PhysicsAssetAdapterAutomation\index.json"
$logPath = Join-Path $ProjectRoot "Docs\Validation\M2\PhysicsAssetAdapterAutomation.log"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\physics_asset_adapter_validation.json"

foreach ($path in @($automationPath, $logPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "缺少 M2 Physics Asset 适配验证产物：$path"
    }
}

$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.AssassinPhysicsAssetAdapter" })[0]
$logText = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
$metricMatch = [regex]::Match($logText, "Assassin Physics Asset adapter: bodies=(?<bodies>\d+) spheres=(?<spheres>\d+) capsules=(?<capsules>\d+) skipped_missing_bones=(?<skipped>\d+)")

$checks = [ordered]@{
    test_discovered = ($null -ne $test)
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    adapter_metrics_present = $metricMatch.Success
    expected_body_count = ($metricMatch.Success -and [int]$metricMatch.Groups["bodies"].Value -eq 18)
    sphere_present = ($metricMatch.Success -and [int]$metricMatch.Groups["spheres"].Value -ge 1)
    capsule_set_present = ($metricMatch.Success -and [int]$metricMatch.Groups["capsules"].Value -ge 1)
    no_missing_bones = ($metricMatch.Success -and [int]$metricMatch.Groups["skipped"].Value -eq 0)
    no_fatal_error = ($logText -notmatch "Fatal error:")
    no_ensure = ($logText -notmatch "Ensure condition failed")
}

$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        skeletal_mesh = "/Game/Assassin/Mesh/SKM_Assassin_Skin1"
        physics_asset = "/Game/Assassin/Mesh/PHYS_Assassin_Skin1"
        bodies = if ($metricMatch.Success) { [int]$metricMatch.Groups["bodies"].Value } else { $null }
        spheres = if ($metricMatch.Success) { [int]$metricMatch.Groups["spheres"].Value } else { $null }
        capsules = if ($metricMatch.Success) { [int]$metricMatch.Groups["capsules"].Value } else { $null }
        skipped_missing_bones = if ($metricMatch.Success) { [int]$metricMatch.Groups["skipped"].Value } else { $null }
        automation_report = $automationPath
        engine_log = $logPath
    }
}

$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") {
    throw "M2 Physics Asset 适配验证失败，详见：$reportPath"
}
Write-Output "M2_PHYSICS_ASSET_ADAPTER_VALIDATION_PASSED report=$reportPath"
