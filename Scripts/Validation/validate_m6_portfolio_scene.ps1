param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')))

$ErrorActionPreference = 'Stop'
$root = Join-Path $ProjectRoot 'Docs\Validation\M6'
$runtime = Get-Content -LiteralPath (Join-Path $root 'DrivenCharacterPortfolio.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$samples = @(Import-Csv -LiteralPath (Join-Path $root 'DrivenCharacterPortfolio.csv'))
$capture = Get-Content -LiteralPath (Join-Path $root 'portfolio_scene_capture.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$performance = Get-Content -LiteralPath (Join-Path $root 'physics_control_performance_validation.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$coupling = Get-Content -LiteralPath (Join-Path $root 'chaos_joint_coupling_validation.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$drop = Get-Content -LiteralPath (Join-Path $root 'chaos_drop_mapping_validation.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$getUp = Get-Content -LiteralPath (Join-Path $root 'physical_getup_validation.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$actorSource = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Private\PhysicsControlCharacterRealtimeActor.cpp') -Raw -Encoding UTF8
$hudSource = Get-Content -LiteralPath (Join-Path $ProjectRoot 'Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Private\PhysicsControlCharacterHUD.cpp') -Raw -Encoding UTF8
$goldenPath = Get-Content -LiteralPath (Join-Path $root 'CASE05_黄金演示.md') -Raw -Encoding UTF8
$screenshotPath = Join-Path $ProjectRoot $capture.screenshot.Replace('/', '\')
Add-Type -AssemblyName System.Drawing
$image = [Drawing.Image]::FromFile($screenshotPath)
$width = $image.Width
$height = $image.Height
$image.Dispose()

$allFinite = $true
foreach ($sample in $samples) {
    foreach ($name in @('time_s','baseline_pose_error_deg','research_pose_error_deg','baseline_pelvis_displacement_cm','research_pelvis_displacement_cm')) {
        if (-not [double]::IsFinite([double]$sample.$name)) { $allFinite = $false }
    }
}

$checks = [ordered]@{
    exact_engine_and_portfolio_mode = [bool]($runtime.engine -eq '5.4.4-35576357+++UE5+Release-5.4' -and $runtime.portfolio_scene)
    same_physics_contract_is_preserved = [bool]($runtime.scenario -eq 'SharedSpineImpulse' -and $runtime.scene_rigid_body_count -eq 44 -and $runtime.research_control_count -eq 22 -and $runtime.shared_impulse_y_kg_cm_per_s -eq 5200)
    portfolio_stage_has_six_meshes_and_two_lights = [bool]($runtime.portfolio_mesh_count -eq 6 -and $runtime.portfolio_light_count -eq 2)
    runtime_has_360_finite_samples = [bool]($runtime.sample_count -eq 360 -and $samples.Count -eq 360 -and $allFinite)
    controller_result_matches_frozen_impulse_gate = [bool]([math]::Abs($runtime.baseline_final_pose_error_deg - 107.60364828567778) -lt 1.0e-9 -and [math]::Abs($runtime.research_final_pose_error_deg - 27.143672045157054) -lt 1.0e-9 -and [math]::Abs($runtime.research_final_pelvis_displacement_cm - 2.9961892292570607) -lt 1.0e-9)
    screenshot_is_1600x900_and_nontrivial = [bool]((Test-Path -LiteralPath $screenshotPath) -and $width -eq 1600 -and $height -eq 900 -and (Get-Item -LiteralPath $screenshotPath).Length -gt 500000)
    capture_owns_a_real_process = [bool]($capture.owned_pid -gt 0 -and $capture.screenshot_bytes -eq (Get-Item -LiteralPath $screenshotPath).Length)
    stage_is_visual_only = [bool]($actorSource -match 'PhysicsDrivenCharacterPortfolio' -and $actorSource -match 'ECollisionEnabled::NoCollision' -and $actorSource -match 'PortfolioBaselineLight' -and $actorSource -match 'PortfolioResearchLight')
    chinese_hud_contains_ab_and_evidence_boundary = [bool]($hudSource -match '被动 Chaos 基线' -and $hudSource -match 'Chaos \+ Physics Control' -and $hudSource -match '同资产 · 同冲量 · 固定 60 Hz')
    prior_height_coupling_performance_gates_pass = [bool]($drop.result -eq 'passed' -and $coupling.result -eq 'passed' -and $performance.result -eq 'passed')
    als_handoff_gate_and_screenshot_exist = [bool]($getUp.result -eq 'passed' -and (Test-Path -LiteralPath (Join-Path $root 'Screenshots\Case05_ALS_PhysicalGetUp.png')))
    golden_path_is_30_to_90_seconds = [bool]($goldenPath -match '总时长：约 55 秒' -and $goldenPath -match '0–12 秒' -and $goldenPath -match '42–55 秒')
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$result = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = ($failed.Count -eq 0 ? 'passed' : 'failed')
    scope = 'Case05 final rendered portfolio scene plus golden-path evidence audit.'
    claim_boundary = 'The stage is dynamically generated visual presentation with collision disabled. Manny, Physics Asset, impulse, fixed step and PhysicsControl path are identical to the frozen quantitative gate. ALS remains separately attributed in the handoff scene.'
    screenshot = [ordered]@{ path=$capture.screenshot; width=$width; height=$height; bytes=(Get-Item -LiteralPath $screenshotPath).Length }
    runtime = $runtime
    checks = $checks
    failed_checks = $failed
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $root 'portfolio_scene_validation.json') -Encoding UTF8
$result | ConvertTo-Json -Depth 8
if ($failed.Count -gt 0) { exit 1 }
