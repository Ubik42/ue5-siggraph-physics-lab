param(
    [string]$ServerExport = 'Docs/Validation/M6/NetworkProfiles/Case05_Server_100ms_5pct.json',
    [string]$ClientExport = 'Docs/Validation/M6/NetworkProfiles/Case05_Client_100ms_5pct.json',
    [string]$OutputPath = 'Docs/Validation/M6/network_profile_attribution_validation.json'
)

$ErrorActionPreference = 'Stop'

function Get-NearestRankPercentile {
    param([double[]]$Values, [double]$Percentile)
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 0) { return 0 }
    $rank = [Math]::Ceiling($Percentile * $sorted.Count) - 1
    return $sorted[[Math]::Max(0, [Math]::Min($rank, $sorted.Count - 1))]
}

function Get-AttributedBits {
    param([object[]]$Events)
    [long]$sum = 0
    foreach ($event in $Events) {
        switch ($event.type) {
            'ReplicateProperty' { $sum += [long]$event.payload_bits }
            'WritePropertyHeader' { $sum += [long]$event.header_bits }
            'WritePropertyHandle' { $sum += [long]$event.bits }
            'BeginContentBlock' { $sum += [long]$event.header_bits }
            'EndContentBlock' { $sum += [long]$event.footer_bits }
        }
    }
    return $sum
}

$server = Get-Content -LiteralPath $ServerExport -Raw -Encoding UTF8 | ConvertFrom-Json
$client = Get-Content -LiteralPath $ClientExport -Raw -Encoding UTF8 | ConvertFrom-Json

$clientRecoveryRpcs = @($client.matching_events | Where-Object { $_.type -eq 'SendRPC' -and $_.function -eq 'ServerRequestRecovery' })
$recoveryRpc = $clientRecoveryRpcs | Select-Object -First 1

$serverTargetProperties = @($server.matching_events | Where-Object {
    $_.type -eq 'ReplicateProperty' -and
    $_.content -eq 'RecoverableFallComponent' -and
    $_.property -in @('FallState', 'RecoveryBranch')
})
$propertyGroups = @($serverTargetProperties | Group-Object { [string]$_.time_s } | Sort-Object { [double]$_.Name })
$dualGroups = @($propertyGroups | Where-Object {
    @($_.Group | Where-Object property -eq 'FallState').Count -eq 1 -and
    @($_.Group | Where-Object property -eq 'RecoveryBranch').Count -eq 1
})
if ($dualGroups.Count -lt 1) { throw 'No paired FallState/RecoveryBranch recovery update was found.' }

$firstRecoveryTime = [double]$dualGroups[0].Name
$lastDualTime = [double]$dualGroups[-1].Name
$recoveredGroup = $propertyGroups | Where-Object {
    [double]$_.Name -gt $lastDualTime -and
    @($_.Group | Where-Object property -eq 'FallState').Count -eq 1 -and
    @($_.Group | Where-Object property -eq 'RecoveryBranch').Count -eq 0
} | Select-Object -First 1
if (-not $recoveredGroup) { throw 'No terminal FallState-only recovery update was found after paired updates.' }

$selectedGroups = @($dualGroups) + @($recoveredGroup)
$attempts = @($selectedGroups | ForEach-Object {
    [double]$time = $_.Name
    $frameEvents = @($server.matching_events | Where-Object {
        [double]$_.time_s -eq $time -and $_.content -eq 'RecoverableFallComponent'
    })
    [pscustomobject][ordered]@{
        time_s = $time
        fall_state_writes = @($_.Group | Where-Object property -eq 'FallState').Count
        recovery_branch_writes = @($_.Group | Where-Object property -eq 'RecoveryBranch').Count
        property_payload_bits = [long](($_.Group | Measure-Object -Property payload_bits -Sum).Sum)
        profiler_attributed_bits = Get-AttributedBits $frameEvents
    }
})

$attributedSamples = [double[]]@($attempts | ForEach-Object { [double]$_.profiler_attributed_bits })
$intervals = @()
for ($index = 1; $index -lt $dualGroups.Count; $index++) {
    $intervals += [double]$dualGroups[$index].Name - [double]$dualGroups[$index - 1].Name
}
$checks = [ordered]@{
    both_profiles_reached_end_of_stream = [bool]($server.parsed.end_of_stream -and $client.parsed.end_of_stream)
    parser_version_is_ue54_v14 = [bool]($server.header.version -eq 14 -and $client.header.version -eq 14)
    client_sent_exactly_one_recovery_rpc = [bool]($clientRecoveryRpcs.Count -eq 1)
    recovery_rpc_total_is_29_bits = [bool]($recoveryRpc.total_bits -eq 29)
    recovery_has_three_paired_attempts = [bool]($dualGroups.Count -eq 3)
    recovery_has_two_correction_repeats = [bool](($dualGroups.Count - 1) -eq 2)
    terminal_recovered_update_is_present = [bool]($recoveredGroup)
    selected_window_has_four_state_writes = [bool](($attempts | Measure-Object -Property fall_state_writes -Sum).Sum -eq 4)
    selected_window_has_three_branch_writes = [bool](($attempts | Measure-Object -Property recovery_branch_writes -Sum).Sum -eq 3)
    selected_window_property_payload_is_21_bits = [bool](($attempts | Measure-Object -Property property_payload_bits -Sum).Sum -eq 21)
    profiler_attributed_distribution_is_finite = [bool](($attributedSamples | Where-Object { -not [double]::IsFinite($_) -or $_ -le 0 }).Count -eq 0)
}

$failedChecks = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$result = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = ($failedChecks.Count -eq 0 ? 'passed' : 'failed')
    scope = 'UE 5.4 Network Profiler v14 attribution for the selected recoverable-fall request and server recovery window under 100 ms / 5% packet simulation.'
    claim_boundary = 'RPC totals are exact serialized RPC bits. Property payload and granular profiler-attributed bits are exact for emitted profiler tokens, but they exclude untracked packed content-length bits and cannot be called standalone IP/UDP wire bytes. Whole-session socket bytes remain available in the parser export but include unrelated Lyra traffic.'
    client_recovery_rpc = [ordered]@{
        count = $clientRecoveryRpcs.Count
        time_s = [double]$recoveryRpc.time_s
        header_bits = [long]$recoveryRpc.header_bits
        parameter_bits = [long]$recoveryRpc.parameter_bits
        footer_bits = [long]$recoveryRpc.footer_bits
        total_bits = [long]$recoveryRpc.total_bits
    }
    server_recovery_window = [ordered]@{
        first_recovering_time_s = $firstRecoveryTime
        recovered_time_s = [double]$recoveredGroup.Name
        paired_update_attempts = $dualGroups.Count
        correction_repeats = $dualGroups.Count - 1
        repeat_intervals_s = $intervals
        fall_state_writes = [long](($attempts | Measure-Object -Property fall_state_writes -Sum).Sum)
        recovery_branch_writes = [long](($attempts | Measure-Object -Property recovery_branch_writes -Sum).Sum)
        property_payload_bits = [long](($attempts | Measure-Object -Property property_payload_bits -Sum).Sum)
        profiler_attributed_bits = [long](($attempts | Measure-Object -Property profiler_attributed_bits -Sum).Sum)
        attributed_bits_p95 = [double](Get-NearestRankPercentile $attributedSamples 0.95)
        attributed_bits_p99 = [double](Get-NearestRankPercentile $attributedSamples 0.99)
        attempts = $attempts
    }
    checks = $checks
    failed_checks = $failedChecks
}

$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory) { New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null }
$result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
$result | ConvertTo-Json -Depth 12
if ($failedChecks.Count -gt 0) { exit 1 }
