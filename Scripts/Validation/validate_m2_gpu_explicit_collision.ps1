param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuExplicitCollisionAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_explicit_collision_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少显式拓扑碰撞报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.GpuExplicitChaosTopology.PhysicsAssetCollision" })[0]
$message = @($test.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "GPU explicit Physics Asset collision:*" })[0]
$pattern = "particles=(?<particles>\d+) probe=(?<probe>\d+) spheres=(?<spheres>\d+) capsules=(?<capsules>\d+) steps=(?<steps>\d+) collision_updates=(?<updates>\d+) collision_applications=(?<applications>\d+) collision_version=(?<version>\d+) input_penetration_cm=(?<input>[0-9.]+) output_penetration_cm=(?<output>[0-9.]+)"
$match = [regex]::Match($message, $pattern)
$checks = [ordered]@{
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    metrics_present = $match.Success
    exact_assassin_particle_count = ($match.Success -and [int]$match.Groups["particles"].Value -eq 2011)
    deliberate_penetration_present = ($match.Success -and [double]$match.Groups["input"].Value -ge 9.0)
    collision_snapshot_applied = ($match.Success -and [int]$match.Groups["updates"].Value -eq 1 -and [int]$match.Groups["applications"].Value -eq 1 -and [int]$match.Groups["version"].Value -eq 1)
    penetration_removed = ($match.Success -and [double]$match.Groups["output"].Value -lt 0.001)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        particles = if ($match.Success) { [int]$match.Groups["particles"].Value } else { $null }
        probe_particle = if ($match.Success) { [int]$match.Groups["probe"].Value } else { $null }
        collision_spheres = if ($match.Success) { [int]$match.Groups["spheres"].Value } else { $null }
        collision_capsules = if ($match.Success) { [int]$match.Groups["capsules"].Value } else { $null }
        input_penetration_cm = if ($match.Success) { [double]$match.Groups["input"].Value } else { $null }
        output_penetration_cm = if ($match.Success) { [double]$match.Groups["output"].Value } else { $null }
        scope = "GPU analytic sphere projection gate on exact Assassin explicit topology; animated full 1-sphere/16-capsule runtime is validated separately"
        automation_report = $automationPath
    }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "显式拓扑碰撞验证失败：$reportPath" }
Write-Output "M2_GPU_EXPLICIT_COLLISION_VALIDATION_PASSED report=$reportPath"
