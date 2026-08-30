param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))

$ErrorActionPreference = "Stop"
$root = Join-Path $ProjectRoot "Docs\Validation\M6"
$serverProfile = Join-Path $root "NetworkProfiles\Case05_Server_100ms_5pct.nprof"
$clientProfile = Join-Path $root "NetworkProfiles\Case05_Client_100ms_5pct.nprof"
$serverLog = Join-Path $root "recoverable_fall_profile_server.log"
$clientLog = Join-Path $root "recoverable_fall_profile_client.log"
$server = Get-Content -LiteralPath $serverLog -Raw -Encoding UTF8
$client = Get-Content -LiteralPath $clientLog -Raw -Encoding UTF8
$checks = [ordered]@{
    engine_packet_impairment_was_enabled = ($server -match 'PktLoss set to 5' -and $server -match 'PktLag set to 100' -and $client -match 'PktLoss set to 5' -and $client -match 'PktLag set to 100')
    server_profile_closed_cleanly = ($server -match 'Network Profiler: Closing session file' -and $server -match 'CleanExit NetMode=1')
    client_profile_closed_cleanly = ($client -match 'Network Profiler: Closing session file' -and $client -match 'CleanExit NetMode=3')
    server_profile_is_nontrivial = ((Test-Path -LiteralPath $serverProfile) -and (Get-Item -LiteralPath $serverProfile).Length -gt 100000)
    client_profile_is_nontrivial = ((Test-Path -LiteralPath $clientProfile) -and (Get-Item -LiteralPath $clientProfile).Length -gt 100000)
    impaired_client_request_reached_server = ($client -match 'ClientRequest' -and $server -match 'ServerAcceptedRequest.*State=Settling')
    impaired_server_recovered = ($server -match 'Settling->Recovering Branch=Front' -and $server -match 'Recovering->Recovered Branch=Front')
    impaired_client_converged = ($client -match 'Settling->Recovering Branch=Front' -and $client -match 'Recovering->Recovered Branch=Front')
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = $(if ($failed.Count -eq 0) { 'passed' } else { 'failed' })
    conditions = [ordered]@{ packet_lag_ms = 100; packet_loss_percent = 5 }
    scope = 'Clean UE Network Profiler captures for dedicated server and independent client while the recoverable-fall sequence converges under engine packet simulation.'
    claim_boundary = 'The raw nprof evidence is valid and nontrivial, but exact per-property/RPC wire-byte attribution and bandwidth P95/P99 have not yet been exported from the legacy GUI. This gate does not invent those numbers.'
    checks = $checks
    files = [ordered]@{
        server = [ordered]@{ path = 'Docs/Validation/M6/NetworkProfiles/Case05_Server_100ms_5pct.nprof'; bytes = (Get-Item -LiteralPath $serverProfile).Length }
        client = [ordered]@{ path = 'Docs/Validation/M6/NetworkProfiles/Case05_Client_100ms_5pct.nprof'; bytes = (Get-Item -LiteralPath $clientProfile).Length }
    }
    failed_checks = $failed
}
$reportPath = Join-Path $root "network_profile_capture_validation.json"
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 6
if ($failed.Count -gt 0) { exit 1 }
