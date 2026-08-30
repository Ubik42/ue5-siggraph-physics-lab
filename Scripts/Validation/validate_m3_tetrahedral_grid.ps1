param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M3\Automation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M3\tetrahedral_grid_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少四面体网格自动化报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.SoftBody.TetrahedralGrid.Contract")
$messages = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "Soft-body tet tier=*" })
$tiers = [ordered]@{}
foreach ($message in $messages) {
    $match = [regex]::Match($message, "tier=(?<tier>\S+) cells=(?<x>\d+)x(?<y>\d+)x(?<z>\d+) vertices=(?<vertices>\d+) tets=(?<tets>\d+) surface_triangles=(?<surface>\d+) fixed=(?<fixed>\d+) total_volume_cm3=(?<volume>[0-9.]+) tet_volume_cm3=(?<tetvolume>[0-9.]+) materials=(?<materials>\d+)")
    if ($match.Success) {
        $tiers[$match.Groups["tier"].Value] = [ordered]@{
            cells = @([int]$match.Groups["x"].Value, [int]$match.Groups["y"].Value, [int]$match.Groups["z"].Value)
            vertices = [int]$match.Groups["vertices"].Value
            tetrahedra = [int]$match.Groups["tets"].Value
            surface_triangles = [int]$match.Groups["surface"].Value
            fixed_vertices = [int]$match.Groups["fixed"].Value
            total_volume_cm3 = [double]$match.Groups["volume"].Value
            tetrahedron_volume_cm3 = [double]$match.Groups["tetvolume"].Value
            materials = [int]$match.Groups["materials"].Value
        }
    }
}
function TierMatches([string]$Name, [int[]]$Cells, [int]$Vertices, [int]$Tets, [int]$Surface, [int]$Fixed, [double]$Volume) {
    $tier = $tiers[$Name]
    return $null -ne $tier -and ($tier.cells -join ',') -eq ($Cells -join ',') -and $tier.vertices -eq $Vertices `
        -and $tier.tetrahedra -eq $Tets -and $tier.surface_triangles -eq $Surface -and $tier.fixed_vertices -eq $Fixed `
        -and $tier.materials -eq 2 -and [Math]::Abs($tier.total_volume_cm3 - $Volume) -le ($Volume * 1e-6)
}
$checks = [ordered]@{
    target_test_passed = ($test.Count -eq 1 -and $test[0].state -eq "Success")
    full_m3_suite_zero_failures = ([int]$automation.succeeded -eq 9 -and [int]$automation.failed -eq 0)
    zero_test_warnings_and_errors = ([int]$test[0].warnings -eq 0 -and [int]$test[0].errors -eq 0)
    three_resolution_tiers_reported = ($tiers.Count -eq 3)
    low_tier_exact_contract = (TierMatches "低" @(4,2,2) 45 96 80 9 16000.0)
    medium_tier_exact_contract = (TierMatches "中" @(8,4,4) 225 768 320 25 128000.0)
    high_tier_exact_contract = (TierMatches "高" @(16,8,8) 1377 6144 1280 81 1024000.0)
    positive_uniform_tetrahedron_volume = (@($tiers.Values | Where-Object { $_.tetrahedron_volume_cm3 -gt 0.0 -and [Math]::Abs($_.tetrahedron_volume_cm3 - 166.666672) -lt 0.001 }).Count -eq 3)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    checks = $checks
    tiers = $tiers
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M3 四面体网格验证失败：$($failed -join ', ')" }
Write-Output "M3_TETRAHEDRAL_GRID_VALIDATION_PASSED report=$reportPath"
