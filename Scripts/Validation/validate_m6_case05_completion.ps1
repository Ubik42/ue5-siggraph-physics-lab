param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')))

$ErrorActionPreference = 'Stop'
$root = Join-Path $ProjectRoot 'Docs\Validation\M6'
$reportNames = @(
    'planar_recovery_foundation_validation.json', 'vertical_landing_validation.json', 'articulated_recovery_validation.json',
    'physics_control_realtime_validation.json', 'physical_getup_validation.json', 'physical_getup_interaction_validation.json',
    'recoverable_fall_authority_validation.json', 'recoverable_fall_network_validation.json',
    'network_profile_capture_validation.json', 'network_profile_attribution_validation.json',
    'chaos_drop_mapping_validation.json', 'chaos_joint_coupling_validation.json',
    'physics_control_performance_validation.json', 'portfolio_scene_validation.json')
$reports = [ordered]@{}
foreach ($name in $reportNames) {
    $reports[$name] = Get-Content -LiteralPath (Join-Path $root $name) -Raw -Encoding UTF8 | ConvertFrom-Json
}
$automation = Get-Content -LiteralPath (Join-Path $root 'AutomationPortfolioRegression\index.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$scope = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Docs\Research\CASE05_物理驱动角色复现范围.md') -Raw -Encoding UTF8
$golden = Get-Content -LiteralPath (Join-Path $root 'CASE05_黄金演示.md') -Raw -Encoding UTF8
$screenshots = @(Get-ChildItem -LiteralPath (Join-Path $root 'Screenshots') -Filter 'Case05_*.png')
$failedReports = @($reports.GetEnumerator() | Where-Object { $_.Value.result -ne 'passed' } | ForEach-Object Key)

$checks = [ordered]@{
    all_fourteen_subsystem_reports_pass = [bool]($reports.Count -eq 14 -and $failedReports.Count -eq 0)
    three_programmatic_benchmarks_pass = [bool]($reports['planar_recovery_foundation_validation.json'].result -eq 'passed' -and $reports['vertical_landing_validation.json'].result -eq 'passed' -and $reports['articulated_recovery_validation.json'].result -eq 'passed')
    real_chaos_impulse_height_and_coupling_pass = [bool]($reports['physics_control_realtime_validation.json'].result -eq 'passed' -and $reports['chaos_drop_mapping_validation.json'].result -eq 'passed' -and $reports['chaos_joint_coupling_validation.json'].result -eq 'passed')
    als_interaction_and_authority_pass = [bool]($reports['physical_getup_validation.json'].result -eq 'passed' -and $reports['physical_getup_interaction_validation.json'].result -eq 'passed' -and $reports['recoverable_fall_authority_validation.json'].result -eq 'passed')
    real_network_and_attribution_pass = [bool]($reports['recoverable_fall_network_validation.json'].result -eq 'passed' -and $reports['network_profile_capture_validation.json'].result -eq 'passed' -and $reports['network_profile_attribution_validation.json'].result -eq 'passed')
    performance_and_final_render_pass = [bool]($reports['physics_control_performance_validation.json'].result -eq 'passed' -and $reports['portfolio_scene_validation.json'].result -eq 'passed')
    final_automation_is_16_of_16_clean = [bool]($automation.succeeded -eq 16 -and $automation.failed -eq 0 -and $automation.succeededWithWarnings -eq 0 -and @($automation.tests).Count -eq 16)
    both_independent_maps_exist = [bool]((Test-Path -LiteralPath (Join-Path $ProjectRoot 'Plugins\GameFeatures\PhysicsDrivenCharacter\Content\Maps\L_PhysicsControlCharacter.umap')) -and (Test-Path -LiteralPath (Join-Path $ProjectRoot 'Plugins\GameFeatures\PhysicsDrivenCharacter\Content\Maps\L_PhysicalGetUpHandoff.umap')))
    portfolio_has_four_nontrivial_screenshots = [bool]($screenshots.Count -ge 4 -and @($screenshots | Where-Object Length -lt 300000).Count -eq 0)
    golden_path_and_claim_boundaries_exist = [bool]($golden -match '总时长：约 55 秒' -and $scope -match '未训练 NCP' -and $scope -match 'Profiler 可归因 token')
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$result = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = ($failed.Count -eq 0 ? 'passed' : 'failed')
    verdict = ($failed.Count -eq 0 ? 'M6 / Case05 may be frozen; remaining project work moves to M7 unified delivery.' : 'Case05 remains in progress.')
    completed_scope = 'Three standard benchmarks, real Chaos/PhysicsControl A/B, height and joint mapping, ALS handoff, player interaction, Lyra authority, weak-network profiling, performance, rendered portfolio scene and 55-second golden path.'
    claim_boundary = 'Case05 is a deterministic, paper-inspired UE engineering reproduction. It does not claim neural-policy training, a replacement Chaos solver, or ownership of Epic/ALS assets and runtime systems.'
    subsystem_reports = @($reportNames)
    screenshots = @($screenshots | ForEach-Object { [IO.Path]::GetRelativePath($ProjectRoot, $_.FullName).Replace('\','/') })
    checks = $checks
    failed_reports = $failedReports
    failed_checks = $failed
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root 'case05_completion_validation.json') -Encoding UTF8
$result | ConvertTo-Json -Depth 8
if ($failed.Count -gt 0) { exit 1 }
