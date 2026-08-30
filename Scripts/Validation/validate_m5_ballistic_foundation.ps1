param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))

$ErrorActionPreference = 'Stop'
$root = Join-Path $ProjectRoot 'Docs\Validation\M5'
$automation = Get-Content -LiteralPath (Join-Path $root 'Automation\index.json') -Raw -Encoding UTF8 | ConvertFrom-Json

function Get-Metric([string]$TestPath, [string]$Prefix) {
    $test = @($automation.tests | Where-Object fullTestPath -eq $TestPath)[0]
    $message = @($test.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "$Prefix*" })[0]
    $values = @{}
    foreach ($match in [regex]::Matches($message, '(?<key>[a-z][a-z0-9_]*?)=(?<value>[^\s]+)')) { $values[$match.Groups['key'].Value] = $match.Groups['value'].Value }
    return $values
}

$gradient = Get-Metric 'PhysicsResearch.InverseDesign.BallisticGradient' 'M5_BALLISTIC_GRADIENT'
$optimization = Get-Metric 'PhysicsResearch.InverseDesign.BallisticOptimization' 'M5_BALLISTIC_OPTIMIZATION'
$history = Get-Metric 'PhysicsResearch.InverseDesign.BallisticOptimization' 'M5_BALLISTIC_LOSS_HISTORY'
$boundary = Get-Metric 'PhysicsResearch.InverseDesign.BallisticLandingOnlyBoundary' 'M5_BALLISTIC_IDENTIFIABILITY'
$losses = @($history.values -split ',' | ForEach-Object { [double]$_ })
$curve = for ($index = 0; $index -lt $losses.Count; ++$index) { [pscustomobject]@{ iteration = $index; loss = $losses[$index] } }
$curvePath = Join-Path $root 'ballistic_loss_curve.csv'
$curve | Export-Csv -LiteralPath $curvePath -NoTypeInformation -Encoding UTF8
$strictlyDecreasing = $true
for ($index = 1; $index -lt $losses.Count; ++$index) { if ($losses[$index] -ge $losses[$index - 1]) { $strictlyDecreasing = $false; break } }

$checks = [ordered]@{
    inverse_design_regression_6_of_6 = ([int]$automation.succeeded -eq 6 -and [int]$automation.failed -eq 0 -and [int]$automation.notRun -eq 0)
    ballistic_tangent_matches_centered_difference = ([double]$gradient.maximum_relative_error -lt 2.0e-5)
    joint_three_parameter_fit_reduces_loss_by_six_orders = ([double]$optimization.final_loss -lt [double]$optimization.initial_loss * 1.0e-6)
    joint_fit_recovers_scaled_parameters = ([double]$optimization.parameter_scaled_l1_error -lt 2.0)
    full_convergence_curve_is_strictly_decreasing = ($losses.Count -eq [int]$optimization.history -and $losses.Count -gt 100 -and $strictlyDecreasing)
    landing_only_target_fits_but_is_non_identifiable = ([double]$boundary.final_loss -lt [double]$boundary.initial_loss * 1.0e-6 -and [double]$boundary.parameter_scaled_l1_error -gt 50.0)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = $(if ($failed.Count -eq 0) { 'passed' } else { 'failed' })
    interpretation = 'A 2D gravity-plus-linear-drag trajectory differentiates initial horizontal/vertical velocity and drag through the exact semi-implicit update. Cyclic coordinate gradient descent prevents the drag scale from dominating velocity recovery. Landing-X-only is retained as a non-identifiable boundary.'
    checks = $checks
    gradient = $gradient
    optimization = $optimization
    landing_only_boundary = $boundary
    loss_curve_csv = 'Docs/Validation/M5/ballistic_loss_curve.csv'
    failed_checks = $failed
}
$reportPath = Join-Path $root 'ballistic_foundation_validation.json'
$report | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 7
if ($failed.Count -gt 0) { exit 1 }
