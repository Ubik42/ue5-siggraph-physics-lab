param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))

$ErrorActionPreference = 'Stop'
$root = Join-Path $ProjectRoot 'Docs\Validation\M5'
$automation = Get-Content -LiteralPath (Join-Path $root 'Automation\index.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$firstFailure = Get-Content -LiteralPath (Join-Path $root 'AutomationFirstFailure\index.json') -Raw -Encoding UTF8 | ConvertFrom-Json

function Get-Test([string]$TestPath) {
    return @($automation.tests | Where-Object fullTestPath -eq $TestPath)[0]
}

function Get-Message([string]$TestPath, [string]$Prefix) {
    $test = Get-Test $TestPath
    return @($test.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "$Prefix*" })[0]
}

function Convert-Metrics([string]$Message) {
    $values = [ordered]@{}
    foreach ($match in [regex]::Matches($Message, '(?<key>[a-z][a-z0-9_]*?)=(?<value>[^\s]+)')) {
        $values[$match.Groups['key'].Value] = $match.Groups['value'].Value
    }
    return [pscustomobject]$values
}

$gradient = Convert-Metrics (Get-Message 'PhysicsResearch.InverseDesign.SpringChainGradient' 'chain_gradient')
$optimization = Convert-Metrics (Get-Message 'PhysicsResearch.InverseDesign.SpringChainOptimization' 'chain_optimization')
$history = Convert-Metrics (Get-Message 'PhysicsResearch.InverseDesign.SpringChainOptimization' 'chain_loss_history')
$boundary = Convert-Metrics (Get-Message 'PhysicsResearch.InverseDesign.SpringChainTerminalOnlyBoundary' 'chain_terminal_boundary')
$costMessages = @(Get-Test 'PhysicsResearch.InverseDesign.SpringChainCostScaling').entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like 'chain_cost*' }
$cost = @($costMessages | ForEach-Object { Convert-Metrics $_ })
$losses = @($history.values -split ',' | ForEach-Object { [double]$_ })
$curve = for ($index = 0; $index -lt $losses.Count; ++$index) { [pscustomobject]@{ iteration = $index; loss = $losses[$index] } }
$curvePath = Join-Path $root 'spring_chain_loss_curve.csv'
$curve | Export-Csv -LiteralPath $curvePath -NoTypeInformation -Encoding UTF8
$strictlyDecreasing = $true
for ($index = 1; $index -lt $losses.Count; ++$index) { if ($losses[$index] -ge $losses[$index - 1]) { $strictlyDecreasing = $false; break } }

$firstFailedTest = @($firstFailure.tests | Where-Object fullTestPath -eq 'PhysicsResearch.InverseDesign.SpringChainOptimization')[0]
$firstFailureMessage = @($firstFailedTest.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like 'chain_optimization*' })[0]
$firstFailureMetrics = Convert-Metrics $firstFailureMessage

$springOptimization = Convert-Metrics (Get-Message 'PhysicsResearch.InverseDesign.SpringOptimization' 'M5_INVERSE_OPTIMIZATION')
$ballisticOptimization = Convert-Metrics (Get-Message 'PhysicsResearch.InverseDesign.BallisticOptimization' 'M5_BALLISTIC_OPTIMIZATION')
$optimizationCosts = @(
    [pscustomobject]@{ benchmark = 'one_dof_damped_spring'; iterations = [int]$springOptimization.iterations; elapsed_ms = [double]$springOptimization.optimization_ms },
    [pscustomobject]@{ benchmark = 'two_dimensional_ballistic'; iterations = [int]$ballisticOptimization.iterations; elapsed_ms = [double]$ballisticOptimization.optimization_ms },
    [pscustomobject]@{ benchmark = 'six_dof_segmented_spring_chain'; iterations = [int]$optimization.iterations; elapsed_ms = [double]$optimization.optimization_ms }
)

$checks = [ordered]@{
    inverse_design_regression_10_of_10 = ([int]$automation.succeeded -eq 10 -and [int]$automation.failed -eq 0 -and [int]$automation.notRun -eq 0)
    both_tangents_match_centered_difference = ([double]$gradient.proximal_rel_error -lt 2.0e-5 -and [double]$gradient.distal_rel_error -lt 2.0e-5)
    full_trajectory_reduces_loss_by_six_orders = ([double]$optimization.final_loss -lt [double]$optimization.initial_loss * 1.0e-6)
    both_segment_stiffnesses_are_recovered = ([double]$optimization.parameter_l1 -lt 1.0)
    loss_curve_is_complete_and_strictly_decreasing = ($losses.Count -eq [int]$optimization.samples -and $losses.Count -gt 2 -and $strictlyDecreasing)
    terminal_only_target_fits_but_is_spatially_non_identifiable = ([double]$boundary.final_loss -lt [double]$boundary.initial_loss * 1.0e-6 -and [double]$boundary.parameter_l1 -gt 5.0)
    cost_distribution_has_three_scales = ($cost.Count -eq 3 -and @($cost | Where-Object { [double]$_.mean_ms -le 0.0 -or [double]$_.p95_ms -le 0.0 }).Count -eq 0)
    all_three_benchmarks_report_optimizer_elapsed_time = ($optimizationCosts.Count -eq 3 -and @($optimizationCosts | Where-Object elapsed_ms -le 0.0).Count -eq 0)
    first_forward_state_failure_is_preserved = ($firstFailedTest.state -eq 'Fail' -and [double]$firstFailureMetrics.parameter_l1 -gt 60.0)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = $(if ($failed.Count -eq 0) { 'passed' } else { 'failed' })
    interpretation = 'A fixed-end six-mass chain differentiates two spatial stiffness segments through the exact semi-implicit trajectory. Full tip position/velocity history identifies both segments; terminal position alone admits a different compliance distribution. Timings are observed CPU costs on this machine, not cross-platform speed claims.'
    checks = $checks
    gradient = $gradient
    optimization = $optimization
    terminal_only_boundary = $boundary
    evaluation_cost_distribution = $cost
    optimizer_elapsed_time = $optimizationCosts
    preserved_first_failure = [ordered]@{
        cause = 'Force arrays were resized with SetNumZeroed, which only cleared newly added elements; force and tangent force accumulated between steps.'
        failed_test_state = $firstFailedTest.state
        metrics = $firstFailureMetrics
        evidence = 'Docs/Validation/M5/AutomationFirstFailure/index.json'
    }
    loss_curve_csv = 'Docs/Validation/M5/spring_chain_loss_curve.csv'
    failed_checks = $failed
}
$reportPath = Join-Path $root 'spring_chain_foundation_validation.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
$report | ConvertTo-Json -Depth 8
if ($failed.Count -gt 0) { exit 1 }
