param([string]$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")))
$ErrorActionPreference = "Stop"
$evidence = Join-Path $ProjectRoot "Docs\Validation\M2"
$runtimePath = Join-Path $evidence "gpu_explicit_assassin_interactive_runtime.json"
$comparisonPath = Join-Path $evidence "gpu_explicit_assassin_interactive_comparison.csv"
$timingPath = Join-Path $evidence "gpu_explicit_assassin_interactive_gpu_timing.csv"
$screenshotPath = Join-Path $evidence "Screenshots\M2_ExplicitAssassinInteractive.png"
$reportPath = Join-Path $evidence "gpu_explicit_assassin_interactive_validation.json"
$runtime = Get-Content -LiteralPath $runtimePath -Raw -Encoding UTF8 | ConvertFrom-Json
$comparison = Import-Csv -LiteralPath $comparisonPath -Encoding UTF8
$timing = Import-Csv -LiteralPath $timingPath -Encoding UTF8
$screenshot = if (Test-Path -LiteralPath $screenshotPath -PathType Leaf) { Get-Item -LiteralPath $screenshotPath } else { $null }
$checks = [ordered]@{
    runtime_passed = ($runtime.result -eq "passed")
    interaction_script_enabled = ($runtime.interaction_script -eq $true)
    one_real_reset = ([int]$runtime.reset_count -eq 1)
    pause_single_step_resume = ([int]$runtime.pause_toggle_count -eq 2 -and [int]$runtime.single_step_count -eq 1)
    driver_clock_frozen_with_experiment = ([int]$runtime.driver_freeze_count -eq 2 -and [int]$runtime.driver_resume_count -eq 2 -and [int]$runtime.manual_driver_step_count -eq 1 -and $runtime.driver_pause_scope -like "experiment clock*")
    backend_display_cycle_restored = ([int]$runtime.backend_switch_count -eq 3 -and $runtime.display_mode -eq "both")
    debug_cycle_restored = ([int]$runtime.debug_toggle_count -eq 2 -and $runtime.debug_draw_enabled -eq $true)
    reset_did_not_leak_gpu_state = ([int]$runtime.steps_executed -eq 300 -and [int]$runtime.target_applications -eq 300 -and [int]$runtime.collision_applications -eq 300 -and [int]$runtime.gpu_timing_samples -eq 300 -and [int]$runtime.gpu_timing_samples_skipped -eq 0)
    standard_physics_profile_restored = ($runtime.failure_profile -eq "none" -and [Math]::Abs([double]$runtime.ndb_base_weight - 10.0) -lt 0.000001 -and $runtime.complete_ccd_enabled -eq $true)
    nominal_penetration_gate = ([double]$runtime.maximum_collision_penetration_cm -lt 0.001)
    paired_curve_complete = (@($comparison).Count -eq 75 -and [int]$comparison[0].step -eq 4 -and [int]$comparison[-1].step -eq 300)
    gpu_curve_complete = (@($timing).Count -eq 300 -and [int]$timing[0].step -eq 1 -and [int]$timing[-1].step -eq 300)
    screenshot_present = ($null -ne $screenshot -and $screenshot.Length -gt 100000)
}
$report = [ordered]@{
    generated_at = (Get-Date).ToString("o")
    result = if ($checks.Values -contains $false) { "failed" } else { "passed" }
    checks = $checks
    evidence = [ordered]@{
        runtime = $runtimePath
        screenshot = $screenshotPath
        controls = "Space 暂停/继续；N 单步；R 重置；B 后端观察切换；D Debug；F 故障档；E 导出"
        reset_count = [int]$runtime.reset_count
        pause_toggle_count = [int]$runtime.pause_toggle_count
        single_step_count = [int]$runtime.single_step_count
        backend_switch_count = [int]$runtime.backend_switch_count
        debug_toggle_count = [int]$runtime.debug_toggle_count
        maximum_penetration_cm = [double]$runtime.maximum_collision_penetration_cm
        scope = "暂停同时冻结共享实验时钟、Assassin 动画和 Chaos Cloth；单步通过下一自然组件 Tick 执行，禁止在 Actor Tick 内重入 Cloth Tick。"
    }
}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($report.result -ne "passed") { throw "Case 01 交互黄金路径验证失败：$reportPath" }
Write-Output "M2_GPU_EXPLICIT_INTERACTIVE_VALIDATION_PASSED report=$reportPath"
