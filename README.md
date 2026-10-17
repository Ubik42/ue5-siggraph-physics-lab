# SIGGRAPH Physics Lab

本项目包含基于 UE5.4.4、Lyra 5.4 和 Chaos 物理引擎的五项图形与物理研究，涵盖 GPU 布料、多尺度 GPU 软体、Flow Map 流体、可微参考模型的参数反演以及物理角色系统。五个案例均提供图文记录，其中物理角色案例包含本次测试的对照视频，完整内容展示于 [个人作品主页](https://ubik42.github.io/?work=siggraph-physics-lab)。

我实现了资产输入适配、CPU 参考与 GPU 研究求解器、基于 RDG 与 RHI 的调度管线、异步回读、固定步采样逻辑，以及基准对照与失败分析场景。各项案例的复现范围与运行约束互不相同。

![SIGGRAPH Physics Lab 五个 Case 入口](Docs/Validation/M7/Screenshots/M7_PortfolioHub.png)

## 五个 Case

| Case | 研究方向 | 当前实现 |
| --- | --- | --- |
| [01 · 接触感知 GPU 布料](Docs/Cases/Case01_接触感知GPU布料.md) | GPU 布料、接触障碍、低频误差 | 用 Assassin Clothing 拓扑和 Physics Asset 驱动研究 GPU 求解器，并与 Chaos Cloth 做同输入对照 |
| [02 · 多尺度 GPU 软体](Docs/Cases/Case02_多尺度GPU软体.md) | XPBD、多尺度求解、软体接触 | 四面体 XPBD、低频全局校正、Dragon 表面绑定与接触实验 |
| [03 · Particle Flow Map 流体](Docs/Cases/Case03_ParticleFlowMap流体.md) | 欧拉—拉格朗日流体、长时平流 | 2D MAC 网格、粒子 Flow Map、压力投影与 Chaos 动态障碍 |
| [04 · 可微物理与反向设计](Docs/Cases/Case04_可微物理与反向设计.md) | 参数反演、离散导数、可辨识性 | 前向切线、有限差分验证、受界优化与 Chaos Constraint 重放 |
| [05 · 物理驱动角色](Docs/Cases/Case05_物理驱动角色.md) | 物理控制、跌倒恢复、动画交接 | Chaos 多刚体、Physics Control、ALS 起身与 Lyra 服务器权威 |

## 运行画面

### [Case 01 · 接触感知 GPU 布料](Docs/Cases/Case01_接触感知GPU布料.md)

![Assassin Clothing 与研究 GPU 布料同输入运行](Docs/Validation/M2/Screenshots/M2_ExplicitAssassinCloth.png)

### [Case 02 · 多尺度 GPU 软体](Docs/Cases/Case02_多尺度GPU软体.md)

![Dragon 表面由四面体 GPU Cage 驱动并参与地面接触](Docs/Validation/M3/Screenshots/M3_DragonGpuSurface_Contact.png)

### [Case 03 · Particle Flow Map 流体](Docs/Cases/Case03_ParticleFlowMap流体.md)

![Particle Flow Map、MAC 压力势与 Live Chaos 运动边界](Docs/Validation/M4/Screenshots/Case03_FlowMap_LiveChaos.png)

### [Case 04 · 可微物理与反向设计](Docs/Cases/Case04_可微物理与反向设计.md)

![可微研究模型反演参数并交给 Chaos Constraint 重放](Docs/Validation/M5/Screenshots/Case04_InverseSpring_ChaosReplay.png)

### [Case 05 · 物理驱动角色](Docs/Cases/Case05_物理驱动角色.md)

![被动 Chaos 与 Physics Control 主动控制的同冲量对照](Docs/Validation/M6/Screenshots/Case05_PhysicsControl_Portfolio.png)

**[播放同冲量实机视频](https://ubik42.github.io/?work=siggraph-physics-lab)** · [源码与复现步骤](Docs/Development/CASE05_REPRODUCTION.md) · [本次数据与指纹](Docs/Validation/20261017/README.md)

在物理角色案例中，我实现了物理与动画状态判据、控制权交接管线以及服务器复制逻辑。Chaos 负责刚体模拟，Physics Control 负责姿态控制，ALS V4 提供起身动作资产，Lyra 提供宿主环境与 Manny 角色资产。

本次测试仅重测 Manny 在同冲量场景下的姿态响应。两路对照均配置相同的 22 刚体拓扑。被动路由 Chaos 响应；主动路包含 21 个父空间控制与 1 个固定 pelvis 世界空间控制，力与力矩均未做限幅（MaxForce/MaxTorque=0）；系统在 0.5 秒时向 spine_03 施加 (0, 5200, 0) kg·cm/s 冲量。

测试在 60Hz 采样下记录 6 秒共计 360 条数据样本：

| 测试路 | 最终姿态角误差 (deg) | Pelvis 最终位移 (cm) |
| :--- | :--- | :--- |
| 被动 Chaos | 107.604 | 152.214 |
| Physics Control 主动路 | 27.144 | 2.996 |

实验范围严格限定于该指定目标约束下的姿态追踪，自主平衡、有限力矩表现与性能收益均未由此证明。2026-10-17 测得的终态数据与 8 月 30 日历史记录在 1e-6 容差内保持一致。

7.7 秒展示视频由 0.3 至 6 秒实机运行片段与 2 秒末帧停留拼接而成，录制引入的墙钟开销使其不能作为性能评估依据。起身与网络同步属于历史验证资料，连续起身录像尚未补齐。本案例未训练任何神经策略。

## 项目环境

- Unreal Engine 5.4.4
- Lyra Starter Game
- Chaos Cloth / Chaos Rigid Body / Physics Control
- UE C++、RDG / RHI Compute Shader、Game Feature Plugin
- Assassin 与 Dragon 仅作为本地 Fab 资产输入，仓库不重新分发原始商业资产

公开的 214 份自有源码、Shader、Build.cs、插件描述与配置文件与实际开发工程一致，以 Lyra 覆盖层形式提供。运行工程必须自行准备合法的 UE5.4.4 与 Lyra 5.4 宿主环境，新机器下的完整恢复流程未经全量验证。

Assassin 与 Dragon 资产仅作为 Case 01 与 Case 02 的本地输入，与物理角色同冲量场景无关。ALS 起身流程包含额外依赖，恢复时需补齐对应资源。测试记录仅反映指定输入与约束下的结果，不包含未经测试的工况。

离线核对：`python Scripts/Validation/check_portfolio_snapshot.py`。该脚本只检查保存的源码和数据；宿主检查、编译与运行参数见[复现说明](Docs/Development/CASE05_REPRODUCTION.md)。

## 论文与资产归因

每个 Case 的文章会列出论文来源、复现范围和没有实现的部分。第三方资产、Lyra、ALS 与 Fab 内容的使用边界见 [归因说明](Docs/Development/ATTRIBUTION.md)。

## License

本仓库用于公开展示个人源码与文档，暂未授予 MIT、Apache-2.0 等复用许可。Epic、Fab、Lyra、ALS 及论文内容仍遵循各自许可与版权。
