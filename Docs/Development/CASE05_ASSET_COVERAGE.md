# Case 05 项目早期美术覆盖审计

审计日期：2026-08-30
目标工程：`SIGGRAPHPhysicsLab` / UE 5.4.4 CL 35576357

## 结论

Case05 当前垂直切片不需要新增 Fab 包。Lyra 5.4、ALS V4、已加入工程的 Assassin/Dragon 与项目自制研究可视化已经覆盖核心体验；真正缺口是控制、状态、重定向和网络工程，不是资产数量。进入最终场景前只需验证明确的 ALS → Manny 或 ALS → Assassin 重定向结果，不应再下载另一套角色框架。

## 九类覆盖表

| 类别 | 当前来源 | 状态 | 使用决定与风险 |
| --- | --- | --- | --- |
| 角色 | Lyra Manny/Quinn；Assassin 四套整合角色；四条 Dragon | 最终可用 | 标准基准先用 Manny；最终镜头优先 Assassin，Dragon 只作 Boss 冲击来源。Assassin 是自定义 UE4 Skeleton，需验证重定向，不能假设与 ALS 直接兼容。 |
| 动画 | Lyra Dash/受击/死亡；ALS Flail、正反面 Get Up、Land Roll；Assassin 23 段、Dragon 69 段 | 需修改 | ALS Get Up 资产已存在，但必须通过 IK Retarget/曲线/Root Motion 门；不整包迁移 ALS AnimBP。 |
| 环境 | Lyra `L_ShooterGym`、`L_ShooterPerf`、Firing Range；ALS 示例场 | 最终可用 | `ShooterGym` 可承担可解释的物理实验室；最终可复制局部布局到 Case 私有地图，不修改源地图。 |
| 道具/武器 | Lyra Rifle/Pistol/Shotgun/Grenade、物理测试方块；Dragon | 最终可用 | 手雷/方块用于推撞与爆炸冲量，Dragon 可作为大体型碰撞源；输入力必须由服务器权威计算。 |
| 材质 | Lyra 角色/场景材质；Assassin/Dragon 原材质；个人 Debug 色 | 需修改 | 原材质保持只读；研究层增加相位、支撑面、力矩预算颜色，不复制第三方材质为个人成果。 |
| VFX | Lyra 命中、爆炸、伤害数字、死亡、Dash Niagara | 需修改 | 复用命中/爆炸反馈并做参数变体；物理状态和失败原因仍由 HUD/Debug Draw 明示。 |
| 音频 | Lyra 武器、命中、环境、UI；ALS 脚步/落地 | 最终可用 | 垂直切片已有最低闭环，最终只按需迁移落地/起身音效依赖。 |
| UI | Lyra CommonUI/HUD/Perf Stats；PhysicsResearch 中文 HUD | 需修改 | Case 私有中文 HUD 显示相位、姿态误差、恢复时间、力矩/能量、服务器权威和失败原因。 |
| 展示一致性 | Lyra 技术实验室 + Manny；Assassin/Dragon 风格资产 | 需修改 | 标准基准保持 Lyra 技术实验室风格；最终场景只在 Assassin 重定向通过后加入 Dragon，避免把互不一致的包强行拼在同一镜头。 |

## 迁移与许可边界

- ALS 源工程：`D:\3D\UE Projects\AdvancedLocomotionSystemV`。候选只包括 `ALS_Flail`、正/反面 Get Up、Land Roll 及其必要曲线/Notify；迁移前记录依赖和 Skeleton，结果进入 `PhysicsDrivenCharacter` 私有挂载点。
- Assassin 与 Dragon 已在 `/Game/Assassin`、`/Game/FourEvilDragonsHP` 注册并可加载，保持只读且不进入公开源码仓。
- Lyra、ALS 和 Epic Physics Control 的贡献必须在作品归因中单列；个人贡献是控制状态机、物理预算、Chaos/Physics Control 编排、网络边界、量测和失败实验。

## 触发重新找资产的唯一条件

只有 ALS 的正/反面起身动作经过两条目标 Skeleton 重定向仍无法满足脚底滑移、根朝向和姿态连续性门，且 Game Animation Sample 5.4 也无可用替代时，才定向寻找 UE 5.4 Manny 兼容的 Get Up 动画。当前条件尚未触发。
