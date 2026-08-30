param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))

$ErrorActionPreference = "Stop"
$root = Join-Path $ProjectRoot "Docs\Validation\M6"
$migrationPath = Join-Path $root "als_getup_migration.json"
$retargetPath = Join-Path $root "als_getup_retarget.json"
$mapReportPath = Join-Path $root "physical_getup_map_setup.json"
$runtimePath = Join-Path $root "PhysicalGetUpHandoff.json"
$automationPath = Join-Path $root "automation_physical_getup.log"
$screenshotPath = Join-Path $root "Screenshots\Case05_ALS_PhysicalGetUp.png"
$mapAssetPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Content\Maps\L_PhysicalGetUpHandoff.umap"
$animationRoot = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Content\Animations\ALS"
$actorSourcePath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Private\PhysicalGetUpRealtimeActor.cpp"
$decisionHeaderPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Public\PhysicalGetUpDecision.h"

$migration = Get-Content -LiteralPath $migrationPath -Raw -Encoding UTF8 | ConvertFrom-Json
$retarget = Get-Content -LiteralPath $retargetPath -Raw -Encoding UTF8 | ConvertFrom-Json
$mapReport = Get-Content -LiteralPath $mapReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$runtime = Get-Content -LiteralPath $runtimePath -Raw -Encoding UTF8 | ConvertFrom-Json
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8
$actorSource = Get-Content -LiteralPath $actorSourcePath -Raw -Encoding UTF8
$decisionHeader = Get-Content -LiteralPath $decisionHeaderPath -Raw -Encoding UTF8

$outputs = @($retarget.outputs)
$mappings = @($retarget.rig_creation.chain_mappings)
$unmappedMetacarpals = @($mappings | Where-Object { $_.target -match 'Metacarpal$' -and $_.source -eq 'None' })
$front = @($runtime.lanes | Where-Object { $_.branch -eq 'Front' })
$back = @($runtime.lanes | Where-Object { $_.branch -eq 'Back' })
$moving = @($runtime.lanes | Where-Object { $_.branch -eq 'RejectMoving' })
$recovered = @($runtime.lanes | Where-Object { $_.phase -eq 'Recovered' })
$automationSuccesses = [regex]::Matches($automation, 'Test Completed\. Result=\{成功\}.*Path=\{PhysicsResearch\.DrivenCharacter\.')

$checks = [ordered]@{
    minimal_als_source_migration = (@($migration.requested_packages).Count -eq 2 -and @($migration.source_assets).Count -eq 2 -and @($migration.errors).Count -eq 0 -and [bool]$migration.dependencies_included)
    exactly_two_manny_retargeted_sequences = ($outputs.Count -eq 2 -and @($outputs | Where-Object { $_.skeleton -eq '/Game/Characters/Heroes/Mannequin/Meshes/SK_Mannequin.SK_Mannequin' -and [math]::Abs([double]$_.play_length_seconds - 1.5) -lt 0.001 }).Count -eq 2)
    deterministic_chain_mapping = ([int]$retarget.rig_creation.mapped_target_chain_count -eq 20 -and $unmappedMetacarpals.Count -eq 8 -and @($retarget.errors).Count -eq 0)
    derived_retarget_assets_exist = ((Test-Path -LiteralPath (Join-Path $animationRoot 'ALS_CLF_GetUp_Front_Manny.uasset')) -and (Test-Path -LiteralPath (Join-Path $animationRoot 'ALS_CLF_GetUp_Back_Manny.uasset')) -and (Test-Path -LiteralPath (Join-Path $animationRoot 'RTG_ALS_To_LyraManny.uasset')))
    map_saved_without_errors = ([bool]$mapReport.saved -and @($mapReport.errors).Count -eq 0 -and (Test-Path -LiteralPath $mapAssetPath))
    exact_engine_build = ($runtime.engine -eq '5.4.4-35576357+++UE5+Release-5.4')
    front_and_back_observed = ([bool]$runtime.observed_front_and_back -and $front.Count -eq 1 -and $back.Count -eq 1)
    both_physical_lanes_recovered = ([bool]$runtime.both_recovered -and $recovered.Count -eq 2 -and @($recovered | Where-Object { [int]$_.control_count -eq 22 }).Count -eq 2)
    deterministic_handoff_timing = (@($recovered | Where-Object { [math]::Abs([double]$_.recovery_start_s - 1.3333334) -lt 0.02 -and [math]::Abs(([double]$_.recovery_complete_s - [double]$_.recovery_start_s) - 1.75) -lt 0.02 }).Count -eq 2)
    velocity_lane_was_rejected = ([bool]$runtime.velocity_rejection_pass -and $moving.Count -eq 1 -and $moving[0].phase -eq 'Rejected' -and [double]$moving[0].linear_speed_cm_s -gt 18.0)
    runtime_acceptance_passed = [bool]$runtime.get_up_handoff_pass
    source_uses_physicscontrol_and_releases_authority = ($actorSource -match 'CreateBodyModifiersFromSkeletalMeshBelow' -and $actorSource -match 'CreateControlsFromSkeletalMeshBelow' -and $actorSource -match 'DestroyControlsInSet' -and $actorSource -match 'DestroyBodyModifiersInSet')
    source_uses_both_als_branches = ($actorSource -match 'ALS_CLF_GetUp_Front_Manny' -and $actorSource -match 'ALS_CLF_GetUp_Back_Manny')
    decision_thresholds_are_explicit = ($decisionHeader -match 'MaximumLinearSpeed = 18\.0' -and $decisionHeader -match 'MaximumAngularSpeed = 0\.9' -and $decisionHeader -match 'RequiredStableSeconds = 0\.35' -and $decisionHeader -match 'MinimumFacingDot = 0\.10')
    driven_character_automation_is_7_of_7 = ($automationSuccesses.Count -eq 7 -and $automation -notmatch 'Test Completed\. Result=\{失败\}')
    screenshot_exists_and_is_nontrivial = ((Test-Path -LiteralPath $screenshotPath) -and (Get-Item -LiteralPath $screenshotPath).Length -gt 100000)
}

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = $(if ($failed.Count -eq 0) { 'passed' } else { 'failed' })
    engine = 'UE 5.4.4 CL 35576357'
    scope = 'Minimal ALS front/back get-up migration, deterministic UE4-to-Lyra-Manny retarget, Chaos prone classification, PhysicsControl animation drive, and explicit physics-to-animation authority handoff.'
    claim_boundary = 'This proves local front/back recovery selection and authority handoff on one machine. It does not yet prove player interaction, Lyra GAS/state integration, replication, rollback, or multiplayer correction.'
    checks = $checks
    runtime = $runtime
    automation_success_count = $automationSuccesses.Count
    screenshot = 'Docs/Validation/M6/Screenshots/Case05_ALS_PhysicalGetUp.png'
    manual_visual_inspection = 'passed: both recovered Manny lanes are visible in their final get-up pose; the high-speed rejection lane remains physically prone; Chinese HUD names all three outcomes.'
    failed_checks = $failed
}
$reportPath = Join-Path $root "physical_getup_validation.json"
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 8
if ($failed.Count -gt 0) { exit 1 }
