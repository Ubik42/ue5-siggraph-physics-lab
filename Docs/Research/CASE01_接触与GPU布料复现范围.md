# Case 01：接触与 GPU 布料复现范围

## 论文锚点

- 论文：*Efficient GPU Cloth Simulation with Non-distance Barriers and Subspace Reuse*
- 会场：SIGGRAPH Asia 2024
- DOI：10.1145/3687760
- 本地精读结论：方法以 Projective Dynamics 为主体，把低频全局误差、局部高频误差和碰撞安全分别交给模态子空间、GPU Jacobi 与边界完整 CCD；连续接触使用非距离障碍增权，固定预算剩余误差可跨帧转发，但必须设置失效门与回退。

## UE / Chaos 职责边界

| 层 | Chaos 基线 | 论文后端 |
|---|---|---|
| 角色与动画 | Lyra/Assassin Skeletal Mesh、Animation、Physics Asset | 复用同一驱动姿态 |
| 布料求解 | Chaos Cloth | 独立 `PhysicsContactClothRuntime` 组件与 Compute Shader |
| 碰撞表示 | Chaos 场景查询、球/胶囊、Physics Asset | 第一阶段球/胶囊解析接触，后续 SDF 与 CCD |
| 数据与展示 | `PhysicsResearch` 固定步、指标、HUD、导出 | 复用同一实验协议 |
| 对照方式 | 保留原始 Marketplace 资产，只读 | 同场并排或同输入回放，不修改 Chaos Cloth 源码 |

这不是对 Chaos 的全面替代。Case 要证明的是能够识别引擎物理模块的资产层、碰撞层、求解层、调度层和可视化层，并只替换研究需要的求解职责。

## 分阶段实现

1. **Chaos 资产运行门（已完成）**：真实验证 Assassin Cloth 实例、2011 粒子回写与动态位移。
2. **CPU 数学参考（已完成）**：程序化规则网格、质量/固定点、拉伸/剪切/弯曲约束和球接触已通过确定性测试与真实地图运行；胶囊接触随 GPU 基础阶段补入。
3. **GPU 局部求解（已通过）**：Structured Buffer、聚合 Jacobi、固定预算、持久位置 Buffer、异步回读与批量动态碰撞已在 D3D12 SM6 下通过。Assassin 动画组件每固定步把当前 Physics Asset 姿态的 1 球/16 胶囊上传 GPU；300 步运行、角色/碰撞体/布片同屏与机器报告均通过。
4. **论文关键结构（工程复现范围已完成）**：固定顶边 12 模态、缩减 SPD 全局校正、GPU NDB 连续激活、解析完整 CCD 和带安全门的残差跨帧转发已接入同一逐帧管线。完整 `3m×3m` rank-one 碰撞 Hessian 具有 CPU 真值、逐粒子缓存和独立 GPU 装配门（GPU/CPU 最大误差 `1.911e-7`），并已在持久管线中从当前解析接触装配矩阵/RHS、GPU Cholesky 求解后回写低频校正。工程版每固定步装配一次并保留局部罚投影处理高频细节，不宣称等同论文每轮 local-global 内重装配。
5. **消融与失败边界（已完成）**：无子空间消融、常量罚权重/NDB × 无 CCD/CCD 四象限微基准与 rank-one 开关 300 步 Assassin 消融均已通过；已导出活跃接触对、连续激活、残差回退、CPU 渲染提交耗时和真实 GPU timestamp。8 槽查询环不等待 GPU；最新标准配置 300 步无丢样，固定排除前 16 步后稳态均值 4.753 ms、P95 5.110 ms、P99 5.197 ms、最大 5.317 ms。另有只降低 NDB 权重的 `weak_ndb` 玩家失败配置，在同一输入哈希下把最大穿透从 0.0000858 cm 放大到 0.0486479 cm。
6. **严格同拓扑对照（基础闭环已完成）**：已将 Assassin 两份 Chaos Clothing Asset 的 LOD0 物理网格提升为正式运行时输入契约，提供 2011 顶点、3705 三角形、5714 条排序唯一边及静止长度、1307 固定点、704 动态点、逆质量、MaxDistance 和资产区间。隔离地图 `L_GpuExplicitAssassin` 只使用一个注册角色组件：它同时驱动 UE Clothing 蒙皮目标、Physics Asset 解析碰撞和 Chaos Cloth 输出。固定 60 Hz 运行采集 300 份 Chaos 快照，并把 75 份研究 GPU 异步读回按源步号配对，首步 4、末步 300。显式 12 模态、rank-one、NDB 与完整 CCD 已每步执行；工程版图低频基和每固定步一次 rank-one 装配不等同论文完整离线模态/每轮重装配，GPU timestamp 也覆盖完整 RDG 步而非单 kernel，因此仍不称为论文完整复现或性能等价对照。
7. **角色披风展示（已完成）**：HUD 同屏显示 Assassin、Physics Asset、两后端拓扑，并绘制 Chaos/研究动态位移与完整固定步 GPU 时间曲线。确定性重放入口会哈希 300 步蒙皮目标及球/胶囊输入，正确期望哈希通过、错误哈希失败。玩家可暂停/继续、安全单步、完整重置、切换实际显示后端、切换调试绘制、切换标准/弱障碍失败配置并导出报告；自动交互脚本与两张独立 GUI 截图已验证。

## 标准算法基准清单

| 基准 | 固定输入与对照 | 量化结果 | 证明范围 |
|---|---|---|---|
| 任意拓扑 Jacobi | Assassin 2011 粒子/5714 边确定性扰动，输入 vs 128 次 GPU Jacobi | 最大边误差 `9.6211 → 0.3406 cm`，1307 固定点漂移 0 | 真实 Chaos Clothing 拓扑被研究 GPU 消费 |
| NDB 连续接触 | 四步持续重叠，常量罚权重 vs `K=2` 连续增权 | 穿透 `4.1135 → 1.5031 cm`，激活计数达到 4 | 历史增权与常量 penalty 职责分离 |
| 完整 CCD | 球高速中途穿越，离散端点 vs 线段扫掠 | 离散落到 `z=-100.000`，CCD 截停 `z=10.010` | CCD 只处理路径穿越，不冒充初始重叠修复 |
| rank-one Hessian | CPU 双精度真值 vs D3D12 完整矩阵装配 | XY 耦合非零，GPU/CPU 最大误差 `1.911e-7` | 向量接触法线耦合没有被三个标量轴替代 |
| 300 步作品运行 | Chaos 与研究后端共用动画、拓扑与 Physics Asset 输入 | 75 个源步配对、300 个 GPU 样本、研究端最大穿透 `0.0000858 cm` | 引擎集成、求解、观测和导出闭环 |

上述五项中前四项是可重复算法基准，第五项是作品级综合运行门；它们不以“看起来相似”替代数学或输入合同。

## 45 秒紧凑演示路径

该路径用于短版作品集剪辑；包含完整失败归因与性能收尾的 60 秒黄金路径见 `Docs/Validation/M2/README.md`。

1. 进入 `/PhysicsContactCloth/Maps/L_GpuExplicitAssassin`，先指出同一 Assassin 组件同时驱动 Chaos 与研究后端，右侧两图分别是动态位移和完整 GPU 步耗时。
2. 按 `Space` 暂停，观察角色动画、Chaos Cloth 和研究固定步同时冻结；按 `N` 只推进一帧，再按 `Space` 继续。
3. 连按 `B`，依次观察 Both、Chaos Only、Research Only 的真实几何来源；切回 Both。说明后台仍维持双求解器，用于同源配对而不是伪造 UI 标签。
4. 按 `D` 关闭/打开固定点、碰撞原语和诊断绘制；按 `R` 重建会话和指标，证明 Demo 可重复而非一次性录屏状态。
5. 按 `F` 切到 `weak_ndb`：其他安全层不变，HUD 将最大穿透标为约 `0.048648 cm` 的预期失败；再按 `F` 回到 `k=10` 标准配置。
6. 按 `E` 导出 CSV/JSON，以 HUD 的 2011 粒子、5714 边、1 球/16 胶囊、Pass 计数和 GPU P95 收尾。

## 不提前宣称的内容

- CPU 规则网格通过不等于论文 GPU 方法复现完成。
- 只做视觉相似不等于验证非距离障碍、模态子空间或 CCD。
- 论文依赖二次弹性能，不能把结果泛化为所有真实织物。
- 残差转发不是无条件稳定机制；必须展示阈值与回退，而不是只保留最好看的录屏。
