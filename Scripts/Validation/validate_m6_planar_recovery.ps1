param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))

$ErrorActionPreference = 'Stop'
$root = Join-Path $ProjectRoot 'Docs\Validation\M6'
$automation = Get-Content -LiteralPath (Join-Path $root 'Automation\index.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$contactFailureAutomation = Get-Content -LiteralPath (Join-Path $root 'AutomationContactFailure\index.json') -Raw -Encoding UTF8 | ConvertFrom-Json

function Get-Test([string]$Path) {
    return @($automation.tests | Where-Object fullTestPath -eq $Path)[0]
}

function Convert-Metrics([string]$Message) {
    $values = [ordered]@{}
    foreach ($match in [regex]::Matches($Message, '(?<key>[a-z][a-z0-9_]*?)=(?<value>[^\s]+)')) {
        $values[$match.Groups['key'].Value] = $match.Groups['value'].Value
    }
    return [pscustomobject]$values
}

function Get-Metrics([string]$Path, [string]$Prefix) {
    $message = @((Get-Test $Path).entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like "$Prefix*" })[0]
    return Convert-Metrics $message
}

$push = Get-Metrics 'PhysicsResearch.DrivenCharacter.PlanarPushRejection' 'M6_PLANAR_PUSH'
$recovery = Get-Metrics 'PhysicsResearch.DrivenCharacter.PlanarFallAndRecovery' 'M6_PLANAR_RECOVERY'
$symmetry = Get-Metrics 'PhysicsResearch.DrivenCharacter.PlanarFrontBackSymmetry' 'M6_PLANAR_SYMMETRY'
$failure = Get-Metrics 'PhysicsResearch.DrivenCharacter.PlanarTorqueBudgetFailure' 'M6_PLANAR_TORQUE_FAILURE'
$contactFailureTest = @($contactFailureAutomation.tests | Where-Object fullTestPath -eq 'PhysicsResearch.DrivenCharacter.PlanarFallAndRecovery')[0]
$contactFailureMetrics = Convert-Metrics @($contactFailureTest.entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like 'M6_PLANAR_RECOVERY*' })[0]
$sampleMessages = @((Get-Test 'PhysicsResearch.DrivenCharacter.PlanarFallAndRecovery').entries | ForEach-Object { $_.event.message } | Where-Object { $_ -like 'M6_PLANAR_SAMPLE*' })
$samples = @($sampleMessages | ForEach-Object { Convert-Metrics $_ })

$trajectory = @($samples | ForEach-Object {
    [pscustomobject]@{
        step = [int]$_.step
        time_s = [double]$_.time_s
        angle_deg = [double]$_.angle_deg
        angular_velocity_deg_s = [double]$_.angular_velocity_deg_s
        target_deg = [double]$_.target_deg
        torque_nm = [double]$_.torque_nm
        support_margin_m = [double]$_.support_margin_m
        phase = [string]$_.phase
    }
})
$trajectory | Export-Csv -LiteralPath (Join-Path $root 'planar_recovery_trajectory.csv') -NoTypeInformation -Encoding UTF8

$monotonic = $true
for ($index = 1; $index -lt $trajectory.Count; ++$index) {
    if ($trajectory[$index].step -le $trajectory[$index - 1].step -or $trajectory[$index].time_s -le $trajectory[$index - 1].time_s) { $monotonic = $false; break }
}
$phases = @($trajectory.phase | Select-Object -Unique)
$nonFinite = @($trajectory | Where-Object {
    -not [double]::IsFinite($_.angle_deg) -or -not [double]::IsFinite($_.angular_velocity_deg_s) -or -not [double]::IsFinite($_.torque_nm)
})

$checks = [ordered]@{
    driven_character_regression_4_of_4 = ([int]$automation.succeeded -eq 4 -and [int]$automation.failed -eq 0 -and [int]$automation.notRun -eq 0)
    moderate_push_is_rejected_without_fall = ($push.fell -eq 'false' -and $push.recovered -eq 'false' -and [double]$push.max_angle_deg -lt 10.0 -and [double]$push.final_error_deg -lt 0.25)
    large_push_falls_and_recovers = ($recovery.fell -eq 'true' -and $recovery.recovered -eq 'true' -and $recovery.failed -eq 'false' -and [double]$recovery.recovery_time_s -gt 0.0 -and [double]$recovery.recovery_time_s -lt 5.5)
    recovery_respects_torque_and_pose_gates = ([double]$recovery.peak_torque_nm -le 1250.0 -and [double]$recovery.final_error_deg -lt 3.0 -and [double]$recovery.work_j -gt 0.0)
    inelastic_ground_has_one_persistent_impact = ([int]$recovery.impacts -eq 1)
    front_and_back_branches_are_symmetric = ($symmetry.front_recovered -eq 'true' -and $symmetry.back_recovered -eq 'true' -and $symmetry.front_orientation -eq 'front' -and $symmetry.back_orientation -eq 'back' -and [double]$symmetry.recovery_time_difference_s -lt (1.0 / 480.0))
    low_torque_is_an_expected_failure = ($failure.fell -eq 'true' -and $failure.recovered -eq 'false' -and $failure.failed -eq 'true' -and [double]$failure.peak_torque_nm -le [double]$failure.torque_limit_nm -and [double]$failure.final_error_deg -gt 45.0)
    sampled_trajectory_is_finite_and_monotonic = ($trajectory.Count -gt 100 -and $nonFinite.Count -eq 0 -and $monotonic)
    all_recovery_phases_are_observed = (@('Balanced','Falling','Settling','Recovering','Recovered' | Where-Object { $_ -notin $phases }).Count -eq 0)
    restitution_contract_failure_is_preserved = ($contactFailureTest.state -eq 'Fail' -and [int]$contactFailureMetrics.impacts -eq 2)
}
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
$report = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    result = $(if ($failed.Count -eq 0) { 'passed' } else { 'failed' })
    interpretation = 'The deterministic planar character separates moderate push rejection, fall-settle-get-up recovery, front/back branch symmetry, and an underpowered torque failure. This is a control-contract truth model, not yet a full skeletal Chaos character.'
    units = [ordered]@{ angle = 'degrees'; angular_velocity = 'degrees/second'; torque = 'newton-meter'; angular_impulse = 'newton-meter-second'; support_margin = 'meter'; work = 'joule' }
    checks = $checks
    moderate_push = $push
    fall_and_recovery = $recovery
    front_back_symmetry = $symmetry
    torque_budget_failure = $failure
    preserved_contact_contract_failure = [ordered]@{
        cause = 'A restitution of 0.04 produced a second legitimate bounce, contradicting the intended single stable landing episode for the get-up truth model. The formal foundation uses an inelastic ground; elastic-drop behavior remains a separate future benchmark.'
        failed_test_state = $contactFailureTest.state
        metrics = $contactFailureMetrics
        evidence = 'Docs/Validation/M6/AutomationContactFailure/index.json'
    }
    sampled_trajectory = [ordered]@{ rows = $trajectory.Count; phases = $phases; csv = 'Docs/Validation/M6/planar_recovery_trajectory.csv' }
    failed_checks = $failed
}
$report | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $root 'planar_recovery_foundation_validation.json') -Encoding UTF8
$report | ConvertTo-Json -Depth 7
if ($failed.Count -gt 0) { exit 1 }
