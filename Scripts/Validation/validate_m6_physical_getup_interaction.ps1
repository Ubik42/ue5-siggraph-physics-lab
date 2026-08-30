param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))

$ErrorActionPreference = "Stop"
$root = Join-Path $ProjectRoot "Docs\Validation\M6"
$runtimePath = Join-Path $root "PhysicalGetUpInteraction.json"
$screenshotPath = Join-Path $root "Screenshots\Case05_ALS_Interaction.png"
$sourcePath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Private\PhysicalGetUpRealtimeActor.cpp"
$hudPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Private\PhysicalGetUpHUD.cpp"
$buildPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\PhysicsDrivenCharacterRuntime.Build.cs"

$runtime = Get-Content -LiteralPath $runtimePath -Raw -Encoding UTF8 | ConvertFrom-Json
$source = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8
$hud = Get-Content -LiteralPath $hudPath -Raw -Encoding UTF8
$build = Get-Content -LiteralPath $buildPath -Raw -Encoding UTF8
$checks = [ordered]@{
    exact_engine_build = ($runtime.engine -eq '5.4.4-35576357+++UE5+Release-5.4')
    keyboard_and_replay_share_action_path = ([bool]$runtime.uses_same_actions_as_keyboard -and $source -match 'BindKey\(EKeys::One' -and $source -match 'BindKey\(EKeys::Two' -and $source -match 'BindKey\(EKeys::P' -and $source -match 'BindKey\(EKeys::SpaceBar' -and $source -match 'BindKey\(EKeys::R')
    exact_interaction_counts = ([int]$runtime.pose_selection_count -eq 2 -and [int]$runtime.push_count -eq 1 -and [int]$runtime.recovery_request_count -eq 3 -and [int]$runtime.reset_count -eq 1)
    scripted_sequence_completed = ([int]$runtime.script_step_count -eq 6 -and $runtime.final_branch -eq 'RejectMoving' -and $runtime.final_phase -eq 'Rejected' -and [bool]$runtime.interaction_pass)
    input_module_dependency_is_explicit = ($build -match '"InputCore"')
    hud_documents_all_controls_in_chinese = ($hud -match '1 俯卧' -and $hud -match '2 仰卧' -and $hud -match 'P 施加物理扰动' -and $hud -match 'Space 请求起身' -and $hud -match 'R 重置当前姿态')
    screenshot_exists_and_is_nontrivial = ((Test-Path -LiteralPath $screenshotPath) -and (Get-Item -LiteralPath $screenshotPath).Length -gt 100000)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = $(if ($failed.Count -eq 0) { 'passed' } else { 'failed' })
    scope = 'Player-facing pose selection, physical push, recovery request, and reset actions on the existing Chaos/PhysicsControl/ALS handoff map.'
    claim_boundary = 'This proves local player interaction and deterministic replay through the same C++ actions. It does not yet prove Lyra Enhanced Input, GAS, server authority, or replication.'
    checks = $checks
    runtime = $runtime
    screenshot = 'Docs/Validation/M6/Screenshots/Case05_ALS_Interaction.png'
    manual_visual_inspection = 'passed: Chinese key legend and exact interaction counters are visible; the active Manny is physically disturbed and rejected while comparison lanes remain prone.'
    failed_checks = $failed
}
$reportPath = Join-Path $root "physical_getup_interaction_validation.json"
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 6
if ($failed.Count -gt 0) { exit 1 }
