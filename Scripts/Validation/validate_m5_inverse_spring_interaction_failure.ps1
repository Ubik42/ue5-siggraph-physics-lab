param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))

$ErrorActionPreference = 'Stop'
$root = Join-Path $ProjectRoot 'Docs\Validation\M5'

function Get-Summary([string]$LogName) {
    $line = (Select-String -LiteralPath (Join-Path $root $LogName) -Pattern 'M5_INVERSE_REALTIME_SUMMARY' | Select-Object -Last 1).Line
    $values = @{}
    foreach ($match in [regex]::Matches($line, '(?<key>[a-z][a-z0-9_]*?)=(?<value>[^\s]+)')) {
        $values[$match.Groups['key'].Value] = $match.Groups['value'].Value
    }
    return $values
}

$interaction = Get-Summary 'InverseSpringInteraction.log'
$failure = Get-Summary 'InverseSpringFailure.log'
$failureJsonPath = Join-Path $root 'Realtime\InverseSpringRealtime_IdentifiabilityFailure.json'
$failureCsvPath = Join-Path $root 'Realtime\InverseSpringRealtime_IdentifiabilityFailure.csv'
$failureScreenshotPath = Join-Path $root 'Screenshots\Case04_InverseSpring_IdentifiabilityFailure.png'
$failureJson = Get-Content -LiteralPath $failureJsonPath -Raw -Encoding UTF8 | ConvertFrom-Json
$failureSamples = @(Import-Csv -LiteralPath $failureCsvPath)
$finalFailureSample = $failureSamples[-1]
$recomputedResearchFinalError = [math]::Abs([double]$finalFailureSample.research_cm - [double]$finalFailureSample.target_cm)

$checks = [ordered]@{
    interaction_finishes_standard_runtime = ($interaction.pass -eq 'true' -and $interaction.complete -eq 'true' -and $interaction.failure -eq 'false')
    pause_resume_and_natural_single_step_exercised = ([int]$interaction.pause -eq 2 -and [int]$interaction.single -eq 1)
    reoptimization_and_full_reset_exercised = ([int]$interaction.reoptimize -eq 1 -and [int]$interaction.reset -eq 1)
    debug_and_manual_export_exercised = ([int]$interaction.debug -eq 1 -and [int]$interaction.export -eq 1)
    interaction_script_reaches_final_phase = ([int]$interaction.interaction_phase -eq 7 -and [int]$interaction.steps -eq 120 -and [int]$interaction.chaos_samples -eq 120)
    failure_is_expected_not_passed = ($failure.pass -eq 'false' -and $failure.expected_failure -eq 'true' -and $failure.failure -eq 'true')
    failure_uses_short_position_only_observation = ([int]$failure.observation_step -eq 30 -and [int]$failure.steps -eq 120)
    failure_fits_observation_but_parameters_are_wrong = ([double]$failureJson.fitted_loss -lt [double]$failureJson.initial_loss * 1.0e-4 -and [double]$failure.parameter_l1_error -gt 10.0)
    failure_long_horizon_diverges = ([double]$failure.research_final_target_error_cm -gt 100.0 -and [double]$failure.chaos_final_target_error_cm -gt 100.0)
    failure_csv_recomputes_research_final_error = ($failureSamples.Count -eq 120 -and [math]::Abs($recomputedResearchFinalError - [double]$failureJson.research_final_target_error_cm) -lt 1.0e-6)
    failure_screenshot_exists_and_is_nontrivial = ((Test-Path -LiteralPath $failureScreenshotPath) -and (Get-Item -LiteralPath $failureScreenshotPath).Length -gt 100000)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = $(if ($failed.Count -eq 0) { 'passed' } else { 'failed' })
    checks = $checks
    interaction = $interaction
    identifiability_failure = $failureJson
    recomputed_failure_research_final_error_cm = $recomputedResearchFinalError
    manual_visual_inspection = 'passed: orange failure HUD, observation step 30, low fitted loss, large parameter error, all four bodies and controls are visible'
    failed_checks = $failed
}
$reportPath = Join-Path $root 'inverse_spring_interaction_failure_validation.json'
$report | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 7
if ($failed.Count -gt 0) { exit 1 }
