# SIGGRAPH Physics Lab 架构

## 依赖方向

```text
LyraGame / GameFeatures / ModularGameplay
                    │
                    ▼
             PhysicsResearch
      固定步进、后端契约、指标、导出
       ┌────────┬────────┬────────┬────────┐
       ▼        ▼        ▼        ▼        ▼
 ContactCloth SoftBody FlowMap InverseDesign DrivenCharacter
```

Case 插件只能依赖共享核心或明确的 Lyra Feature；共享核心不得反向依赖任何 Case。

## Game Feature 清单

| 插件 | 中文作品 | 直接依赖 | 首个技术切片 |
|---|---|---|---|
| `PhysicsContactCloth` | 无穿透接触与 GPU 布料 | PhysicsResearch | 规则方布 + 球/楔形接触，随后接 Assassin 披风 |
| `PhysicsSoftBody` | GPU 多尺度软体 | PhysicsResearch | 悬臂梁/压缩球，随后接 Dragon 派生封闭网格 |
| `PhysicsFlowMap` | Flow Map 流体 | PhysicsResearch | 直槽 + 固定障碍的速度场与粒子路径 |
| `PhysicsInverseDesign` | 可微物理反向设计 | PhysicsResearch | 弹道目标或二维形状参数反演与梯度检查 |
| `PhysicsDrivenCharacter` | 物理驱动角色 | PhysicsResearch、ShooterCore | Lyra 角色受击转物理与确定性恢复 |

## 资产边界

- 第三方只读：`/Game/Assassin`、`/Game/FourEvilDragonsHP`。
- 项目共享个人资产：`/Game/PhysicsResearch`。
- Case 私有资产：各 Game Feature 自己的挂载点。
- ALS 只迁移明确需要的动作及依赖，不跨项目引用，不覆盖 Lyra 权威 Gameplay 架构。

## 后端契约方向

共享核心后续提供：实验定义、运行状态机、固定步进时钟、确定性种子、Chaos/Research 后端接口、逐步样本、聚合统计、CSV/JSON 导出和 Debug Draw。Case 负责物理状态与指标语义，UI 不持有权威仿真状态。
