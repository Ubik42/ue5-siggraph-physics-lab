param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\.."))
)

$ErrorActionPreference = "Stop"
$ccdPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuCompleteCcdIntegrationAutomation\index.json"
$ndbPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuNonDistanceBarrierIntegrationAutomation\index.json"
$ablationPath = Join-Path $ProjectRoot "Docs\Validation\M2\GpuHybridAblationAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\gpu_hybrid_collision_validation.json"
foreach ($path in @($ccdPath, $ndbPath, $ablationPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "缺少 GPU 混合碰撞自动化报告：$path"
    }
}

$ccd = Get-Content -LiteralPath $ccdPath -Raw -Encoding UTF8 | ConvertFrom-Json
$ndb = Get-Content -LiteralPath $ndbPath -Raw -Encoding UTF8 | ConvertFrom-Json
$ablation = Get-Content -LiteralPath $ablationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$ccdTest = @($ccd.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.GpuCompleteCcdIntegration" })[0]
$ndbTest = @($ndb.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.GpuNonDistanceBarrierIntegration" })[0]
$ablationTest = @($ablation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.GpuHybridAblationMatrix" })[0]
$ccdMessage = @($ccdTest.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "Complete CCD crossing:*" })[0]
$ndbMessage = @($ndbTest.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "NDB activation schedule:*" })[0]
$ccdMatch = [regex]::Match($ccdMessage, "discrete_z=(?<discrete>-?[0-9.]+) ccd_z=(?<ccd>-?[0-9.]+) ccd_passes=(?<passes>\d+)")
$ndbMatch = [regex]::Match($ndbMessage, "first_penetration_cm=(?<first>[0-9.]+) sustained_penetration_cm=(?<sustained>[0-9.]+) state_updates=(?<updates>\d+)")
$ablationMessage = @($ablationTest.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "Hybrid ablation matrix:*" })[0]
$ablationMatch = [regex]::Match($ablationMessage, "constant_no_ccd_pen=(?<constant_pen>[0-9.]+) ndb_no_ccd_pen=(?<ndb_pen>[0-9.]+) constant_ccd_pen=(?<constant_ccd_pen>[0-9.]+) full_pen=(?<full_pen>[0-9.]+) constant_no_ccd_z=(?<constant_z>-?[0-9.]+) ndb_no_ccd_z=(?<ndb_z>-?[0-9.]+) constant_ccd_z=(?<ccd_z>-?[0-9.]+) full_z=(?<full_z>-?[0-9.]+)")
$checks = [ordered]@{
    ccd_test_succeeded = ($null -ne $ccdTest -and $ccdTest.state -eq "Success")
    ndb_test_succeeded = ($null -ne $ndbTest -and $ndbTest.state -eq "Success")
    ablation_test_succeeded = ($null -ne $ablationTest -and $ablationTest.state -eq "Success")
    d3d12_sm6 = (@($ccd.devices + $ndb.devices + $ablation.devices | Where-Object { $_.renderMode -eq "D3D12_SM6" }).Count -eq 3)
    metrics_present = ($ccdMatch.Success -and $ndbMatch.Success -and $ablationMatch.Success)
    discrete_baseline_tunnels = ($ccdMatch.Success -and [double]$ccdMatch.Groups["discrete"].Value -lt -10.0)
    ccd_stops_at_entry = ($ccdMatch.Success -and [double]$ccdMatch.Groups["ccd"].Value -ge 9.99 -and [int]$ccdMatch.Groups["passes"].Value -eq 1)
    ndb_is_finite_penalty = ($ndbMatch.Success -and [double]$ndbMatch.Groups["first"].Value -gt 0.01)
    ndb_sustained_contact_stiffens = ($ndbMatch.Success -and [double]$ndbMatch.Groups["sustained"].Value -lt [double]$ndbMatch.Groups["first"].Value -and [int]$ndbMatch.Groups["updates"].Value -eq 4)
    ablation_ndb_has_independent_benefit = ($ablationMatch.Success -and [double]$ablationMatch.Groups["ndb_pen"].Value -lt [double]$ablationMatch.Groups["constant_pen"].Value * 0.6)
    ablation_ccd_has_independent_benefit = ($ablationMatch.Success -and [double]$ablationMatch.Groups["constant_z"].Value -lt -10.0 -and [double]$ablationMatch.Groups["ccd_z"].Value -ge 9.99)
    ccd_not_misreported_as_depenetration = ($ablationMatch.Success -and [math]::Abs([double]$ablationMatch.Groups["constant_ccd_pen"].Value - [double]$ablationMatch.Groups["constant_pen"].Value) -lt 0.001)
    full_method_preserves_both_effects = ($ablationMatch.Success -and [math]::Abs([double]$ablationMatch.Groups["full_pen"].Value - [double]$ablationMatch.Groups["ndb_pen"].Value) -lt 0.001 -and [double]$ablationMatch.Groups["full_z"].Value -ge 9.99)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        discrete_crossing_z_cm = if ($ccdMatch.Success) { [double]$ccdMatch.Groups["discrete"].Value } else { $null }
        ccd_clamped_z_cm = if ($ccdMatch.Success) { [double]$ccdMatch.Groups["ccd"].Value } else { $null }
        first_contact_penetration_cm = if ($ndbMatch.Success) { [double]$ndbMatch.Groups["first"].Value } else { $null }
        sustained_contact_penetration_cm = if ($ndbMatch.Success) { [double]$ndbMatch.Groups["sustained"].Value } else { $null }
        ablation_constant_penetration_cm = if ($ablationMatch.Success) { [double]$ablationMatch.Groups["constant_pen"].Value } else { $null }
        ablation_ndb_penetration_cm = if ($ablationMatch.Success) { [double]$ablationMatch.Groups["ndb_pen"].Value } else { $null }
        ablation_constant_no_ccd_crossing_z_cm = if ($ablationMatch.Success) { [double]$ablationMatch.Groups["constant_z"].Value } else { $null }
        ablation_constant_ccd_crossing_z_cm = if ($ablationMatch.Success) { [double]$ablationMatch.Groups["ccd_z"].Value } else { $null }
        ccd_automation_report = $ccdPath
        ndb_automation_report = $ndbPath
        ablation_automation_report = $ablationPath
    }
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") {
    throw "GPU 混合碰撞验证失败，详见：$reportPath"
}
Write-Output "M2_GPU_HYBRID_COLLISION_VALIDATION_PASSED report=$reportPath"
