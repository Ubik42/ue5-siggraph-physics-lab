# M1 共享实验内核验证

## 已实现

- `Stopped / Running / Paused` 明确状态机。
- 开始、暂停、停止、重置和固定步长单步。
- `FRandomStream` 确定性种子，重置后重复序列一致。
- 通用指标样本：名称、数值、单位、步号和模拟时间。
- 有限容量指标缓存，避免长期实验无界增长。
- CSV/JSON 导出，文件名清洗且固定写入 `Saved/PhysicsResearch`。
- 单步墙钟耗时与平均步耗时统计。

## 验证结果

- LyraEditor Win64 Development：通过。
- 自动化：2 成功、0 失败、0 警告。
  - `PhysicsResearch.Configuration.Defaults`
  - `PhysicsResearch.Experiment.LifecycleAndMetrics`
- 导出测试真实创建 JSON，确认存在后只删除该测试拥有的文件。
- 最小地图：`/Game/PhysicsResearch/Maps/L_PhysicsLab_M1`，使用原生 `APhysicsLabM1Actor` 和 `APhysicsLabM1GameMode`。
- 真实运行：180 个固定步、3.0 秒模拟时间、360 条指标样本，后端标记为 `ChaosBaseline`。
- 指标：`OrbitRadiusError` 与 `EnergyProxy`，步号完整覆盖 1–180，所有数值均为有限数。
- 运行产物：`Saved/PhysicsResearch/M1_Runtime.json` 与 `M1_Runtime.csv`。
- GUI 证据：`Screenshots/M1_Runtime.png`，1280×720；可见中文 HUD、暂停状态、步数、耗时、控制说明与 Debug Draw。
- 机器可读校验：`runtime_validation.json`，九项检查全部通过。

## 环境提示

MSVC 14.44 可以成功构建，但 UE 5.4 仍提示首选 14.38。当前不阻塞；如出现 ABI/工具链问题再处理，不主动更换项目引擎。

## 可重复验证

运行 `Scripts/Validation/validate_m1_outputs.ps1` 可复核 JSON、CSV 与截图。该脚本只读取运行产物并更新 M1 验证报告，不会修改 Marketplace/Fab 原资产。

## 结论

M1 已达到退出条件。此场景是实验框架与数据链路的工程验证，不宣称复现任何 SIGGRAPH 论文；论文算法从五个独立 Game Feature Case 开始实现。
