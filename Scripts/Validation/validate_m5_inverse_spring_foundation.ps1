param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))

$ErrorActionPreference = "Stop"
$root = Join-Path $ProjectRoot "Docs\Validation\M5"
$automationPath = Join-Path $root "Automation\index.json"
$reportPath = Join-Path $root "inverse_spring_foundation_validation.json"
$curvePath = Join-Path $root "inverse_spring_loss_curve.csv"
$automation = Get-Content -LiteralPath $automationPath -Raw -Encoding UTF8 | ConvertFrom-Json

function Get-MetricMessage([string]$TestPath, [string]$Prefix) {
    $test = @($automation.tests | Where-Object fullTestPath -eq $TestPath)[0]
    return @($test.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "$Prefix*" })[0]
}
function Get-Values([string]$Message) {
    $values = @{}
    foreach ($match in [regex]::Matches($Message, '(?<key>[a-z][a-z0-9_]*?)=(?<value>[^\s]+)')) {
        $values[$match.Groups['key'].Value] = $match.Groups['value'].Value
    }
    return $values
}

$gradient = Get-Values (Get-MetricMessage "PhysicsResearch.InverseDesign.SpringGradient" "M5_INVERSE_GRADIENT")
$optimization = Get-Values (Get-MetricMessage "PhysicsResearch.InverseDesign.SpringOptimization" "M5_INVERSE_OPTIMIZATION")
$history = Get-Values (Get-MetricMessage "PhysicsResearch.InverseDesign.SpringOptimization" "M5_INVERSE_LOSS_HISTORY")
$boundary = Get-Values (Get-MetricMessage "PhysicsResearch.InverseDesign.SpringIdentifiabilityBoundary" "M5_INVERSE_IDENTIFIABILITY")
$lossValues = @($history.values -split ',' | ForEach-Object { [double]$_ })

$curve = for ($index = 0; $index -lt $lossValues.Count; ++$index) {
    [pscustomobject]@{ iteration = $index; loss = $lossValues[$index] }
}
$curve | Export-Csv -LiteralPath $curvePath -NoTypeInformation -Encoding UTF8

$checks = [ordered]@{
    automation_3_of_3 = ([int]$automation.succeeded -eq 3 -and [int]$automation.failed -eq 0 -and [int]$automation.notRun -eq 0)
    tangent_matches_centered_finite_difference = ([double]$gradient.maximum_relative_error -lt 2.0e-5)
    optimizer_reduces_loss_by_four_orders = ([double]$optimization.final_loss -lt [double]$optimization.initial_loss * 1.0e-4)
    optimizer_recovers_drive = ([math]::Abs([double]$optimization.recovered_drive - [double]$optimization.truth_drive) -lt 0.25)
    optimizer_recovers_damping = ([math]::Abs([double]$optimization.recovered_damping - [double]$optimization.truth_damping) -lt 0.15)
    convergence_curve_is_strictly_decreasing = ($lossValues.Count -gt 5 -and @((1..($lossValues.Count - 1)) | Where-Object { $lossValues[$_] -ge $lossValues[$_ - 1] }).Count -eq 0)
    position_only_boundary_fits_but_is_non_identifiable = ([double]$boundary.final_loss -lt [double]$boundary.initial_loss * 1.0e-4 -and [double]$boundary.parameter_l1_error -gt 1.0)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = $(if ($failed.Count -eq 0) { "passed" } else { "failed" })
    interpretation = "Fixed-step semi-implicit spring dynamics use forward tangent sensitivities. Centered finite differences are validation truth only. A short-horizon position-only objective is intentionally retained as a non-identifiable failure boundary."
    checks = $checks
    gradient = $gradient
    optimization = $optimization
    identifiability_boundary = $boundary
    loss_curve_csv = "Docs/Validation/M5/inverse_spring_loss_curve.csv"
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 6
if ($failed.Count -gt 0) { exit 1 }
