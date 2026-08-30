# GitHub 发布检查表

本文记录公开源码仓的发布门。目标是让招聘方能快速理解个人贡献、查看可复算证据，并让拥有合法 Epic/Fab 资产的开发者恢复工程；不重新分发第三方原资产。

## 当前已满足

- 公开仓默认分支为 `main`；当前覆盖层不依赖 Git LFS 配额。
- 首发浅克隆包含 783 个文件；最大文件约 1.03 MiB，没有接近 GitHub 100 MiB 上限的已跟踪单文件。
- 当前小型 `.uasset`、`.umap` 直接由普通 Git 管理，克隆后均为真实 UE 文件而非 LFS 指针。
- `Binaries`、`Intermediate`、`Saved`、`DerivedDataCache` 和本地打包目录不进入仓库。
- Lyra 原始源码/内容、Assassin、Dragon 和 Dragon 派生封闭外壳不进入公开仓。
- README 已提供中文项目定位、五个 Case、截图、验收入口、恢复步骤和归因边界。
- 五个 Case 均已冻结；M7 中文统一入口与 Hub→Case05 路由已通过机器验证。
- Win64 Development Build/Cook/Stage/Pak/IoStore/Archive 已成功；打包版默认 Hub 截图和 Hub→Case05 路由机器门 10/10。
- Case01 作品级总门 17/17、统一 Hub 9/9、项目级发布准备总门 15/15，机器报告均已纳入仓库。
- 当前源配置已禁用 Android File Server 并移除连接 token。
- 公开仓库已创建并上传：`https://github.com/Ubik42/ue5-siggraph-physics-lab`；默认分支 `main`，七个作品集 Topics 已设置。
- GitHub 首发使用当前验收状态的干净根提交，本机 101 次开发历史保持不变，旧本地 token 未进入公网。
- 全新浅克隆验证已通过：783 个文件、UE 资产无 LFS 指针、最大文件约 1.03 MiB、README 与 Hub 截图存在、项目总门仍为 passed、配置中无非空 AFS token。

## 首次推送后已完成

- 已采用干净公开快照，不推送含旧 Android File Server 本地 token 的开发历史；本机历史保留用于个人追溯。
- 复核证据日志中的本机绝对路径是否接受公开；它们用于原始运行可追溯，不是凭证。
- 当前采用求职源码展示边界：不添加开源许可证，个人原创内容保留所有权利；若未来希望允许复用，再单独切换 MIT/Apache-2.0 等许可证。第三方内容不随个人许可证重新授权。
- GitHub 远端、简介、Topics 和封面图均已就位。由于账号 LFS 配额已满、当前二进制文件最大仅约 447 KiB，公开覆盖层改为普通 Git 并成功推送。
- 已在全新目录完成一次浅克隆，确认无 LFS 指针、忽略规则有效、README 与图片存在；机器报告见 `Docs/Validation/M7/github_publication_validation.json`。

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
