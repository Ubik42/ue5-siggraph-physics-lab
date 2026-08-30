param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\ChaosTopologyAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\chaos_cloth_topology_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少 Chaos Cloth 拓扑报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.ChaosClothTopologyExtraction" })[0]
$message = @($test.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "Chaos cloth topology:*" })[0]
$match = [regex]::Match($message, "assets=(?<assets>\d+) vertices=(?<vertices>\d+) triangles=(?<triangles>\d+) unique_edges=(?<edges>\d+) fixed_vertices=(?<fixed>\d+) dynamic_vertices=(?<dynamic>\d+) max_distance_movable=(?<movable>\d+)")
$checks = [ordered]@{
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    one_success_zero_failures = ([int]$automation.succeeded -eq 1 -and [int]$automation.failed -eq 0)
    metrics_present = $match.Success
    expected_asset_count = ($match.Success -and [int]$match.Groups["assets"].Value -eq 2)
    nonempty_topology = ($match.Success -and [int]$match.Groups["vertices"].Value -gt 0 -and [int]$match.Groups["triangles"].Value -gt 0 -and [int]$match.Groups["edges"].Value -gt 0)
    fixed_and_dynamic_partition = ($match.Success -and [int]$match.Groups["fixed"].Value -gt 0 -and [int]$match.Groups["dynamic"].Value -gt 0)
    max_distance_matches_dynamic_vertices = ($match.Success -and [int]$match.Groups["movable"].Value -eq [int]$match.Groups["dynamic"].Value)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        asset = "/Game/Assassin/Mesh/SKM_Assassin_Skin1"
        clothing_assets = if ($match.Success) { [int]$match.Groups["assets"].Value } else { $null }
        vertices = if ($match.Success) { [int]$match.Groups["vertices"].Value } else { $null }
        triangles = if ($match.Success) { [int]$match.Groups["triangles"].Value } else { $null }
        unique_edges = if ($match.Success) { [int]$match.Groups["edges"].Value } else { $null }
        fixed_vertices = if ($match.Success) { [int]$match.Groups["fixed"].Value } else { $null }
        dynamic_vertices = if ($match.Success) { [int]$match.Groups["dynamic"].Value } else { $null }
        max_distance_movable = if ($match.Success) { [int]$match.Groups["movable"].Value } else { $null }
        scope = "Exact Chaos physical topology extraction gate, including validated Clothing bone influence contract; animated pose and same-step Chaos/research comparison remain pending"
        automation_report = $automationPath
    }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "Chaos Cloth 拓扑验证失败：$reportPath" }
Write-Output "M2_CHAOS_CLOTH_TOPOLOGY_VALIDATION_PASSED report=$reportPath"
