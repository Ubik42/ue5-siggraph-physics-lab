# GitHub 发布检查表

本文记录公开源码仓的发布门。目标是让招聘方能快速理解个人贡献、查看可复算证据，并让拥有合法 Epic/Fab 资产的开发者恢复工程；不重新分发第三方原资产。

## 当前已满足

- 主分支为 `main`，仓库已启用 Git LFS。
- 已跟踪内容约 39 MiB、772 个文件；当前没有超过 GitHub 100 MiB 上限的已跟踪单文件。
- `.uasset`、`.umap` 与常见源素材由 Git LFS 管理。
- `Binaries`、`Intermediate`、`Saved`、`DerivedDataCache` 和本地打包目录不进入仓库。
- Lyra 原始源码/内容、Assassin、Dragon 和 Dragon 派生封闭外壳不进入公开仓。
- README 已提供中文项目定位、五个 Case、截图、验收入口、恢复步骤和归因边界。
- 五个 Case 均已冻结；M7 中文统一入口与 Hub→Case05 路由已通过机器验证。
- Win64 Development Build/Cook/Stage/Pak/IoStore/Archive 已成功；打包版默认 Hub 截图和 Hub→Case05 路由机器门 10/10。
- Case01 作品级总门 17/17、统一 Hub 9/9、项目级发布准备总门 15/15，机器报告均已纳入仓库。
- 当前源配置已禁用 Android File Server 并移除连接 token。

## 首次推送前必须完成

- 决定如何处理旧提交中已经出现过的 Android File Server 本地 token：保留完整开发历史，或在首次推送前明确授权一次历史净化。当前分支已不再包含该值。
- 复核证据日志中的本机绝对路径是否接受公开；它们用于原始运行可追溯，不是凭证。
- 当前采用求职源码展示边界：不添加开源许可证，个人原创内容保留所有权利；若未来希望允许复用，再单独切换 MIT/Apache-2.0 等许可证。第三方内容不随个人许可证重新授权。
- 创建 GitHub 远端并设置仓库简介、Topics、封面图；推送前执行 `git lfs push --all <remote>` 或正常带 LFS 的首次推送。
- 在全新目录做一次浅克隆，确认 LFS 指针可拉取、忽略规则有效、README 链接与图片均可显示。

## 建议的 GitHub 页面信息

- 仓库名：`ue5-siggraph-physics-lab`
- 简介：`基于 UE5.4 / Lyra / Chaos 的五个 SIGGRAPH 物理研究复现作品：GPU 布料、软体、Flow Map、反向设计与物理驱动角色。`
- Topics：`unreal-engine-5`、`chaos-physics`、`siggraph`、`gpu-compute`、`physics-simulation`、`lyra`、`portfolio`
- 默认分支：`main`
- Release：源码仓与可运行构建分开发布；任何包含 Fab/Marketplace 原资产的构建都必须先核实再分发权限。

## 不应上传

- Epic Lyra、ALS 或 Fab/Marketplace 的原始资产包。
- `Builds/` 下的本地发行包，除非已经逐项核实其中第三方内容的再分发权限。
- Unreal 生成目录、崩溃转储、个人日志、IDE 状态和本机缓存。
- 无法解释来源或许可的模型、贴图、音频、字体和视频。
