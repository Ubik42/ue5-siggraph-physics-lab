param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M3\GpuTopologyAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M3\gpu_topology_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少 GPU 拓扑自动化报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object fullTestPath -eq "PhysicsResearch.SoftBody.GpuTopology.Contract")
$messages = @($test[0].entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "Soft-body GPU topology tier=*" })
$tiers = [ordered]@{}
foreach ($message in $messages) {
    $match = [regex]::Match($message, "tier=(?<tier>\d+) vertices=(?<vertices>\d+) edges=(?<edges>\d+) edge_incidence=(?<edgeinc>\d+) tets=(?<tets>\d+) tet_incidence=(?<tetinc>\d+)")
    if ($match.Success) {
        $tiers[$match.Groups["tier"].Value] = [ordered]@{
            vertices = [int]$match.Groups["vertices"].Value
            edges = [int]$match.Groups["edges"].Value
            edge_incidence = [int]$match.Groups["edgeinc"].Value
            tetrahedra = [int]$match.Groups["tets"].Value
            tetrahedron_incidence = [int]$match.Groups["tetinc"].Value
        }
    }
}
function TierMatches([string]$Name, [int]$Vertices, [int]$Edges, [int]$Tets) {
    $tier = $tiers[$Name]
    return $null -ne $tier -and $tier.vertices -eq $Vertices -and $tier.edges -eq $Edges `
        -and $tier.edge_incidence -eq $Edges * 2 -and $tier.tetrahedra -eq $Tets `
        -and $tier.tetrahedron_incidence -eq $Tets * 4
}
$checks = [ordered]@{
    target_test_passed = ($test.Count -eq 1 -and $test[0].state -eq "Success")
    isolated_suite_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    zero_test_warnings_and_errors = ([int]$test[0].warnings -eq 0 -and [int]$test[0].errors -eq 0)
    three_tiers_reported = ($tiers.Count -eq 3)
    low_tier_exact_gpu_contract = (TierMatches "0" 45 180 96)
    medium_tier_exact_gpu_contract = (TierMatches "1" 225 1152 768)
    high_tier_exact_gpu_contract = (TierMatches "2" 1377 8160 6144)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($failed.Count -eq 0) { "passed" } else { "failed" }
    checks = $checks
    tiers = $tiers
    gpu_iteration_plan = @("distance constraint pass", "tetrahedron volume constraint pass", "vertex CSR gather/apply pass")
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($failed.Count -gt 0) { throw "M3 GPU 拓扑验证失败：$($failed -join ', ')" }
Write-Output "M3_GPU_TOPOLOGY_VALIDATION_PASSED report=$reportPath"
