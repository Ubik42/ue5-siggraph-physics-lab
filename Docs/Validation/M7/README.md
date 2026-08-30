# M7 / 统一交付验证

## 中文作品总览

统一入口复用 `/Game/PhysicsResearch/Maps/L_PhysicsLab_M1`，通过 `-PhysicsLabPortfolioHub` 切换为作品总览，不修改五个已冻结 Case。数字键 `1–5` 分别打开布料、软体、Flow Map、反向设计和物理角色的正式地图；`H` 进入 Case05 的 ALS 交接子场景。

五个 Case 的统一录屏叙事、黄金路径、单变量失败与 Chaos 归因见 `五作品黄金与失败路径.md`。

- 1600×900 D3D12 总览截图：`Screenshots/M7_PortfolioHub.png`。
- 五个目标地图资产均在对应 Game Feature 中存在，五份 acceptance/completion 报告均为 passed。
- 端到端路由已从 Hub 自动选择 Case05，日志确认 `M7_HUB_OPEN`、目标地图 Load Complete，随后目标 Actor 完成 360 帧冻结冲量协议并正常退出。
- 机器门：`portfolio_hub_validation.json` 9/9。

## Win64 发行工程门

- UAT `BuildCookRun` 已完成 Win64 Development Build/Cook/Stage/Pak/IoStore/Archive：4053/4053 包、33144/33144 Shader、ExitCode 0，总耗时 2254.74 秒。
- 归档目录：本地 `Builds/M7_Win64`，共 126 个文件、约 1648.77 MiB；项目 IoStore 为 4 组 `.utoc/.ucas`，数据 561711136 bytes。该目录已被 Git 忽略。
- 打包版默认启动中文 Hub，生成 `Screenshots/M7_Packaged_PortfolioHub.png`（1600×900、226243 bytes），随后正常退出。
- 第二个打包进程从 Hub 自动选择 Case05；日志确认目标地图 Load Complete，Case05 完成 360 帧自动运行后正常退出。
- 机器门：`packaged_build_validation.json` 10/10；复核脚本为 `Scripts/Validation/validate_m7_packaged_build.ps1`。
- 两份打包冒烟日志保留为 `packaged_hub_smoke.log` 和 `packaged_route_case05.log`；全量 UAT 控制台不进入仓库，关键计数、容器大小和退出状态由机器报告固化。
- 项目级发布准备总验收：`project_completion_validation.json` 15/15；统一聚合 M1、五份 Case 总验收、Hub、Win64 包、地图/Cook、截图、中文文档、第三方资产边界与当前配置安全。

核心开发和本地发行工程门已经完成。剩余是 GitHub 发布治理：仓库可见性与许可证选择、远端创建、旧本地 AFS token 的历史策略和全新克隆恢复验证。发行包包含本机合法安装的第三方依赖，只作本地验收，不直接公开分发。
