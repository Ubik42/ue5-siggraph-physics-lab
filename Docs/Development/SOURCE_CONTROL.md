# 源码管理策略

本仓是建立在 Epic Lyra Starter Game 5.4 上的个人覆盖层，不重复提交 Lyra/Fab 原始大体积内容。

## 跟踪内容

- `SIGGRAPHPhysicsLab.uproject`、README、Config。
- `Plugins/PhysicsResearch`。
- 五个 `Plugins/GameFeatures/Physics*` Case。
- `Content/PhysicsResearch` 中的个人共享资产。
- Docs、Scripts、自动化报告和个人截图。

## 不跟踪内容

- Lyra 原始 Source、Build、Platforms、Content 与基础插件。
- `/Game/Assassin`、`/Game/FourEvilDragonsHP`。
- Binaries、Intermediate、Saved、DerivedDataCache 与运行日志。

## 恢复方式

1. 准备与本工程一致的 Lyra Starter Game 5.4 基座。
2. 应用本仓覆盖层。
3. 通过 Epic Games Launcher 将 Assassin 与 Dragon 两个 Asset Package 添加到工程。
4. 使用 UE 5.4.4 编译并运行项目内验证脚本。

需要发布可运行构建时，通过 Cook/Stage/Package 形成交付物；源码仓不承担第三方原资产再分发。

## GitHub 首次发布

- 执行 `git lfs install`，确认 `git lfs ls-files` 能列出个人 `.uasset/.umap`。
- 不把 `Builds/`、本地 Cook 产物或任何 Fab 原资产加入 Git；可运行构建是否能公开分发需要另行核实资产许可。
- 推送前完成敏感配置、绝对路径、单文件大小和 README 链接检查。
- 在新目录克隆一次并按上述恢复流程验证，避免本机未跟踪资产掩盖缺失依赖。
- 详细发布门见 `Docs/Development/GITHUB_RELEASE_CHECKLIST.md`。
