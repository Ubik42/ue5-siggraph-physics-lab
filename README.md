# SIGGRAPH Physics Lab

这是一个基于 Epic Lyra Starter Game 5.4 的 UE 物理研究作品工程，用于承载五个相互隔离的论文复现 Case，并始终保留 Chaos 基线对照。

![五个物理作品的中文统一入口](Docs/Validation/M7/Screenshots/M7_PortfolioHub.png)

## 五个独立作品

| Case | 研究主题 | UE / Chaos 落地点 | 作品级证据 |
| --- | --- | --- | --- |
| 01 | 接触感知 GPU 布料 | UE Clothing 拓扑、Physics Asset 球/胶囊、Chaos Cloth 同输入基线、RDG Compute | [M2 验收](Docs/Validation/M2/README.md) |
| 02 | 多尺度 GPU 软体 | 动态 Dragon 表面、持久 XPBD、低频模态、Chaos/Flesh 版本边界 | [M3 验收](Docs/Validation/M3/README.md) |
| 03 | Flow Map 流体 | Chaos 动态障碍、GPU 闭环流场、预算档位、长序列稳定性 | [M4 验收](Docs/Validation/M4/README.md) |
| 04 | 可微物理与反向设计 | 可微参考模型、参数反演、Chaos Constraint 同参数重放 | [M5 验收](Docs/Validation/M5/README.md) |
| 05 | 物理驱动角色 | Chaos 多刚体、Physics Control、ALS 起身交接、Lyra 服务器权威 | [M6 验收](Docs/Validation/M6/README.md) |

五个 Case 都包含标准配置、可解释失败配置、可复算指标和中文场景；统一入口用数字键 `1–5` 进入正式地图，`H` 进入 Case 05 的 ALS 物理—动画交接场景。算法、UE 集成与第三方资产贡献的边界见[作品归因](Docs/Development/ATTRIBUTION.md)。

完整作品录屏可按[五作品黄金与失败路径](Docs/Validation/M7/五作品黄金与失败路径.md)执行。

## 代表画面

| Case 01：接触 GPU 布料 | Case 02：多尺度 GPU 软体 |
| --- | --- |
| ![Assassin 接触 GPU 布料](Docs/Validation/M2/Screenshots/M2_ExplicitAssassinCloth.png) | ![Dragon GPU 软体与接触](Docs/Validation/M3/Screenshots/M3_DragonGpuSurface_Contact.png) |

| Case 03：Flow Map + Chaos 障碍 | Case 04：反向设计 + Chaos 重放 |
| --- | --- |
| ![Flow Map 与 Live Chaos 障碍](Docs/Validation/M4/Screenshots/Case03_FlowMap_LiveChaos.png) | ![反向设计与 Chaos Constraint 重放](Docs/Validation/M5/Screenshots/Case04_InverseSpring_ChaosReplay.png) |

| Case 05：物理驱动角色 |
| --- |
| ![Chaos 与 Physics Control 角色 A/B](Docs/Validation/M6/Screenshots/Case05_PhysicsControl_Portfolio.png) |

## GitHub 仓库说明

本仓库采用“Lyra 覆盖层”发布方式，只保存个人插件、Case 资产、自动化脚本和验证证据，不重新分发 Lyra、ALS 或 Fab/Marketplace 原资产。因此它不是解压即玩的完整 Lyra 工程，也不会把第三方美术资产计入个人成果。

恢复工程需要：

1. 安装 Unreal Engine `5.4.4`，并从自己的 Epic 账户创建 Lyra Starter Game 5.4 工程。
2. 将本仓内容覆盖到 Lyra 工程根目录，并安装 Git LFS 后拉取 LFS 文件。
3. 从自己的 Epic 账户向工程添加 `Assassin` 与 `Dragon for Boss Monster : Handpainted` 两个资源包。
4. 重新生成工程文件并编译 `LyraEditor Win64 Development`；随后打开 `/Game/PhysicsResearch/Maps/L_PhysicsLab_M1`。

更完整的跟踪范围、排除项和恢复步骤见[源码管理策略](Docs/Development/SOURCE_CONTROL.md)。本仓当前作为求职源码展示，未授予开源复用许可，个人代码保留所有权利；Epic、Lyra、ALS 与 Fab 内容始终服从其各自条款。

## 许可

除非文件中另有说明，本仓个人原创代码、文档与截图均保留所有权利，暂不授予复制、修改、再发布或商业使用许可。仓库不重新授权、不重新分发 Epic Games、Lyra、ALS、Fab/Marketplace 或论文作者的第三方内容；恢复工程时请分别遵守其原始许可与服务条款。

## 引擎与来源

- 实际开发引擎：Unreal Engine 5.4.4（Changelist 35576357）。
- Gameplay、角色、输入、UI、网络与部分场景基座：Epic Lyra Starter Game 5.4。
- ALS 只作为动作、Ragdoll/Get Up、姿态曲线和 Retarget 来源，不覆盖 Lyra 的权威 Gameplay 架构。
- 个人新增运行时模块：`Plugins/PhysicsResearch`。

## PhysicsResearch 模块

共享实验内核现已提供：

- 五类实验枚举：接触布料、多尺度软体、Flow Map 流体、可微反向设计、物理控制角色。
- Chaos 基线与论文复现求解器的统一后端枚举。
- 固定时间步、最大追赶步数、子步和迭代配置。
- `UPhysicsExperimentComponent`：统一实验生命周期、固定步进回调和基础统计。
- 明确的运行/暂停/停止/重置/单步生命周期、确定性随机流与有界指标缓存。
- CSV/JSON 导出、单步墙钟耗时统计、中文 HUD 和 Debug Draw。
- 最小验证地图 `/Game/PhysicsResearch/Maps/L_PhysicsLab_M1`。
- 自动化测试：`PhysicsResearch.Configuration.Defaults` 与 `PhysicsResearch.Experiment.LifecycleAndMetrics`。

UE 5.4 自带的 ChaosFlesh 属于实验插件，当前版本启动时会报告 `ChaosFleshNodes` 的属性初始化错误，因此基础工程不默认启用；进入软体 Case 后再单独启用、隔离并记录该版本问题。

具体论文求解器将在后续 Case 中实现，当前模块不宣称已经复现论文算法。

## 已加入的 Fab Asset Package

以下两个 UE `Asset package` 已经加入工程并通过 UE 5.4 Asset Registry 验证：

1. `Assassin`：位于 `/Game/Assassin`，注册 141 个资产；用于布料/接触 Case 的独立服装或护甲垂片。
2. `Dragon for Boss Monster : Handpainted`：位于 `/Game/FourEvilDragonsHP`，注册 125 个资产；用于软体和 Boss 物理 Case 的低面数有机模型。

代表性的 Skeleton、Skeletal Mesh、Physics Asset、动画、材质和地图均可加载。不要直接修改第三方原资产；个人派生资产进入 `/Game/PhysicsResearch` 或对应 Game Feature 插件。Marketplace/Fab 资产默认不提交到公开源码仓。

## 目录规划

```text
Plugins/PhysicsResearch/       个人物理运行时模块
Content/PhysicsResearch/       五个 Case 的地图、数据与个人资产（后续生成）
Content/Marketplace/           第三方资产的隔离迁移区（不公开提交）
Docs/Validation/               编译、资产与运行验证记录（后续生成）
```

## 当前状态与边界

- 已最小迁移并派生 ALS Front/Back Get Up 到 Lyra Manny；未迁入 ALS Character、AnimBP 或 Camera 框架。
- M1 共享实验框架已完成真实 UE 运行、指标导出、自动化和 GUI 截图验证。
- 最小 M1 地图只验证运行框架，不作为论文复现成果。
- M2 / Case 01 接触与 GPU 布料垂直切片已达到独立作品门槛：Chaos Cloth 基线与研究后端共享同一 Assassin 组件、动画姿态、2011 粒子拓扑和 1 球/16 胶囊输入；研究端已接入 12 维缩减全局步、rank-one 接触系统、GPU NDB、局部 Jacobi 与完整解析 CCD。作品级机器总验收见 `Docs/Validation/M2/case01_acceptance_validation.json`。
- 当前完整 D3D12 SM6 自动化为 32/32；四象限消融已分离证明 NDB 与 CCD 的独立职责，带残差/CCD/拓扑安全门的持久 GPU 残差转发已进入规则网格管线。完整 `3m×3m` rank-one 碰撞 Hessian 现也已迁入 Assassin 任意拓扑：每步从 Physics Asset 分析接触装配、GPU Cholesky 求解并回写，再由 NDB/Jacobi 与 CCD 收口。
- Assassin 300 步现从真实激活 Buffer 导出接触分布：活跃接触对平均 15.96、P95 40、P99/峰值 57；CPU 渲染线程 RDG 构建/提交平均 0.154 ms、P99 0.228 ms。该耗时明确不冒充 GPU kernel 时间。
- 显式拓扑 Case 已建立同一注册 Assassin 组件的 Chaos/研究求解器对照：固定 60 Hz 下采集 300 份 Chaos 快照并与 75 份 GPU 异步读回按源步号配对；12 模态、rank-one、NDB 与完整 CCD 均执行 300 次，研究后端穿透约 `0.0000858 cm`。Rank-one 关闭/开启的 300 步消融分别得到结构误差 `2.738/2.624 cm`、穿透 `0.0000792/0.0000858 cm`，两组均通过原有门槛；该差异是同输入算法响应，不是要求与 Chaos 数值相等。
- Assassin 显式求解现用 8 组非阻塞 `RQT_AbsoluteTime` 查询记录完整固定步 RDG 区间。最新标准配置 300 步无丢样；固定排除前 16 步预热后，284 个稳态样本均值 `4.753 ms`、P95 `5.110 ms`、P99 `5.197 ms`、最大 `5.317 ms`。原始逐步 CSV 与独立复算验证均已提交；该区间包含上传、Compute Pass 和计划的读回复制，不冒充单 kernel 时间。
- 作品 HUD 现把 Chaos/研究动态位移与逐步 GPU 时间画成两张现场曲线，明确标出预热边界、稳态 P95 与未丢弃的启动尖峰。`-PhysicsClothExplicitReplay` 可重置同一固定步输入并哈希 300 步 UE Clothing 蒙皮目标及 Physics Asset 原语；三次独立进程验证得到稳定哈希 `429e63168b51a679`，正确期望通过、故意错误期望准确失败。
- 作品地图提供真实交互：`Space` 协同暂停/继续实验时钟、Assassin 动画和 Chaos Cloth，`N` 在自然 Skeletal Mesh Tick 中安全单步，`R` 重建完整运行状态，`B` 切换实际显示几何来源，`D` 切换调试绘制，`F` 在标准 `NDB k=10` 与可复现弱障碍 `k=0.05` 间切换，`E` 导出 CSV/JSON。自动交互脚本验证 1 次重置、2 次暂停切换、1 次单步、3 次后端切换和 2 次调试切换后仍精确完成 300 步。
- 弱障碍失败配置只改变 NDB 基础权重，保留相同输入哈希、rank-one 与完整 CCD；其最大穿透 `0.048648 cm`，明显超过标准配置 `0.0000858 cm` 和既有 `<0.001 cm` 门，作为可解释的失败场景而非放宽阈值。作品归因见 `Docs/Development/ATTRIBUTION.md`。
- M3 / Case 02 已通过作品级总验收：持久 D3D12 XPBD、两模态低频校正、Dragon 动态表面、玩家交互、标准/弱接触消融和 CPU 真后端切换形成闭环；UE 5.4.4 Chaos Flesh 的插件边界被如实记录。标准接触最大穿透 0，弱配置穿透 64.24 cm，CPU 局部参考在同预算下产生 978.28 cm 预期质量失败；6 张 GUI 截图和统一机器报告已提交。
- M4 / Case 03 与 M5 / Case 04 已分别通过作品级总验收并冻结；机器报告见 `Docs/Validation/M4/case03_acceptance_validation.json` 与 `Docs/Validation/M5/case04_acceptance_validation.json`。
- M6 / Case 05 已通过总验收并冻结：三个标准基准、真实 Chaos 冲量/高度/骨骼耦合、ALS 交接、玩家交互、Lyra 双进程复制、弱网 token 归因、三进程性能和 1600×900 Portfolio 场景均有证据；总门 10/10，DrivenCharacter 16/16。五个 Case 至此全部冻结。
- M7 发行工程门已完成：中文 Hub、数字键 1–5、ALS 交接、跨 Case 黄金/失败路径、GitHub 图集和 Win64 Development Build/Cook/Stage/Pak/IoStore/Archive 全部通过。打包版默认 Hub 与 Hub→Case05 冒烟机器门 10/10，项目级发布准备总门 15/15；当前只剩许可证、远端、旧本地 token 历史策略和全新克隆等 GitHub 发布治理。
- Case 02 位移控制材料基准也已通过：同一 8 cm 压缩边界下，均匀软/硬块总体积比为 0.8021/0.8545，分区块为 0.8338，三组运动学目标误差均为 0。稳定重力参数与高对比材料参数被明确分成两个实验配置，避免用数值不可见的材料编号伪造差异。
