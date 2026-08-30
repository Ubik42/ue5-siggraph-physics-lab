# Case 03：Particle Flow Map 流体复现范围

## 论文依据与作品主张

主线论文为 2024 SIGGRAPH《Eulerian-Lagrangian Fluid Simulation on Particle Flow Maps》。论文核心是把真实前向粒子的初末位置与 backward-map Jacobian 作为 Flow Map 样本，通过长/短双尺度映射、含梯度的保守粒子—网格传输和网格压力投影，减少长时平流耗散。论文处理无黏 Euler 流和运动学固体边界，不覆盖自由表面、黏性或双向固液受力。

本 Case 定位为“受 PFM 启发的 UE 工程复现”，不声称完整复现论文。现已建立可测试的 2D MAC/粒子/双 Map CPU 闭环，并在真实 D3D12 RDG 上完成持久粒子、Impulse/梯度 P2G、压力反馈、周期 Map Reset、非阻塞 timestamp，以及版本化运动固体的速度、压力与粒子防穿越。Live Chaos Actor 已在独立运行地图中驱动同一边界版本，玩家交互、GPU 粒子/速度箭头、逐 Cell 压力势热图、失败消融和真实 GUI 截图均已通过。正式 `ShortMapBaseline` / `ParticleFlowMapResearch` 权威 GPU 会话切换、低中高预算矩阵及两条 1200 步长序列逐步 GPU CSV 也已建立；标准基准映射、60–75 秒黄金路径、第三方/个人归因和总验收已经收口，Case03 进入冻结状态。

## UE / Chaos / 研究求解器分层

| 层 | UE / Chaos 职责 | Case 03 个人实现 |
|---|---|---|
| 世界与固体 | Actor 生命周期、Chaos 刚体变换、碰撞几何/场景查询 | 把静态或运动障碍栅格化为流体边界条件 |
| 固定步与实验 | `PhysicsResearch` 状态机、确定性种子、指标与导出 | Flow Map 会话、重置周期和质量门 |
| 流体状态 | 不把 Niagara 粒子当权威状态 | MAC 面速度、压力/散度、PFM 粒子、Map Jacobian |
| 计算 | UE Task/RDG/RHI 负责调度与资源生命周期 | 粒子平流、P2G/G2P、压力投影与 Map 重初始化 |
| 表现 | Niagara、材质、Debug Draw、中文 HUD | 只消费求解快照，不反向决定物理结果 |

Chaos 在本 Case 的联动重点不是“Chaos 自带流体求解器基线”，而是运动刚体、碰撞边界、固定步生命周期与调试接口。正式数值 A/B 使用同一 MAC 网格、同一初态、同一固定步和同一压力预算下的每步重置 Short Map 路径，与 Long Map Gradient/PFM 研究路径对比数值耗散、散度和粒子保持。运行界面和报告分别使用 `ChaosRigidBoundary`、`ShortMapBaseline` 与 `ParticleFlowMapResearch`，不得把 Short PIC/APIC 或 Niagara Fluids 冒充 Chaos 数值基线。

## 标准场景与指标

1. 直槽均匀流：验证 MAC 数据布局、零散度、粒子解析位移、边界与长短 Map 重置计数。
2. 刚体旋涡：验证轨道半径、动能/涡量代理、Jacobian 行列式和长时数值耗散。
3. 弯槽与分流：验证边界栅格、质量通量代理、分支流量比和粒子丢失。
4. 固定/移动障碍：由 UE/Chaos 提供障碍变换，验证无穿透边界和尾涡结构。
5. 压力或 Map 重置不足的故意失败配置：只改变一个参数，保留相同初态与预算。

所有规模至少报告网格/粒子数、固定步、CFL、速度误差、散度 RMS/极值、粒子丢失、Map 重置次数、Jacobian 失真、平均/P95/P99 或极值耗时。GPU 时间必须来自非阻塞 timestamp 或 Insights，不能用渲染线程提交耗时冒充。

## 当前首切片合同

- `PhysicsFlowMapRuntime` 为独立 Game Feature 运行时模块，不把 Case 代码塞回共享核心。
- 速度使用 2D staggered MAC 网格：`U=(Nx+1)×Ny`，`V=Nx×(Ny+1)`。
- 每格确定性均匀布置 `S×S` 粒子；粒子持有当前位置、长/短 Map 原点和 backward-map Jacobian。
- RK2 固定步消费 MAC 双线性速度；速度梯度更新 backward-map Jacobian；按独立周期或失真阈值重置 Map。
- 首门验证 16×8 Clamp 直槽与 32×32 周期域解析刚体旋涡；周期回卷与粒子丢失分开统计，不用方形边界硬钳位污染旋涡轨道。该阶段没有 impulse P2G、压力投影、GPU、UE 地图或 Niagara 展示，因此不能标记 Case 03 完成。

## 演化态传输消融

首个时间演化门使用 32×32 周期网格、4096 粒子、120 个 1/60 秒固定步和 `(20,10) cm/s` 均匀速度平移 `sin(2x)cos(2y)` 标量。该标量可解释为向量 impulse 的单个分量；解析平移解使数值耗散能够与边界、压力和速度误差分离。

三条路径共享所有输入：Short PIC 每步执行一次有损网格重采样，共 120 次；LongMapPic 每 20 步重置一次，共 6 次，但不使用粒子梯度；LongMapGradient 使用相同 6 次重置并加入一阶梯度 P2G。最终 RMS 分别为 `0.498927 / 0.123740 / 0.007644`。梯度长 Map 相对 Short PIC 的误差比为 `0.0153`，相对相同周期 LongMapPic 为 `0.0618`，从而把“少做有损通信”和“梯度重建”两项贡献分开。

Short PIC 的幅值由解析 `0.96194` 耗散到 `0.00207`，LongMapGradient 保持 `0.97670`；对应能量代理为 `1.15e-6 / 0.2577`，解析为 `0.25`。长 Map 略有能量超调，因此当前只宣称显著降低耗散和 RMS，不宣称严格能量守恒。该门仍未把向量 impulse 回写为速度并重新压力投影，完整闭环和旋涡场消融属于下一阶段。

## 向量 impulse 与压力速度闭环

Taylor–Green 周期旋涡把上一节的分量传输推进为真实速度反馈。每个粒子保存 reference impulse `m₀` 和 backward-map Jacobian `B`，由当前压力投影后的非均匀 MAC 速度做 RK2 平流；在线 impulse 为 `Bᵀm₀`。每个固定步都把粒子 impulse 归一化写回 U/V 面，随后执行周期 Poisson 投影，投影后的速度再进入下一步粒子推进。

32×32 网格、4096 粒子、120×1/240 秒和每步 128 次压力 Jacobi 下，Short PIC、LongMapPic、LongMapGradient 三路径依次隔离重置周期和向量梯度项。最终速度 RMS 为 `11.1807/1.03065/0.017909 cm/s`；梯度路径相对 LongMapPic 的误差比 `0.0174`，相对 RMS 仅 `0.1266%`。初始动能代理 `99.0393`，三路径最终为 `4.3853/85.9592/100.2291`。三者散度 RMS 都低于 `1e-3`，因此能量差异不能归因于跳过不可压缩约束。

该门证明长程 Map 状态、非均匀速度平流、向量 impulse/梯度 P2G 和压力反馈已经闭环，也暴露梯度 Long Map 的动能约 1.20% 超调、散度高于 Short PIC但仍在门内。当前使用 `BᵀG₀B` 一阶梯度变换，忽略 `B` 空间导数对应的 Hessian 类项，backward Jacobian 也采用一阶离散；后续迁移 GPU 时必须保留这些误差门，不能把 CPU 结果写成论文完整实现。

## GPU / RDG 压力子系统迁移

首个 GPU 门选择压力投影而不是直接迁移整条 Flow Map：它拥有确定的 CPU 真值、明确的 Pass 拓扑和逐面可比输出。32×16 周期压缩波在 CPU 与 D3D12 GPU 上使用相同 512 次 Jacobi 预算；GPU RDG 执行 1 次散度、512 次压力 ping-pong 和 2 次面速度回写。CPU/GPU 投影后散度 RMS 为 `0.00988997345 / 0.00989034952`，所有 U/V 面最大误差为 `3.60012e-5 cm/s`。

这证明个人实现已进入 UE 的 Global Shader、RDG Buffer、UAV/SRV 生命周期和 RHI Readback 边界，而不是把 CPU 数组包装成“GPU 效果”。当前阻塞读回只服务自动化真值门；实时作品必须改为持久资源和异步快照，并为 GPU 成本补非阻塞 timestamp。

压力子系统随后已进入持久会话：FaceU/FaceV、散度和两份压力 Buffer 跨四个投影步保留，游戏线程只排队，U/V 快照通过成对 Readback 异步返回并携带源步号。四个 `128` 迭代 GPU 步累计 4/512/8 个散度/Jacobi/Apply Pass，最终第 4 步快照相对顺序相同的 CPU 路径，面速度最大误差 `5.30905e-6 cm/s`。验证结束使用的 GPU Idle 只属于自动化收口，不进入运行 API。

权威粒子状态也已开始 GPU 化。独立持久会话在周期 staggered MAC 场上用 RK2 推进 1024 粒子，并从同一中点采样速度梯度更新 `2×2` backward-map Jacobian。20 个 `1/240 s` 步后，GPU/CPU 位置最大误差 `3.81470e-6 cm`，Jacobian 分量与行列式差均不超过 `8.34465e-7`；最终异步快照严格标记为第 20 步。该门证明粒子轨迹和 Map 变形已进入 RDG，但尚未携带 reference impulse/gradient 或回写 MAC 网格。

Reference impulse 与梯度重建也已具备 GPU 真值。256 个畸变粒子携带非单位 `B`、`m₀` 和 `G₀`；确定性面 Gather 分别验证 `Bᵀm₀` 与 `BᵀG₀B` Taylor P2G。普通/梯度模式相对 CPU 的最大面速度误差为 `2.86102e-6 / 5.72205e-6 cm/s`。面主动 Gather 不依赖浮点 Atomic 顺序，代价是当前复杂度为 `O(face×particle)`；它是参考正确性实现，后续性能优化必须保持相同输出门。

上述子系统现已真正串成持久 GPU 闭环。每个 Enqueue 从上一步投影速度出发，依次完成粒子 RK2/Jacobian、梯度 impulse P2G、散度、64 次压力 Jacobi 和 U/V 回写；最终速度保留为下一步输入。16×16、256 粒子的 4 步门精确执行 `4/8/4/256/8` 个平流/Gather/散度/Jacobi/Apply Pass，统一异步快照同时包含速度和粒子状态并标记第 4 步。相对同输入 CPU LongMapGradient，最大面误差 `1.83582e-5 cm/s`，散度 RMS 为 `1.66251e-6`。

Long Map 生命周期也已跨越真实重置边界。8 步门以 4 步为周期触发两次 GPU Reset；Reset Compute 位于压力 Apply 之后，从最终投影 U/V 重新采样 `m₀/G₀` 并将 `B` 设为单位阵。重置步写回 Current 粒子 Buffer 且不执行普通 ping-pong 交换，最终快照因此读取重置后状态。相对同顺序 CPU 闭环，最大面速度误差 `2.00272e-5 cm/s`；第 8 步 Jacobian 单位阵与行列式误差均为 0。

完整固定步现由 8 槽 `RQT_AbsoluteTime` 环做非阻塞计时。32 步批量排队时完成 8 个样本、显式跳过 24 个满槽步，二者精确覆盖全部执行步；预热 4 步后的 4 个样本均值/P95 为 `4.336/6.347 ms`。这证明查询环、丢样统计和全步计时范围可用，但样本量不足，不能取代运行地图按真实帧采集的长序列 CSV。

运动固体边界先由 UE/Chaos 适配层生成：前后 `FTransform` 在固定步上差分为平面线速度与 UE Z/Yaw 角速度；运动圆随后分别在 Cell、U 面和 V 面采样，所有与固体 Cell 相邻的界面保存 `v_body+ω×r`。16×16 CPU 真值门恢复 `20 cm/s` 与 `1.04719758 rad/s`，20/20 个 U/V 面的解析残差为 0。

这五份数组现由版本化接口进入持久 GPU 会话。连续两步分别捕获版本 1/2；散度在固体面使用刚体速度，固体 Cell 从压力未知量排除，Apply 后把 U/V 面重新钉回无滑移值。同一版本还携带解析圆，粒子 RK2 后若进入安全半径就沿法线投影到外壳。第二版 20/21 个 U/V 固体面残差为 0，流体 Cell 散度 RMS/最大值为 `0.0190895/0.0194440`；256 个粒子无一留在固体内，最小表面间隙 `0.0499992 cm`。全零掩码仍保持原 Taylor–Green CPU/GPU 误差 `1.81198e-5 cm/s`。

## 冻结后的能力边界

- 尚未实现 3D、自由表面、黏性、多相或双向固液耦合。
- Live Chaos 运动障碍、玩家交互、压力势读回与热图、失败消融、三态 GUI、每步重置 Short Map 基线、PFM 研究后端真切换、低中高预算、作品级长序列逐步 GPU 时间 CSV、标准基准映射、黄金路径和归因均已完成。
- 当前粒子接触仅支持最多 8 个解析圆，尚未在每次接触时重置局部 Map Jacobian；这属于已知算法边界，不阻塞当前 2D 运动圆作品切片。
- 论文的 10–20 倍平均加速来自相对 Neural Flow Maps；本作品没有 NFM 实现时不会复用该加速结论。
