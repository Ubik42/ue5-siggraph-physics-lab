param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\AssassinGpuBridgeAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\assassin_gpu_bridge_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) {
    throw "缺少 Assassin GPU 桥自动化报告：$automationPath"
}

$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.AssassinGpuCollisionBridge" })[0]
$messages = if ($null -ne $test) { @($test.entries | ForEach-Object { $_.event.message }) } else { @() }
$metricMessage = @($messages | Where-Object { $_ -like "Assassin GPU collision bridge:*" })[0]
$metricMatch = [regex]::Match($metricMessage, "bodies=(?<bodies>\d+) spheres=(?<spheres>\d+) capsules=(?<capsules>\d+) probe_displacement_cm=(?<displacement>[0-9.]+)")

$checks = [ordered]@{
    test_discovered = ($null -ne $test)
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    d3d12_sm6 = (@($automation.devices | Where-Object { $_.renderMode -eq "D3D12_SM6" }).Count -ge 1)
    bridge_metrics_present = $metricMatch.Success
    expected_body_count = ($metricMatch.Success -and [int]$metricMatch.Groups["bodies"].Value -eq 18)
    expected_sphere_count = ($metricMatch.Success -and [int]$metricMatch.Groups["spheres"].Value -eq 1)
    expected_capsule_count = ($metricMatch.Success -and [int]$metricMatch.Groups["capsules"].Value -eq 16)
    real_asset_probe_projected = ($metricMatch.Success -and [double]$metricMatch.Groups["displacement"].Value -gt 1.0)
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
        probe_displacement_cm = if ($metricMatch.Success) { [double]$metricMatch.Groups["displacement"].Value } else { $null }
        automation_report = $automationPath
    }
}

$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") {
    throw "Assassin GPU 桥验证失败，详见：$reportPath"
}
Write-Output "M2_ASSASSIN_GPU_BRIDGE_VALIDATION_PASSED report=$reportPath"
