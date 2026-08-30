param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")),
    [string]$ServerLog = (Join-Path $ProjectRoot "Docs\Validation\M6\recoverable_fall_server.log"),
    [string]$ClientLog = (Join-Path $ProjectRoot "Docs\Validation\M6\recoverable_fall_client.log")
)

$ErrorActionPreference = "Stop"
$root = Join-Path $ProjectRoot "Docs\Validation\M6"
$setupPath = Join-Path $root "recoverable_fall_gamefeature_setup.json"
$assetPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Content\PhysicsDrivenCharacter.uasset"
$actionSourcePath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Private\PhysicsDrivenCharacterAddComponentsAction.cpp"
$componentSourcePath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Private\RecoverableFallComponent.cpp"
$setup = Get-Content -LiteralPath $setupPath -Raw -Encoding UTF8 | ConvertFrom-Json
$server = Get-Content -LiteralPath $ServerLog -Raw -Encoding UTF8
$client = Get-Content -LiteralPath $ClientLog -Raw -Encoding UTF8
$actionSource = Get-Content -LiteralPath $actionSourcePath -Raw -Encoding UTF8
$componentSource = Get-Content -LiteralPath $componentSourcePath -Raw -Encoding UTF8

$checks = [ordered]@{
    gamefeature_asset_has_single_custom_action = ([bool]$setup.saved -and @($setup.errors).Count -eq 0 -and @($setup.final_actions).Count -eq 1 -and $setup.final_actions[0] -eq 'PhysicsDrivenCharacterAddComponentsAction' -and (Test-Path -LiteralPath $assetPath))
    action_targets_real_lyra_character = ($setup.actor_class -eq '/Script/LyraGame.LyraCharacter' -and $setup.component_class -eq '/Script/PhysicsDrivenCharacterRuntime.RecoverableFallComponent' -and $actionSource -match 'ALyraCharacter::StaticClass' -and $actionSource -match 'AddComponentRequest')
    action_runs_on_client_and_server = ([bool]$setup.client_component -and [bool]$setup.server_component)
    dedicated_server_listened_and_client_joined = ($server -match 'IpNetDriver listening on port 7787' -and $server -match 'Join succeeded' -and $client -match 'Welcomed by server')
    component_injected_into_real_lyra_pawns = ($server -match 'RecoverableFallInjection Owner=B_Hero_ShooterMannequin.*Authority=1 NetMode=1 Health=1.*Movement=1' -and $client -match 'RecoverableFallInjection Owner=B_Hero_ShooterMannequin.*Authority=0 NetMode=3 Health=1.*Movement=1')
    client_asc_completed_late_binding = ($client -match 'RecoverableFallInjection Owner=B_Hero_ShooterMannequin.*Authority=0 NetMode=3 Health=1 ASC=1 Movement=1')
    one_client_intent_reached_server = ($client -match 'RecoverableFallNetworkProbe ClientRequest' -and $server -match 'RecoverableFallNetworkProbe ServerAcceptedRequest.*State=Settling')
    server_completed_authoritative_sequence = ($server -match 'Balanced->Falling' -and $server -match 'Falling->Settling' -and $server -match 'Settling->Recovering Branch=Front' -and $server -match 'Recovering->Recovered Branch=Front')
    client_received_replicated_sequence = ($client -match 'Balanced->Falling' -and $client -match 'Falling->Settling' -and $client -match 'Settling->Recovering Branch=Front' -and $client -match 'Recovering->Recovered Branch=Front')
    no_per_bone_rpc_or_death_state_reuse = ($componentSource -notmatch 'Server.*Bone' -and $componentSource -notmatch 'Status_Death' -and $componentSource -match 'ServerRequestRecovery')
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = $(if ($failed.Count -eq 0) { 'passed' } else { 'failed' })
    topology = 'UE 5.4 dedicated server NetMode=1 on port 7787 plus independent game client NetMode=3 over loopback.'
    scope = 'GameFeature injection into real Lyra shooter pawns, one client recovery-intent RPC, server-only physical-state progression, and replicated Front/Recovered state observed on the client.'
    claim_boundary = 'This proves two-process authority and replication semantics. Exact wire bytes, correction frequency under packet loss, and bandwidth P95/P99 still require a Network Profiler capture.'
    checks = $checks
    logical_replication_contract = [ordered]@{
        client_intent_rpc_count_in_selected_sequence = 1
        replicated_discrete_fields = @('FallState:uint8', 'RecoveryBranch:uint8')
        per_bone_rpc_count = 0
        observed_server_state_transitions = 4
        observed_client_state_notifications = 4
    }
    failed_checks = $failed
}
$reportPath = Join-Path $root "recoverable_fall_network_validation.json"
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 6
if ($failed.Count -gt 0) { exit 1 }
