param(
    [string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")),
    [string]$AutomationLog = (Join-Path $ProjectRoot "Docs\Validation\M6\recoverable_fall_authority_automation.log")
)

$ErrorActionPreference = "Stop"
$root = Join-Path $ProjectRoot "Docs\Validation\M6"
$componentHeaderPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Public\RecoverableFallComponent.h"
$componentSourcePath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Private\RecoverableFallComponent.cpp"
$policyPath = Join-Path $ProjectRoot "Plugins\GameFeatures\PhysicsDrivenCharacter\Source\PhysicsDrivenCharacterRuntime\Private\RecoverableFallState.cpp"
$componentHeader = Get-Content -LiteralPath $componentHeaderPath -Raw -Encoding UTF8
$componentSource = Get-Content -LiteralPath $componentSourcePath -Raw -Encoding UTF8
$policy = Get-Content -LiteralPath $policyPath -Raw -Encoding UTF8
$automation = Get-Content -LiteralPath $AutomationLog -Raw -Encoding UTF8
$successes = [regex]::Matches($automation, 'Test Completed\. Result=\{成功\}.*Path=\{PhysicsResearch\.DrivenCharacter\.')
$recoverableSuccesses = [regex]::Matches($automation, 'Test Completed\. Result=\{成功\}.*Path=\{PhysicsResearch\.DrivenCharacter\.RecoverableFall\.')

$checks = [ordered]@{
    full_driven_character_regression_is_11_of_11 = ($successes.Count -eq 11 -and $automation -notmatch 'Test Completed\. Result=\{失败\}')
    recoverable_authority_tests_are_4_of_4 = ($recoverableSuccesses.Count -eq 4)
    state_and_branch_are_replicated = ($componentHeader -match 'ReplicatedUsing=OnRep_FallState' -and $componentHeader -match '(?s)UPROPERTY\(Replicated\)\s*EPhysicalGetUpBranch RecoveryBranch')
    recovery_intent_is_one_reliable_rpc = ($componentHeader -match 'UFUNCTION\(Server, Reliable' -and $componentHeader -match 'ServerRequestRecovery')
    no_per_bone_rpc_contract = ($componentHeader -notmatch 'Bone.*Server' -and $componentSource -notmatch 'Server.*Bone')
    death_state_has_priority = ($componentSource -match 'ULyraHealthComponent::FindHealthComponent' -and $componentSource -match 'IsDeadOrDying' -and $policy -match 'bDeathActive.*Failed')
    lyra_gas_tags_are_separate_from_death = ($componentSource -match 'Status\.PhysicsRecoverableFall' -and $componentSource -notmatch 'SetLooseGameplayTagCount\([^\r\n]*Status_Death')
    movement_is_server_gated = ($componentSource -match 'DisableMovement\(\)' -and $componentSource -match 'SetMovementMode\(MOVE_Walking\)' -and $componentSource -match 'NewState == ERecoverableFallState::Recovered')
    server_policy_consumes_physical_contract = ($policy -match 'FPhysicalGetUpDecision::Evaluate' -and $policy -match 'bRecoveryRequested' -and $policy -match 'bGrounded')
    client_cannot_advance_policy = ($policy -match '!Input\.bHasAuthority.*return Current')
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = $(if ($failed.Count -eq 0) { 'passed' } else { 'failed' })
    scope = 'Replicated Lyra-aware RecoverableFall state foundation with server-only transition policy, DeathState priority, GAS loose tags, one recovery-intent RPC, and Character Movement authority gate.'
    claim_boundary = 'This is a compiled and tested authority foundation. It is not yet attached through a Lyra Experience/GameFeature action and does not yet prove a two-process server/client replication session or correction bandwidth.'
    checks = $checks
    driven_character_automation_success_count = $successes.Count
    recoverable_fall_automation_success_count = $recoverableSuccesses.Count
    failed_checks = $failed
}
$reportPath = Join-Path $root "recoverable_fall_authority_validation.json"
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 5
if ($failed.Count -gt 0) { exit 1 }
