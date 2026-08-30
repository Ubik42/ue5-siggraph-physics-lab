# 作品归因与资产边界

本文用于作品集、README、录屏和面试时明确区分 Epic/第三方内容、Unreal Engine / Chaos 能力与个人实现。仓库中的第三方资产目录不等于获得再分发许可；公开版本应继续排除 Marketplace/Fab 原资产。

## Epic 与第三方提供

- **Unreal Engine 5.4.4 / Chaos**：引擎运行时、Skeletal Mesh 与动画 Tick、Chaos Cloth 基线、Physics Asset 球/胶囊数据、Clothing Asset 物理网格与蒙皮工具、Render Dependency Graph、RHI Compute 与 GPU timestamp 查询。
- **Lyra Starter Game 5.4**：工程基座、Gameplay 架构、输入/UI/网络范式与部分场景资产。本 Case 没有把 Lyra 自带能力包装成个人物理算法。
- **Assassin Fab / Marketplace Asset Package**：`/Game/Assassin` 中的角色 Skeletal Mesh、Skeleton、动画、Physics Asset、Clothing Asset、材质与贴图。当前本地包用于验证和录屏，保持只读；公开源码仓与可下载构建不得擅自包含原资产。
- **Dragon for Boss Monster : Handpainted**：`/Game/FourEvilDragonsHP`，预留给 Case 02/05 的有机模型与 Boss 演示；Case 01 未使用其内容。

具体商店作者、产品页与许可证文本应以用户 Epic 账户中的已购条目和安装时附带元数据为准；本工程不在无法核实的情况下杜撰作者名或扩大许可范围。

## 个人实现

- `PhysicsResearch` 共享实验协议：固定步、生命周期、确定性随机流、指标缓存、CSV/JSON 导出、中文 HUD、Debug Draw 与自动化合同。
- `PhysicsContactCloth` Case 架构与隔离地图，包括 Chaos/研究双后端编排、同源步配对、运行状态重置、交互控制、失败配置和截图捕获。
- Assassin 两份 Clothing Asset 的求解器无关拓扑提取与数据校验：2011 粒子、3705 三角形、5714 条唯一边、逆质量、MaxDistance、资产区间和骨骼映射合同。
- UE Clothing 蒙皮目标与 Physics Asset 球/胶囊的逐固定步适配、版本化快照、相同输入哈希和确定性重放门。
- 研究求解器与验证：CPU 参考、任意拓扑 GPU Jacobi、图低频模态、缩减 SPD/rank-one 系统、NDB 连续激活、残差复用安全门、解析球/胶囊完整 CCD、异步回读和 D3D12 自动化真值/消融。
- 端到端 GPU 固定步查询、逐步原始数据、独立统计复算、现场曲线、玩家交互黄金路径与弱障碍可解释失败实验。

## 演示时推荐表述

“角色、动画和服装资产来自 Assassin 资源包，Chaos Cloth 是 UE 基线；我实现的是同一 UE Clothing 拓扑和 Physics Asset 输入上的研究 GPU 求解器、Chaos 对照编排、固定步/重放/指标协议、算法真值与消融，以及可操作的失败场景。作品不声称重写了 Unreal 的动画、蒙皮或 Chaos Cloth，也不声称与论文离线实现或完整 Chaos Cloth 功能等价。”

## Case 04：可微物理与反向设计

- **Epic / UE 提供**：UE 5.4.4 运行时、Chaos 刚体与 `UPhysicsConstraintComponent`、Lyra/Game Feature 工程基座、基础几何与渲染。
- **研究背景提供**：SIGGRAPH 相关可微动力学、反向设计、PDE/形状优化论文中的问题意识；本作品未复制论文作者实现，也不声称完整复现单篇论文。
- **个人实现**：1-DOF 阻尼弹簧、2D 阻力弹道和 6-DOF 分段弹簧链的确定性离散前向解与切线；中心有限差分真值门；受界回溯优化；三类不可辨识边界；研究参数到真实 Chaos Constraint 的桥接、模型偏差度量、交互、导出、自动化与中文 HUD。
- **未使用资产**：Case04 最终场景未使用 Assassin 或 Dragon 美术包，避免把第三方角色表现误归因为可微物理成果。

演示时推荐表述：“Chaos 负责 UE 内真实刚体约束重放，我实现可微参考模型、参数反演和两者的同参数对照。三组基准证明离散导数、参数尺度和多体空间耦合；失败场景证明低目标误差不等于参数可信。作品不声称 Chaos 本身可微，也不声称实现了论文的伴随 PDE 或接触导数。”
