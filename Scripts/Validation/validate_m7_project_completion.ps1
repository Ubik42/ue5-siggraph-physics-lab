param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')))

$ErrorActionPreference = 'Stop'

function Read-Json([string]$RelativePath) {
    $path = Join-Path $ProjectRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
}

$foundation = Read-Json 'Docs\Validation\M1\runtime_validation.json'
$caseFiles = @(
    'Docs\Validation\M2\case01_acceptance_validation.json',
    'Docs\Validation\M3\case02_acceptance_validation.json',
    'Docs\Validation\M4\case03_acceptance_validation.json',
    'Docs\Validation\M5\case04_acceptance_validation.json',
    'Docs\Validation\M6\case05_completion_validation.json'
)
$cases = @($caseFiles | ForEach-Object { Read-Json $_ })
$hub = Read-Json 'Docs\Validation\M7\portfolio_hub_validation.json'
$package = Read-Json 'Docs\Validation\M7\packaged_build_validation.json'

$engineConfig = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Config\DefaultEngine.ini') -Raw -Encoding UTF8
$gameConfig = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Config\DefaultGame.ini') -Raw -Encoding UTF8
$gitIgnore = Get-Content -LiteralPath (Join-Path $ProjectRoot '.gitignore') -Raw -Encoding UTF8
$rootReadme = Get-Content -LiteralPath (Join-Path $ProjectRoot 'README.md') -Raw -Encoding UTF8
$goldenDoc = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Docs\Validation\M7\五作品黄金与失败路径.md') -Raw -Encoding UTF8
$sourceControlDoc = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Docs\Development\SOURCE_CONTROL.md') -Raw -Encoding UTF8

$maps = @(
    'Content\PhysicsResearch\Maps\L_PhysicsLab_M1.umap',
    'Plugins\GameFeatures\PhysicsContactCloth\Content\Maps\L_GpuExplicitAssassin.umap',
    'Plugins\GameFeatures\PhysicsSoftBody\Content\Maps\L_DragonGpuSurface.umap',
    'Plugins\GameFeatures\PhysicsFlowMap\Content\Maps\L_FlowMapChaosObstacle.umap',
    'Plugins\GameFeatures\PhysicsInverseDesign\Content\Maps\L_InverseSpringChaosReplay.umap',
    'Plugins\GameFeatures\PhysicsDrivenCharacter\Content\Maps\L_PhysicsControlCharacter.umap',
    'Plugins\GameFeatures\PhysicsDrivenCharacter\Content\Maps\L_PhysicalGetUpHandoff.umap'
)
$researchDocs = @(1..5 | ForEach-Object {
    Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'Docs\Research') -Filter ("CASE0{0}_*.md" -f $_) -File
})
$caseScreenshotRoots = @('M2','M3','M4','M5','M6')
$caseScreenshotCounts = [ordered]@{}
foreach ($milestone in $caseScreenshotRoots) {
    $folder = Join-Path $ProjectRoot ("Docs\Validation\{0}\Screenshots" -f $milestone)
    $caseScreenshotCounts[$milestone] = if (Test-Path -LiteralPath $folder) { @(Get-ChildItem -LiteralPath $folder -Filter '*.png' -File).Count } else { 0 }
}

$checks = [ordered]@{
    m1_shared_foundation_passes = [bool]($foundation -and $foundation.result -eq 'passed')
    all_five_case_acceptance_reports_pass = [bool]($cases.Count -eq 5 -and @($cases | Where-Object { -not $_ -or $_.result -ne 'passed' }).Count -eq 0)
    unified_chinese_hub_passes = [bool]($hub -and $hub.result -eq 'passed' -and @($hub.failed_checks).Count -eq 0)
    packaged_win64_build_passes = [bool]($package -and $package.result -eq 'passed' -and @($package.failed_checks).Count -eq 0)
    hub_five_cases_and_als_handoff_maps_exist = [bool](@($maps | Where-Object { -not (Test-Path -LiteralPath (Join-Path $ProjectRoot $_)) }).Count -eq 0)
    default_map_is_portfolio_hub = [bool]($engineConfig -match 'GameDefaultMap=/Game/PhysicsResearch/Maps/L_PhysicsLab_M1')
    all_release_maps_are_explicitly_cooked = [bool](@('L_PhysicsLab_M1','L_GpuExplicitAssassin','L_DragonGpuSurface','L_FlowMapChaosObstacle','L_InverseSpringChaosReplay','L_PhysicsControlCharacter','L_PhysicalGetUpHandoff' | Where-Object { $gameConfig -notmatch [regex]::Escape($_) }).Count -eq 0)
    each_case_has_reproduction_scope = [bool]($researchDocs.Count -eq 5)
    each_case_has_gui_screenshot_evidence = [bool](@($caseScreenshotCounts.Values | Where-Object { $_ -lt 1 }).Count -eq 0)
    five_case_golden_and_expected_failure_paths_are_documented = [bool](@(1..5 | Where-Object { $goldenDoc -notmatch ("\| 0{0} " -f $_) }).Count -eq 0 -and $goldenDoc -match '黄金路径' -and $goldenDoc -match '可解释失败')
    chinese_github_readme_has_five_case_portfolio = [bool]($rootReadme -match '五个' -and $rootReadme -match 'Case 01' -and $rootReadme -match 'Case 05' -and $rootReadme -match 'M7_PortfolioHub.png')
    epic_lyra_fab_overlay_boundary_is_documented = [bool]($sourceControlDoc -match 'Lyra' -and $sourceControlDoc -match 'Fab' -and $sourceControlDoc -match '不跟踪内容' -and $sourceControlDoc -match '源码仓不承担第三方原资产再分发')
    marketplace_and_lyra_assets_are_excluded = [bool]($gitIgnore -match 'Content/Marketplace/' -and $gitIgnore -match 'Content/Assassin/' -and $gitIgnore -match '/Source/' -and $gitIgnore -match '/Plugins/\*')
    packaged_outputs_are_not_committed = [bool]($gitIgnore -match '(?m)^Builds/$')
    android_file_server_is_disabled_and_has_no_current_token = [bool]($engineConfig -match 'bEnablePlugin=False' -and $engineConfig -match 'bAllowNetworkConnection=False' -and $engineConfig -notmatch '(?m)^SecurityToken=\S+')
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$result = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = ($failed.Count -eq 0 ? 'passed' : 'failed')
    scope = 'SIGGRAPH Physics Lab project-level release readiness: shared UE foundation, five independent paper-inspired cases, Chaos integration boundaries, Chinese portfolio hub, evidence, Win64 packaging and public-source overlay hygiene.'
    acceptance_reports = $caseFiles
    case_screenshot_counts = $caseScreenshotCounts
    checks = $checks
    failed_checks = $failed
    publication_boundary = 'This validates the local UE deliverable and public-source overlay. Creating a GitHub repository, selecting a license, rewriting old Git history and pushing remain explicit user-authorized publication actions.'
}

$reportPath = Join-Path $ProjectRoot 'Docs\Validation\M7\project_completion_validation.json'
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$result | ConvertTo-Json -Depth 8
if ($failed.Count -gt 0) { exit 1 }
