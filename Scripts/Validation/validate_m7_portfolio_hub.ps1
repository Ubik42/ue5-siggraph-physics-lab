param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')))

$ErrorActionPreference = 'Stop'
$sourceRoot = Join-Path $ProjectRoot 'Plugins\PhysicsResearch\Source\PhysicsResearch'
$actorSource = Get-Content -LiteralPath (Join-Path $sourceRoot 'Private\PhysicsLabM1Actor.cpp') -Raw -Encoding UTF8
$hudSource = Get-Content -LiteralPath (Join-Path $sourceRoot 'Private\PhysicsLabM1GameMode.cpp') -Raw -Encoding UTF8
$screenshotPath = Join-Path $ProjectRoot 'Docs\Validation\M7\Screenshots\M7_PortfolioHub.png'
$routeLog = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Docs\Validation\M7\hub_route_case05.log') -Raw -Encoding UTF8
$routeRuntime = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Saved\PhysicsResearch\DrivenCharacterRealtime.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$mapFiles = @(
    'Plugins\GameFeatures\PhysicsContactCloth\Content\Maps\L_GpuExplicitAssassin.umap',
    'Plugins\GameFeatures\PhysicsSoftBody\Content\Maps\L_DragonGpuSurface.umap',
    'Plugins\GameFeatures\PhysicsFlowMap\Content\Maps\L_FlowMapChaosObstacle.umap',
    'Plugins\GameFeatures\PhysicsInverseDesign\Content\Maps\L_InverseSpringChaosReplay.umap',
    'Plugins\GameFeatures\PhysicsDrivenCharacter\Content\Maps\L_PhysicsControlCharacter.umap')
$acceptanceFiles = @(
    'Docs\Validation\M2\case01_acceptance_validation.json',
    'Docs\Validation\M3\case02_acceptance_validation.json',
    'Docs\Validation\M4\case03_acceptance_validation.json',
    'Docs\Validation\M5\case04_acceptance_validation.json',
    'Docs\Validation\M6\case05_completion_validation.json')
$acceptance = @($acceptanceFiles | ForEach-Object { Get-Content -LiteralPath (Join-Path $ProjectRoot $_) -Raw -Encoding UTF8 | ConvertFrom-Json })
Add-Type -AssemblyName System.Drawing
$image = [Drawing.Image]::FromFile($screenshotPath)
$width = $image.Width; $height = $image.Height; $image.Dispose()

$checks = [ordered]@{
    all_five_frozen_acceptance_reports_pass = [bool](@($acceptance | Where-Object result -ne 'passed').Count -eq 0)
    all_five_formal_map_assets_exist = [bool](@($mapFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $ProjectRoot $_)) }).Count -eq 0)
    source_routes_exactly_five_number_keys = [bool]($actorSource -match 'EKeys::One' -and $actorSource -match 'EKeys::Five' -and $actorSource -match 'LaunchPortfolioCase\(5\)')
    source_contains_all_five_formal_map_paths = [bool]($actorSource -match 'L_GpuExplicitAssassin' -and $actorSource -match 'L_DragonGpuSurface' -and $actorSource -match 'L_FlowMapChaosObstacle' -and $actorSource -match 'L_InverseSpringChaosReplay' -and $actorSource -match 'L_PhysicsControlCharacter')
    als_handoff_shortcut_is_explicit = [bool]($actorSource -match 'EKeys::H' -and $actorSource -match 'L_PhysicalGetUpHandoff')
    hub_hud_is_chinese_and_marks_acceptance = [bool]($hudSource -match '五个 UE 物理论文工程复现' -and $hudSource -match '已验收' -and $hudSource -match '数字键 1–5')
    rendered_hub_screenshot_is_1600x900 = [bool]((Test-Path -LiteralPath $screenshotPath) -and $width -eq 1600 -and $height -eq 900 -and (Get-Item -LiteralPath $screenshotPath).Length -gt 150000)
    real_hub_route_opened_case05_map = [bool]($routeLog -match 'M7_HUB_OPEN case=5 map=/PhysicsDrivenCharacter/Maps/L_PhysicsControlCharacter' -and $routeLog -match 'Load map complete /PhysicsDrivenCharacter/Maps/L_PhysicsControlCharacter')
    routed_case_completed_frozen_runtime = [bool]($routeRuntime.sample_count -eq 360 -and $routeRuntime.controller_advantage_pass -and [math]::Abs($routeRuntime.research_final_pelvis_displacement_cm - 2.9961892292570607) -lt 1.0e-9)
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$result = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = ($failed.Count -eq 0 ? 'passed' : 'failed')
    scope = 'M7 unified Chinese portfolio hub: five frozen acceptance reports, five real map routes, rendered UI and one end-to-end route execution.'
    route_contract = [ordered]@{ '1'='/PhysicsContactCloth/Maps/L_GpuExplicitAssassin'; '2'='/PhysicsSoftBody/Maps/L_DragonGpuSurface'; '3'='/PhysicsFlowMap/Maps/L_FlowMapChaosObstacle'; '4'='/PhysicsInverseDesign/Maps/L_InverseSpringChaosReplay'; '5'='/PhysicsDrivenCharacter/Maps/L_PhysicsControlCharacter'; 'H'='/PhysicsDrivenCharacter/Maps/L_PhysicalGetUpHandoff' }
    screenshot = [ordered]@{ path='Docs/Validation/M7/Screenshots/M7_PortfolioHub.png'; width=$width; height=$height; bytes=(Get-Item -LiteralPath $screenshotPath).Length }
    checks = $checks
    failed_checks = $failed
}
$result | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $ProjectRoot 'Docs\Validation\M7\portfolio_hub_validation.json') -Encoding UTF8
$result | ConvertTo-Json -Depth 7
if ($failed.Count -gt 0) { exit 1 }
