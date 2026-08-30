param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M3\LocalXpbdMaterialAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M3\compression_materials_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少材料压缩自动化报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.SoftBody.LocalXpbd.CompressionMaterials")
$message = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "Local XPBD compression materials:*" })[0]
$number = "[0-9.eE+-]+"
$pattern = "compression_cm=(?<compression>$number) kinematic_vertices=(?<vertices>\d+) soft_volume_mean=(?<softmean>$number) soft_volume_ratio=(?<softratio>$number) soft_residual=(?<softres>$number) stiff_volume_mean=(?<stiffmean>$number) stiff_volume_ratio=(?<stiffratio>$number) stiff_residual=(?<stiffres>$number) split_soft_volume_mean=(?<splitsoft>$number) split_stiff_volume_mean=(?<splitstiff>$number) split_volume_ratio=(?<splitratio>$number) split_residual=(?<splitres>$number) soft_target_error_cm=(?<softerror>$number) stiff_target_error_cm=(?<stifferror>$number) split_target_error_cm=(?<spliterror>$number)"
$match = [regex]::Match($message, $pattern)
function Metric([string]$Name) { if ($match.Success) { [double]$match.Groups[$Name].Value } else { [double]::NaN } }
$checks = [ordered]@{
    target_test_passed = ($test.Count -eq 1 -and $test[0].state -eq "Success")
    two_local_tests_zero_failures = ([int]$automation.succeeded -eq 2 -and [int]$automation.failed -eq 0)
    zero_test_warnings_and_errors = ([int]$test[0].warnings -eq 0 -and [int]$test[0].errors -eq 0)
    summary_metrics_present = $match.Success
    exact_displacement_boundary = ($match.Success -and [Math]::Abs((Metric "compression") - 8.0) -lt 1e-9 -and [int]$match.Groups["vertices"].Value -eq 9)
    kinematic_targets_exact = ($match.Success -and (Metric "softerror") -lt 1e-9 -and (Metric "stifferror") -lt 1e-9 -and (Metric "spliterror") -lt 1e-9)
    stiff_material_preserves_volume_better = ($match.Success -and (Metric "stiffmean") -lt (Metric "softmean") * 0.9)
    split_response_between_uniform_profiles = ($match.Success -and (Metric "splitratio") -gt (Metric "softratio") -and (Metric "splitratio") -lt (Metric "stiffratio"))
    stiff_residual_lower_than_soft = ($match.Success -and (Metric "stiffres") -lt (Metric "softres"))
    all_total_volumes_positive = ($match.Success -and (Metric "softratio") -gt 0.5 -and (Metric "stiffratio") -gt 0.5 -and (Metric "splitratio") -gt 0.5)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    checks = $checks
    metrics = if ($match.Success) { [ordered]@{
        compression_cm = Metric "compression"; kinematic_vertices = [int]$match.Groups["vertices"].Value
        soft_mean_volume_relative_error = Metric "softmean"; soft_total_volume_ratio = Metric "softratio"; soft_residual_rms = Metric "softres"
        stiff_mean_volume_relative_error = Metric "stiffmean"; stiff_total_volume_ratio = Metric "stiffratio"; stiff_residual_rms = Metric "stiffres"
        split_soft_mean_volume_relative_error = Metric "splitsoft"; split_stiff_mean_volume_relative_error = Metric "splitstiff"
        split_total_volume_ratio = Metric "splitratio"; split_residual_rms = Metric "splitres"
        maximum_kinematic_target_error_cm = [Math]::Max((Metric "softerror"), [Math]::Max((Metric "stifferror"), (Metric "spliterror")))
    } } else { $null }
    material_profile = [ordered]@{
        soft_edge_compliance = 0.002; stiff_edge_compliance = 0.00002
        soft_volume_compliance = 20.0; stiff_volume_compliance = 0.2
        note = "displacement-controlled contrast profile in UE centimeter/kilogram units; not the gravity cantilever default"
    }
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M3 材料压缩验证失败：$($failed -join ', ')" }
Write-Output "M3_COMPRESSION_MATERIALS_VALIDATION_PASSED report=$reportPath"
