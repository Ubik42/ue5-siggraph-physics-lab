param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$automationPath = Join-Path $ProjectRoot "Docs\Validation\M2\RankOneHessianAutomation\index.json"
$reportPath = Join-Path $ProjectRoot "Docs\Validation\M2\rank_one_hessian_validation.json"
if (-not (Test-Path -LiteralPath $automationPath -PathType Leaf)) { throw "缺少 rank-one Hessian 报告：$automationPath" }
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$test = @($automation.tests | Where-Object { $_.fullTestPath -eq "PhysicsResearch.ContactCloth.RankOneCollisionHessian" })[0]
$message = @($test.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "Rank-one collision Hessian:*" })[0]
$match = [regex]::Match($message, "particles=(?<particles>\d+) modes=(?<modes>\d+) vector_dofs=(?<dofs>\d+) cache_values=(?<cache>\d+) xy_coupling=(?<coupling>[0-9.]+) symmetry_error=(?<symmetry>[0-9.eE+-]+) solve_error=(?<solve>[0-9.eE+-]+) gpu_cpu_error=(?<gpu>[0-9.eE+-]+)")
$checks = [ordered]@{
    test_succeeded = ($null -ne $test -and $test.state -eq "Success")
    metrics_present = $match.Success
    expected_dimensions = ($match.Success -and [int]$match.Groups["particles"].Value -eq 20 -and [int]$match.Groups["modes"].Value -eq 4 -and [int]$match.Groups["dofs"].Value -eq 12)
    complete_particle_outer_product_cache = ($match.Success -and [int]$match.Groups["cache"].Value -eq 320)
    cross_axis_coupling_present = ($match.Success -and [double]$match.Groups["coupling"].Value -gt 1.e-8)
    symmetric_system = ($match.Success -and [double]$match.Groups["symmetry"].Value -lt 1.e-12)
    known_solution_recovered = ($match.Success -and [double]$match.Groups["solve"].Value -lt 1.e-10)
    gpu_matches_cpu_truth = ($match.Success -and [double]$match.Groups["gpu"].Value -lt 1.e-5)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        xy_coupling = if ($match.Success) { [double]$match.Groups["coupling"].Value } else { $null }
        solve_error = if ($match.Success) { [double]$match.Groups["solve"].Value } else { $null }
        gpu_cpu_error = if ($match.Success) { [double]$match.Groups["gpu"].Value } else { $null }
        scope = "GPU matrix assembly matches CPU truth; realtime contact extraction and vector solve pending"
        automation_report = $automationPath
    }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "rank-one Hessian 验证失败：$reportPath" }
Write-Output "M2_RANK_ONE_HESSIAN_VALIDATION_PASSED report=$reportPath"
