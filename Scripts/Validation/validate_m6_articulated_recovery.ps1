param(
    [string]$ReportPath = 'Docs/Validation/M6/AutomationArticulatedRecovery/index.json',
    [string]$FailureReportPath = 'Docs/Validation/M6/AutomationArticulatedFirstFailure/index.json',
    [string]$OutputPath = 'Docs/Validation/M6/articulated_recovery_validation.json'
)
$ErrorActionPreference = 'Stop'

function Convert-Metrics([string]$Line) {
    $result = [ordered]@{}
    foreach ($match in [regex]::Matches($Line, '(?<key>[a-z0-9_]+)=(?<value>[^\s]+)')) {
        $raw = $match.Groups['value'].Value
        if ($raw -eq 'true') { $value = $true } elseif ($raw -eq 'false') { $value = $false } else {
            [double]$number = 0
            $value = if ([double]::TryParse($raw, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$number)) { $number } else { $raw }
        }
        $result[$match.Groups['key'].Value] = $value
    }
    return $result
}
function Get-Line($Report, [string]$Prefix) {
    return @($Report.tests.entries.event.message | Where-Object { $_ -like "$Prefix*" })[0]
}

$report = Get-Content -LiteralPath $ReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$firstFailureReport = Get-Content -LiteralPath $FailureReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$recovery = Convert-Metrics (Get-Line $report 'M6_ARTICULATED_RECOVERY')
$coupling = Convert-Metrics (Get-Line $report 'M6_ARTICULATED_COUPLING')
$failure = Convert-Metrics (Get-Line $report 'M6_ARTICULATED_FAILURE')
$firstFailure = Convert-Metrics (Get-Line $firstFailureReport 'M6_ARTICULATED_FAILURE')

$checks = [ordered]@{
    full_regression_16_of_16 = [bool]($report.succeeded -eq 16 -and $report.failed -eq 0 -and $report.succeededWithWarnings -eq 0)
    large_pose_recovers = [bool]($recovery.recovered -and -not $recovery.failed)
    recovery_reduces_rms_below_two_degrees = [bool]($recovery.initial_rms_deg -gt 50 -and $recovery.final_rms_deg -lt 2)
    recovery_finishes_inside_timeout = [bool]($recovery.recovery_time_s -gt 0 -and $recovery.recovery_time_s -lt 5.5)
    peak_torques_respect_standard_budgets = [bool]($recovery.peak_torque_hip_nm -le 450 -and $recovery.peak_torque_knee_nm -le 300 -and $recovery.peak_torque_spine_nm -le 180)
    work_is_finite_and_positive = [bool]([double]::IsFinite($recovery.work_j) -and $recovery.work_j -gt 0)
    coupled_impulse_recovers = [bool]$coupling.coupled_recovered
    uncoupled_impulse_recovers = [bool]$coupling.uncoupled_recovered
    coupling_transfers_measurable_proximal_response = [bool]($coupling.coupled_proximal_deg -gt 0.5 -and $coupling.transfer_ratio -gt 0.05)
    uncoupled_ablation_isolates_proximal_joints = [bool]($coupling.uncoupled_proximal_deg -lt 0.001)
    low_budget_case_fails = [bool]($failure.failed -and -not $failure.recovered)
    low_budget_failure_remains_visible = [bool]($failure.final_rms_deg -gt 45)
    low_budget_case_respects_all_caps = [bool]($failure.peak_hip_nm -le $failure.torque_limit_hip_nm -and $failure.peak_knee_nm -le $failure.torque_limit_knee_nm -and $failure.peak_spine_nm -le $failure.torque_limit_spine_nm)
    first_failure_report_is_preserved = [bool]($firstFailureReport.failed -eq 1 -and $firstFailure.recovered -and -not $firstFailure.failed)
}
$failedChecks = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$result = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = ($failedChecks.Count -eq 0 ? 'passed' : 'failed')
    scope = 'Case05 standard benchmark 3: three-joint recovery, equal-and-opposite coupling, distal-impulse ablation, and explicit torque-budget failure.'
    model_boundary = 'Deterministic SI-unit articulated truth model. It is not a trained policy and is not yet a Chaos skeletal result.'
    recovery = $recovery
    coupling_ablation = $coupling
    torque_failure = $failure
    rejected_failure_assumption = $firstFailure
    checks = $checks
    failed_checks = $failedChecks
}
$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory) { New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null }
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
$result | ConvertTo-Json -Depth 8
if ($failedChecks.Count -gt 0) { exit 1 }
