param(
    [string]$ReportPath = 'Docs/Validation/M6/AutomationVerticalLanding/index.json',
    [string]$OutputPath = 'Docs/Validation/M6/vertical_landing_validation.json'
)

$ErrorActionPreference = 'Stop'

function Convert-MetricLine {
    param([string]$Line)
    $metrics = [ordered]@{}
    foreach ($match in [regex]::Matches($Line, '(?<key>[a-z0-9_]+)=(?<value>[^\s]+)')) {
        $raw = $match.Groups['value'].Value
        $value = if ($raw -eq 'true') { $true } elseif ($raw -eq 'false') { $false } else {
            [double]$number = 0
            if ([double]::TryParse($raw, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$number)) { $number } else { $raw }
        }
        $metrics[$match.Groups['key'].Value] = $value
    }
    return $metrics
}

$report = Get-Content -LiteralPath $ReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$absorptionTest = $report.tests | Where-Object fullTestPath -eq 'PhysicsResearch.DrivenCharacter.VerticalLanding.Absorption'
$failureTest = $report.tests | Where-Object fullTestPath -eq 'PhysicsResearch.DrivenCharacter.VerticalLanding.TravelFailure'
$absorptionLine = @($absorptionTest.entries.event.message | Where-Object { $_ -like 'M6_VERTICAL_LANDING*' })[0]
$failureLine = @($failureTest.entries.event.message | Where-Object { $_ -like 'M6_VERTICAL_FAILURE*' })[0]
$absorption = Convert-MetricLine $absorptionLine
$failure = Convert-MetricLine $failureLine

$checks = [ordered]@{
    full_driven_character_regression_13_of_13 = [bool]($report.succeeded -eq 13 -and $report.failed -eq 0 -and $report.succeededWithWarnings -eq 0)
    absorption_test_succeeded = [bool]($absorptionTest.state -eq 'Success')
    gravity_impact_speed_matches = [bool]([Math]::Abs($absorption.impact_speed_m_s - $absorption.expected_impact_speed_m_s) -lt 0.03)
    standard_drop_lands_once = [bool]($absorption.landed -and $absorption.contacts -eq 1)
    standard_drop_settles = [bool]($absorption.settled -and -not $absorption.failed)
    peak_force_is_finite_and_positive = [bool]([double]::IsFinite($absorption.peak_force_n) -and $absorption.peak_force_n -gt 0)
    compression_is_inside_40cm_budget = [bool]($absorption.max_compression_m -gt 0 -and $absorption.max_compression_m -lt 0.4)
    damping_dissipation_is_positive = [bool]($absorption.damping_j -gt 0)
    failure_test_succeeded = [bool]($failureTest.state -eq 'Success')
    stressed_drop_records_contact = [bool]$failure.landed
    insufficient_travel_fails = [bool]($failure.failed -and -not $failure.settled)
    failure_crosses_12cm_budget = [bool]($failure.max_compression_m -gt $failure.travel_budget_m)
    failure_respects_2500n_force_cap = [bool]($failure.peak_force_n -le $failure.force_budget_n)
}
$failedChecks = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$result = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = ($failedChecks.Count -eq 0 ? 'passed' : 'failed')
    scope = 'Case05 standard benchmark 2: one-DOF vertical COM drop, compliant leg absorption, settling, and explicit travel/force failure.'
    model_boundary = 'Programmatic SI-unit truth model. It fixes the future Manny/Chaos height, impulse, force, travel, dissipation, and settling contract; it is not itself a Chaos multibody result.'
    standard_drop = $absorption
    stressed_failure = $failure
    checks = $checks
    failed_checks = $failedChecks
}

$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory) { New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null }
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
$result | ConvertTo-Json -Depth 8
if ($failedChecks.Count -gt 0) { exit 1 }
