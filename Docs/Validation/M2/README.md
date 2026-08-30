# M2 Chaos Cloth 资产运行门

## 目的

验证 Assassin 资产不是“包内存在 Chaos 配置”就被误判为可用，而是在 UE 5.4 游戏进程中真正完成以下链路：

`Skeletal Mesh 加载 → Clothing Asset 绑定 → Chaos Cloth 实例 → 粒子回写 → 动态位移`

## 场景

- 地图：`/PhysicsContactCloth/Maps/L_ChaosCloth_AssetGate`
- 资产：`/Game/Assassin/Mesh/SKM_Assassin_Skin1`
- 动画：`/Game/Assassin/Animation/Anim_Assassin_idle1`
- 后端：UE 5.4 Chaos Cloth
- 固定步：1/60 秒，共 240 步；前 30 步预热。

## 结果

- 2 份 Clothing Asset。
- Chaos Cloth 运行时实例创建成功。
- 2011 个模拟粒子回写到游戏线程。
- 记录 844 条指标，覆盖步号 30–240。
- 指标：`ActiveClothActors`、`ClothParticleCount`、`MeanClothDisplacement`、`MaxClothDisplacement`。
- 动态位移门槛大于 0.01 cm，运行门通过，`fallback_required=false`。
- `PhysicsResearch.` 完整 D3D12 SM6 自动化：32 成功、0 失败、0 警告（含任意拓扑模态 CPU 真值、显式 D3D12 rank-one A/B 与非阻塞 GPU timestamp 环）。

数值位移会随实际渲染帧调度略有变化，因此验证门只约束有限数、非零运动、粒子数和完整步号，不把某次最大位移写成黄金常量。

## 证据

- `chaos_cloth_asset_gate.json`：游戏进程写出的运行门结论。
- `asset_gate_validation.json`：13 项机器可读复核。
- `Automation/index.json`：3/3 自动化报告。
- `Screenshots/M2_ChaosClothAssetGate.png`：1280×720 中文 GUI 证据。
- `Saved/PhysicsResearch/M2_ChaosClothAssetGate.json/.csv`：运行指标，不进入源码仓。

## 结论

Assassin 整合 Skeletal Mesh 可以直接用作 Chaos 对照组，不需要新增 Marketplace/Fab 布料包。自定义论文求解器仍使用程序化规则网格起步，避免把资产拓扑、蒙皮和涂权问题混入数学正确性验证。

## CPU 数学参考

资产门之后已新增 `FContactClothCpuSolver`，作为 GPU 实现前的可读真值层：

- 规则网格与固定顶边；
- 拉伸、剪切和二阶邻接弯曲约束；
- 质量预测与 local-global Projective Dynamics；
- 确定性 Gauss-Seidel 全局步；
- 球接触厚度投影、速度回写、拉伸误差和剩余穿透统计。

`PhysicsResearch.ContactCloth.CpuProjectiveDynamics` 已验证重力响应、固定点不漂移、接触被触发、最终剩余穿透小于 `1e-6 cm`、有限误差、相同输入确定性一致，以及重置回静止形状。它是数学参考，不代表 GPU/NDB/子空间/CCD 论文复现已经完成。

### CPU 真实运行

- 地图：`/PhysicsContactCloth/Maps/L_CpuProjectiveDynamics_Reference`
- 规模：120 粒子、612 条拉伸/剪切/弯曲约束。
- 运行：300 个 1/60 秒固定步，导出 1200 条指标。
- 接触：累计 11998 次球投影，最终剩余穿透 0 cm。
- 最大观测拉伸比误差约 0.2062；该值包含布片初始进入接触的瞬态，作为当前 CPU 参考预算而非论文精度结论。
- 机器校验：`cpu_reference_validation.json`，13/13 通过。
- GUI：`Screenshots/M2_CpuProjectiveDynamics.png`，可见约束类别、固定点、球接触和实时误差。

## GPU Jacobi 首个计算门

CPU 参考之后已加入首个真实 GPU 计算门：

- 常驻 `PhysicsResearchShaders` 模块在 `PostConfigInit` 注册全局 Compute Shader；显式加载的 Game Feature 只保留测试与 Case 调用职责。
- 16×12、共 192 个粒子的规则网格通过 Structured Buffer 上传，执行 96 次 ping-pong 聚合 Jacobi。
- 顶边固定，中心粒子施加 60 cm / 25 cm 扰动；测试要求最大结构边误差至少降低 20%。
- 结果通过 GPU Buffer Readback 回到 CPU，并检查粒子数、有限数、固定边和误差下降。
- 同一核支持解析胶囊投影；测试输入最大穿透 29 cm，输出剩余穿透约 0.000002 cm，作为后续读取 Assassin Physics Asset 球/胶囊表示的接口基础。
- 真实环境：NVIDIA GeForce RTX 4080、D3D12 SM6；`GpuJacobiDispatch` 1/1 成功、0 警告、0 错误。

证据位于 `GpuJacobiAutomation/index.json` 与 `gpu_jacobi_validation.json`。当前 `RunBlocking` 和 `BlockUntilGPUIdle` 是刻意保留的验证路径，不能作为生产逐帧布料架构；运行版将改用持久 Buffer、Render Graph 调度与异步指标回读。

本门也确认了 UE 集成边界：全局 Shader 类型不能随 Game Feature 的延迟激活注册，即使模块描述写成 `PostConfigInit` 仍会过晚。正确结构是由常驻早期模块拥有 Shader 注册，Game Feature 拥有实验逻辑和场景生命周期。

### 持久 GPU Buffer 与固定步动力学

一次性正确性门之后已加入独立的逐帧会话合同：

- 四份跨帧 Structured Buffer：当前位置、前一位置、工作区 A/B；不再每步从 CPU 重新上传网格。
- GPU Verlet 预测使用固定 `1/60 s`、重力和速度衰减，随后执行固定预算 Jacobi。
- 运行 API 只排入 Render Graph，不调用 `BlockUntilGPUIdle` 或 `FlushRenderingCommands`。
- Readback 按步间隔异步发起，在后续帧轮询；显式当前快照接口用于报告和退出门。
- `GpuRealtimePersistentBuffers` 在 RTX 4080 / D3D12 SM6 下执行 120 步，最终读回步号 120、120 粒子、2 次快照，最大结构边误差约 1.0303 cm。

自动化中的等待辅助入口只负责让测试获得确定的最终证据，不被运行 Actor 调用。机器报告：`gpu_realtime_validation.json`。

### 逐帧 GPU 运行地图

- 地图：`/PhysicsContactCloth/Maps/L_GpuRealtimeCloth`
- 规模：300 粒子，每步 24 次 Jacobi，共 300 个 `1/60 s` 固定步。
- 调度：每个共享固定步先从 Assassin 动画组件的当前骨骼姿态解析 Physics Asset，再上传 1 球/16 胶囊并排入 GPU 工作；运行 Actor 不调用测试专用等待入口。
- 动态姿态证据：300 步期间完成 301 次姿态解析；角色、橙色碰撞体和青色 GPU 布片同屏。
- 异步证据：74 次快照，最大滞后 8 步，最终快照步号精确为 300。动画姿态提取增加了游戏线程工作量，但仍保持非阻塞 GPU 调度。
- 求解结构：每步执行 12 维 `Uᵀr → Aᵣ⁻¹ → Uq` 缩减全局校正、NDB 连续激活更新、24 次局部 Jacobi，再以完整解析 CCD 作为退出锚点；300 步三类 Pass 均执行 300 次。
- 结果：最大结构边误差约 1.2661 cm，完整 Physics Asset 自由粒子剩余穿透 0 cm，布片底部下降约 6.746 cm。
- 指标：四项指标各 73 条，共 292 条 JSON/CSV 样本；数量与实际完成的异步快照严格对应。
- 机器校验：`gpu_realtime_visual_validation.json` 通过，并显式核验 NDB/CCD 每固定步执行与 17 个碰撞体容量。
- GUI：`Screenshots/M2_GpuRealtimeCloth.png`；可见 Assassin、GPU 布片、固定顶边、动态 Physics Asset、共享/GPU 步数、姿态更新和误差。

运行验证还修正了共享固定步循环的状态传播：回调内暂停后不会在同一帧多推进一步，因此 HUD 与 GPU 最终步均稳定为 `300 / 300`。

### Assassin Physics Asset 适配门

`FContactClothPhysicsAssetAdapter` 直接使用 UE C++ `UPhysicsAsset → USkeletalBodySetup → AggGeom` 链路，支持：

- 在 Skeletal Mesh 参考姿态中生成组件空间球/胶囊；
- 从运行中的 `USkeletalMeshComponent` 获取随骨骼更新的世界空间球/胶囊；
- 把 Sphyl 的中心、旋转、柱段长度和半径转换为研究后端需要的线段端点与半径。

真实 `SKM_Assassin_Skin1 + PHYS_Assassin_Skin1` 结果为 18 个 Body、1 个 Sphere、16 个 Capsule、0 个缺失骨骼。自动化和机器报告位于 `PhysicsAssetAdapterAutomation` 与 `physics_asset_adapter_validation.json`。

### GPU 批量碰撞 Buffer 与动态姿态更新门

GPU 接触核已从单球/单胶囊常量扩展为三组 Structured Buffer：球、胶囊起点/半径、胶囊终点。布料当前位置、前一位置和两份工作 Buffer 继续跨帧持久化；角色骨骼碰撞体作为小规模动态快照，在每个固定步入队时复制并上传，避免游戏线程与渲染线程争用。

- D3D12 SM6 自动化使用 108 粒子、1 个球和 2 个胶囊，显式关闭旧单碰撞体参数。
- 输入最大穿透约 7.6716 cm，数组投影后剩余约 0.000003 cm。
- 同一测试先以空碰撞集合初始化实时会话，再调用 `UpdateCollisionPrimitives`；更新后的碰撞集合在第 1 个固定步生效，剩余穿透约 0.000003 cm。
- 机器校验：`gpu_batch_collision_validation.json`，12/12 通过；原始报告位于 `GpuBatchCollisionAutomation/index.json`。

合成碰撞体门之后，`ConvertToGpuPrimitives` 已把 Assassin 参考姿态的完整 18 个 Body 转成 1 球/16 胶囊并送入真实 Compute Dispatch。测试把一个自由探针放在该真实球体中心，GPU 输出位移 9 cm，证明数据不只经过计数而是参与了投影。机器报告为 `assassin_gpu_bridge_validation.json`。

完整参考姿态 Physics Asset 与逐固定步动态姿态链路现均已通过：`ExtractComponentPose → ConvertToGpuPrimitives → UpdateCollisionPrimitives → GPU Jacobi`，并已有角色、碰撞体和布料同屏的 GUI 证据。

### 低频模态子空间 CPU 真值门

论文关键结构已从静止形状低频模态开始实现。`FContactClothModalSubspace` 为固定顶边规则网格构建按频率排序的质量归一化标量模态，并提供投影、重建和能量保留测量：

- 20×15、300 粒子，12 个低频模态；
- 最大正交归一误差约 `2e-15`，所有模态的固定顶边严格为零；
- 平滑形变能量保留 100%；棋盘格高频仅保留约 `0.0000738`；
- 已组装 `Uᵀ(M + h²K)U` 对称正定缩减系统，并以 Cholesky 解恢复已知模态坐标，最大误差约 `1e-15`；
- 机器校验：`modal_subspace_validation.json`，10/10 通过。

CPU 真值层现已接入逐帧 GPU：第一遍计算 12 维 `Uᵀ(y-x₀)`，第二遍用预计算 `(UᵀAU)⁻¹` 求线性化 PD 全局目标并回写 `x*−y`，然后进入局部 Jacobi。整个路径使用持久 GPU Buffer，不增加逐帧 CPU Readback。

独立消融以相同 300 粒子、90 步、4 次局部 Jacobi 对比：无模态低频能量约 166774，混合求解约 29763（约 17.85%）；底部下垂从 32.883 cm 降至 13.636 cm，同时保留约 41.47% 的重力响应。机器报告 `gpu_modal_correction_validation.json` 为 11/11 通过。

曾尝试直接投影 `rest−predicted`，会把下垂压到 0.06 cm；随后修正为线性化 PD 全局目标。`h²K=18` 仍导致过硬锁定，最终按 1/60 秒尺度使用 `h²k=1.0`。这些失败参数不作为展示结果，但保留在开发记录中说明稳定性预算。

当前已建立论文 rank-one 碰撞 Hessian 的 CPU 真值与逐粒子投影缓存，并把完整 `3m×3m` 系统接入实时 GPU；跨帧 Subspace Reuse 仍以残差转发近似接入，因此不宣称完整子空间算法复现。

### 非距离障碍连续激活状态门

`FContactClothNonDistanceBarrier` 已实现论文 NDB 的状态合同：接触连续活跃 `aᵢ` 次时使用 `wᵢ = k·K^aᵢ`，默认 `K=2`；权重不读取当前几何距离，失活后连续计数归零，并提供最大权重限幅与饱和次数统计。

自动化验证了 5 次连续激活的指数序列、相同历史与距离无关、失活/重激活、限幅、统计和完全重置。机器报告 `non_distance_barrier_validation.json` 为 8/8 通过。

该状态现已进入 GPU 逐帧管线：每个 `粒子 × 解析碰撞体` 保留独立连续激活计数，固定步开始按预测位置更新；局部接触把 `w` 映射为 `α=w/(1+w)` 的有限罚投影。独立 D3D12 门在关闭 CCD、关闭结构松弛和速度记忆后，将穿透从首次接触的 4.545 cm 降到连续 4 步后的 1.503 cm，证明增强来自持久激活历史。

激活按固定步更新一次；其权重现同时进入局部有限罚投影和完整 rank-one 缩减矩阵。工程版尚未在每轮 local-global 内重装配，因此不把它宣称为论文逐字完整实现。机器报告为 `gpu_hybrid_collision_validation.json`。

四象限消融使用相同 2×2 探针、固定步、局部预算和球碰撞体，只切换 `K=1 / K=2` 与完整 CCD：常量罚权重 4 步后剩余穿透 4.114 cm，NDB 为 1.503 cm；无 CCD 的两组高速探针都穿到 `z=-100.000 cm`，有 CCD 的两组都停在 `z=10.010 cm`。CCD 对起始重叠不产生额外修正，说明它没有被误用为离散 depenetration。原始报告位于 `GpuHybridAblationAutomation/index.json`。

### 残差复用安全策略门

残差转发具有明确失败风险，因此先建立与 GPU 实现解耦的策略真值层。`FContactClothResidualReusePolicy` 只在拓扑版本一致、残差与 CCD 修正均为有限数且不超过阈值时返回转发缩放；其他情况按拓扑变化、残差过大、CCD 修正过大和非有限值分别回退并统计。自动化与机器报告位于 `ResidualReusePolicyAutomation` 和 `residual_reuse_policy_validation.json`。

该合同现已进入 GPU：每步把最后两次 Jacobi 位置差写入持久 Residual Buffer，下一固定步在 Verlet 预测中按 0.4 缩放转发。Capture Pass 拒绝非有限值和超过 2 cm 的残差；CCD Gate 拒绝超过 0.25 cm 的边界修正；碰撞体数量变化则清空全部自由粒子旧残差。W 分量累计的是逐粒子回退事件，不是唯一粒子数。

独立 D3D12 门执行 12 个安全固定步，最大当前可转发残差为 0.128833 cm、回退事件为 0；增加一个远端球改变碰撞拓扑后，30 个自由粒子各记录一次回退且残差归零。Assassin 真实地图完成 300 次残差 Pass，最大当前残差 0.014044 cm、累计 74 个回退事件，仍保持近零穿透与有限重力响应。证据为 `GpuResidualReuseAutomation`、`gpu_residual_reuse_validation.json` 和更新后的实时截图/JSON。

### Rank-one 碰撞 Hessian 真值门

为避免把三个轴分别加刚度而丢失真实接触法线耦合，CPU 真值使用完整 `3m×3m` 系统。每个粒子预缓存 `uᵢuᵢᵀ`；活跃接触以 `w(n⊗uᵢ)(n⊗uᵢ)ᵀ` 加入基础缩减系统。20 粒子、4 模态测试生成 320 个缓存值和 12 个向量自由度，XY 交叉项为 0.130065、对称误差 0、已知解恢复误差约 `6.94e-17`。

该门同时运行独立 D3D12 Compute 装配：每个线程写完整矩阵的一个元素，GPU 与 CPU 双精度真值最大误差为 `1.911e-7`。实时管线随后从当前球/胶囊解析接触提取法线和穿透，使用持久 NDB 权重装配矩阵及 RHS，以 GPU Cholesky 求解并回写低频校正。斜向球 A/B 门把穿透由 3.52445 cm 降为 3.45491 cm；Assassin 300 步完成 300 次 rank-one 系统，最终穿透为 0。证据为 `rank_one_hessian_validation.json`、`realtime_rank_one_validation.json` 与更新后的实时 GUI 报告。

### 300 步接触与提交耗时分布

持久会话在每次异步位置快照中同步读回 `粒子 × 碰撞体` 激活 Buffer，并导出 `ActiveContactPairs`、`MaximumNdbActivationCount` 与 `RenderSubmissionTime`。Assassin 300 步、74 组快照得到：活跃接触对平均 15.96、P95 40、P99/峰值 57；快照最大连续激活平均 26.73、P95/P99/峰值 30。

`RenderSubmissionTime` 只覆盖渲染线程构建并提交该固定步 RDG 的 CPU 时间：平均 0.154 ms、P95 0.1915 ms、P99 0.2275 ms、300 步峰值 0.529 ms。它不等于 GPU kernel 时间，不能用于声称 shader 本身只耗时 0.154 ms。

显式 Assassin 会话另用 8 组 `RQT_AbsoluteTime` 查询围住每个完整固定步的 RDG 区间；结果只在就绪时轮询，槽满便记 skipped，不触发 GPU 等待。最新标准配置 300 步得到 300 个样本、0 skipped；固定排除启动前 16 步后，284 个稳态样本均值 4.753345 ms、P95 5.110 ms、P99 5.197 ms、最大 5.317 ms。逐步数据为 `gpu_explicit_assassin_gpu_timing.csv`，机器报告 `gpu_explicit_timing_validation.json` 与 `gpu_explicit_assassin_validation.json` 分别验证查询环和从原始 CSV 独立复算的分布。查询区间包含上传、所有 Compute Pass 与计划的读回复制，不是 CPU 提交时间，也不是单 kernel 时间。

### Chaos Cloth 同拓扑输入门

现有 Chaos 资产运行门确实启动了 Assassin Chaos Cloth，但它使用 2011 粒子的服装物理网格，研究实时地图仍使用 300 粒子规则网格，因此此前只能证明两个后端都真实运行，不能证明同输入对照。

`FContactClothChaosTopology::ExtractLod0` 现是运行时代码，而不是只存在于测试中的审计逻辑。它直接读取两份 Clothing Asset 的 LOD0 `FClothPhysicalMeshData`，生成求解器无关的 `FContactClothExplicitTopology`：静止位置/法线、逆质量、MaxDistance、三角形、排序去重的结构边与静止长度，以及每份 Clothing Asset 的顶点/三角形/边区间。

`ChaosClothTopologyExtraction` 调用同一正式提取器并验证：合计 2011 顶点、3705 三角形、5714 唯一边、1307 固定点、704 动态点，MaxDistance 可动点恰为 704；目标测试 1/1 通过，机器报告为 `chaos_cloth_topology_validation.json`。

研究 GPU 后端随后首次真实派发这份数据：CPU 将唯一边转换为双向 CSR 邻接表，结构修正按 Chaos 逆质量分配；`.Blocking` 门在 D3D12 SM6 上对全部 2011 粒子执行 128 次聚合 Jacobi。确定性扰动的最大结构边误差从 9.621097 cm 降至 0.340600 cm，1307 个固定粒子的最大漂移为 0；目标测试 1/1、0 警告，机器报告为 `gpu_explicit_topology_validation.json`。

`FContactClothGpuExplicitTopologySimulation` 又把这份输入提升为持久会话：当前位置、前一位置、两份工作 Buffer、逆质量和 CSR 拓扑跨固定步保存，每步只排入 Render Graph，异步位置回读与测试专用最终等待分离。`.Realtime` 门执行 90 个 1/60 秒固定步，最终快照精确对应第 90 步；704 个动态粒子平均位移 27.362173 cm、P95 58.781708 cm，平均 Z 位移 -25.273597 cm。5714 条结构边误差平均 0.254785 cm、P95 1.577347 cm、P99 3.810905 cm、最大 7.226608 cm；1307 个固定粒子漂移为 0。

机器报告 `gpu_explicit_topology_realtime_validation.json` 同时记录 CPU 渲染线程 RDG 构建/提交平均 0.033346 ms、峰值 0.115400 ms；这不是 GPU kernel 时间。当前已证明真实拓扑、跨步动力学和非阻塞运行合同连通，但固定粒子仍锚定资产静止位置，尚未接动画蒙皮目标、Physics Asset 碰撞和论文 NDB/模态/rank-one Pass，因此仍不称为同姿态 Chaos 对照。

### UE Clothing 姿态目标与 GPU 运动学边界

拓扑提取器现把骨骼合同纳入有效性检查：两份 Clothing Asset 共 38 个 UsedBone 映射条目、4070 个顶点影响，单顶点最多 4 影响；每个顶点权重和位于 1.000000000–1.000000119。`FContactClothSkinningAdapter` 不重写蒙皮算法，而是逐资产调用 UE 5.4 的 `ClothingMeshUtils::SkinPhysicsMesh`，再按拓扑区间合并 2011 个组件空间目标。

三个 CPU 真值同时通过：Identity RefToLocal 恢复物理静止位置，最大误差 0.000015844 cm；注册到瞬态 World、强制参考姿态并初始化动画的真实 `USkeletalMeshComponent` 最大误差 0.000022987 cm；所有骨骼施加相同刚体变换时，与解析预期最大误差 0.000030533 cm。机器报告为 `chaos_cloth_skinning_validation.json`。

组件生命周期是硬门：未注册瞬态组件即使调用 `RefreshBoneTransforms`，Pose Cache 仍无效，曾得到 186.448151 cm 偏差。测试随后改为真实注册/初始化路径并通过；运行 Demo 不能绕过该生命周期。

GPU 持久会话新增 `UpdateKinematicTargets`，每个固定步捕获一份不可变目标快照。30 步合成姿态序列中，30 次目标更新全部接受且全部在 GPU 执行，最终版本和快照步号均为 30；固定粒子目标误差为 0，最小运动 10.145111 cm，动态粒子平均位移 9.257427 cm，最大边误差 1.144261 cm。机器报告为 `gpu_kinematic_targets_validation.json`。该合成门之外，真实 idle、Physics Asset 接触、Chaos 配对、12 模态、rank-one、NDB 和完整 CCD 已在运行地图闭环。

`/PhysicsContactCloth/Maps/L_GpuExplicitAssassin` 已把同一链路提升为真实运行 Case。注册的 Assassin 组件在 BeginPlay 显式播放 `Anim_Assassin_idle1`，实验组件以骨骼刷新为 Tick 前置条件；每个 1/60 秒固定步调用 UE Clothing 蒙皮，并从同一组件提取组件空间 Physics Asset 的 1 球/16 胶囊。D3D12 SM6 下目标、碰撞、12 模态、rank-one、NDB 与 CCD 均完成 300 步；固定边界最大运动 9.294522 cm、固定点误差 0、动态点平均位移 9.008298 cm、最大结构边误差 2.623925 cm、最大穿透 0.000085831 cm。机器报告为 `gpu_explicit_assassin_validation.json`，截图为 `Screenshots/M2_ExplicitAssassinCloth.png`。

碰撞另有独立故障注入门：在 2011 粒子真实拓扑中将一个动态探针放入 Assassin 分析球中心，输入穿透 9.0 cm；一次显式 GPU 固定步后穿透为 0。NDB、rank-one 与完整 CCD 均已有独立消融：rank-one 开启/关闭的 Assassin 300 步报告为 `gpu_explicit_assassin_runtime.json` 与 `gpu_explicit_assassin_rank_one_off_runtime.json`，机器汇总为 `gpu_explicit_rank_one_validation.json`。这仍不代表完整 Chaos Cloth 碰撞等价：摩擦与布料自碰撞尚未迁移。

第一版运行曾在 300/300 次目标应用完成后仍报告 0 cm 边界运动。修正包含两个不可省略的门：不依赖构造期序列化动画状态，而是在 BeginPlay 显式加载/播放；不以循环动画最后一帧代表整个窗口，而累计 300 步内最大已观察边界运动。现有 Case 已接 Physics Asset、同源步 Chaos 序列和完整 CCD；该段保留为失败记录，而不是当前能力边界。

### 同组件 Chaos / 研究求解器时间序列

`/PhysicsContactCloth/Maps/L_GpuExplicitAssassin` 现只使用一个注册的 Assassin `USkeletalMeshComponent`。它同时提供 idle 动画姿态、UE Clothing 蒙皮目标、组件空间 Physics Asset 以及 Chaos Cloth 输出，避免两个角色组件各自推进动画造成时钟偏差。运行合同固定为应用帧 `1/60 s`，并在 Case 生命周期内关闭 Chaos Cloth 时间步平滑、退出时恢复。

两份 `FClothSimulData` 通过各自 `ComponentRelativeTransform` 转到组件空间，再按 Clothing Asset 拓扑区间合并。独立自动化在 D3D12 SM6 下验证 2 资产、2011 粒子，组件空间往返最大误差 `0.000007644 cm`，并覆盖缺失资产拒绝路径。真实 300 步 GUI 运行得到 300 份 Chaos 快照和 75 份研究 GPU 异步读回，严格按源步 4、8、…、300 配对。

逐步数据位于 `gpu_explicit_assassin_comparison.csv/json`，包含两端动态点平均位移、平均/最大后端距离、最大结构误差和最大穿透。末步平均后端距离约 9.1164 cm，全程最大点距约 42.9133 cm；这些字段用于解释不同算法响应，不作为“应接近零”的误差门槛。运行报告、机器验证与 GUI 截图分别为 `gpu_explicit_assassin_runtime.json`、`gpu_explicit_assassin_validation.json` 和 `Screenshots/M2_ExplicitAssassinCloth.png`。

### 现场曲线与确定性输入重放

HUD 右侧直接消费内存中的 75 个配对样本和 300 个 timestamp 样本：上图画 Chaos/研究动态点平均位移，下图画完整固定步 GPU 时间。下图固定标出第 16 步预热边界和稳态 P95；超过稳态纵轴的启动样本以红色贴边段显示，并同时注明全程最大值，避免视觉裁剪造成误读。

`Scripts/Validation/capture_m2_gpu_explicit_replay.ps1` 会启动三个互不共享状态的 UE 5.4 D3D12 进程。前两次均以 `-PhysicsClothExplicitReplay` 从动画起点运行 300 个固定步，每步按位哈希 2011 个 UE Clothing 蒙皮目标及组件空间 1 球/16 胶囊；第二次把第一次哈希作为期望值。第三次故意改坏哈希末位，必须被作品运行门拒绝。当前稳定哈希为 `429e63168b51a679`，负例期望为 `429e63168b51a670`；正例研究端五项输出在 `1e-9` 容差内一致。机器报告为 `gpu_explicit_assassin_replay_validation.json`，正例、验证版与负例运行报告均保留，GUI 为 `Screenshots/M2_ExplicitAssassinReplay.png`。该合同证明研究输入可重复，不声称 Chaos 内部状态或 GPU 调度耗时逐位确定。

### 玩家交互黄金路径与弱障碍失败门

`L_GpuExplicitAssassin` 的输入不依赖编辑器蓝图临时绑定：`Space/N/R/B/D/F/E` 分别对应协同暂停、安全单步、完整重置、实际显示后端切换、Debug Draw、标准/弱障碍配置切换和手动导出。暂停范围明确包含共享实验时钟、Assassin 动画和 Chaos Cloth；单步等待下一次自然 Skeletal Mesh Tick，避免重入 Chaos Cloth Task。`B` 会真实改变渲染几何来源，后台仍保留双后端同步运行以维持配对曲线。

`-PhysicsClothExplicitInteractionScript` 在真实 UE 5.4 D3D12 GUI 进程中执行 1 次重置、2 次暂停切换、1 次单步、3 次后端切换和 2 次调试切换。重置后报告仍恰好包含 300 个目标应用、300 个碰撞应用、300 个 GPU timestamp 与 75 个同源对照样本；截图为 `Screenshots/M2_ExplicitAssassinInteractive.png`，机器验证为 `gpu_explicit_assassin_interactive_validation.json`。

`-PhysicsClothExplicitFailureWeakBarrier` 只把 NDB 基础权重由 `10` 改为 `0.05`，保留相同输入哈希 `429e63168b51a679`、rank-one 和完整 CCD。标准配置最大穿透为 `0.0000858 cm`，弱障碍配置为 `0.0486479 cm`，达到既定 `>=0.001 cm` 的预期失败观察门；最大结构边误差仍为有限的 `2.21882 cm`。截图为 `Screenshots/M2_ExplicitAssassinFailureWeakBarrier.png`，机器验证为 `gpu_explicit_assassin_failure_weak_barrier_validation.json`。报告的 `result=passed` 只表示基础设施和失败观察合同成立，不把穿透结果冒充正常质量通过。

一键捕获入口为 `Scripts/Validation/capture_m2_gpu_explicit_interaction.ps1`，它分别启动交互与失败两个隔离进程，并在新鲜度、截图大小、计数、输入一致性和质量阈值全部通过后才生成验证报告。

### 60 秒黄金演示路径

1. `0–10 秒`：进入 `/PhysicsContactCloth/Maps/L_GpuExplicitAssassin`，指出两路共享同一 Assassin Skeletal Mesh、动画、2011 粒子 Clothing 拓扑和 1 球/16 胶囊 Physics Asset 输入；左路是 Chaos Cloth，右路是研究 GPU 后端。
2. `10–25 秒`：观察 300 步同源 A/B 曲线，切换 `B` 让实际显示几何在两路间变化；解释不同求解器不要求数值逐帧相等，比较的是同输入下的结构、接触和成本响应。
3. `25–38 秒`：暂停、自然 Tick 单步并恢复，打开 Debug Draw；指认 12 模态、rank-one、NDB、局部 Jacobi 与完整 CCD 在每个固定步的职责。
4. `38–50 秒`：按 `F` 切换弱障碍。唯一变量是 NDB 基础权重 `10 → 0.05`，同一输入哈希下最大穿透从 `0.0000858 cm` 增至 `0.0486479 cm`；明确这是预期失败被观测，不是质量通过。
5. `50–60 秒`：展示稳定输入哈希 `429e63168b51a679`、284 个稳态 GPU 样本的 P95/P99 `5.110/5.197 ms` 与 CSV/JSON 导出；收尾归因 Chaos/UE Clothing/Physics Asset 与个人 RDG 求解、量测和失败实验。

作品级总门由 `Scripts/Validation/validate_m2_case01_acceptance.ps1` 聚合算法基准、D3D12 回归、最终地图、Chaos 同输入、交互、重放、性能、失败场景、截图和归因，结果写入 `case01_acceptance_validation.json`。

### 外层完整 CCD 边界锚点

`FContactClothContinuousCollision` 已为当前 Physics Asset 分析表示提供点对球、点对胶囊的完整线段扫掠，并返回最早 TOI、命中位置、法线和初始重叠状态。解析胶囊同时覆盖柱段、两端球帽与退化球。

自动化专门使用“起点和终点都在碰撞体外、运动中途穿过”的离散漏检案例：球和胶囊柱段 TOI 均为 0.45，胶囊端帽 TOI 为 0.625；还覆盖平行近失、初始重叠和 TOI 前安全锚点。机器报告 `complete_ccd_validation.json` 为 8/8 通过。

完整 CCD 已放在每个 GPU 固定步的局部 Jacobi 之后，使用步初位置到候选位置的最早球/胶囊 TOI，并加入 0.01 cm 安全偏移。D3D12 集成反例中，离散端点投影让探针从 `z=100` 穿到 `z=-100.000`，完整 CCD 将其截停在球体入射面 `z=10.010`；真实地图完成 300 次 CCD Pass。

该锚点只覆盖点对 UE Physics Asset 球/胶囊，不等于布料边/三角形自碰撞 CCD，也尚未统计每步命中数与修正幅度。机器报告为 `gpu_hybrid_collision_validation.json`。

### GPU 可视化运行

- 地图：`/PhysicsContactCloth/Maps/L_GpuJacobi_Validation`
- 画面：橙色为带中心扰动的输入网格，青色为 96 次 Jacobi 后的 GPU 输出，红色为固定顶边。
- 真实运行：192 粒子，最大结构边误差约从 64.217 cm 降至 0.195 cm，降幅约 99.70%。
- 共享实验协议：导出 `InputMaximumEdgeError`、`OutputMaximumEdgeError`、`ErrorReductionRatio`、`BlockingWallTime` 四项 JSON/CSV 指标。
- 机器校验：`gpu_jacobi_visual_validation.json`，14/14 通过。
- GUI：`Screenshots/M2_GpuJacobiValidation.png`，1280×720；可见中文状态、GPU 回读、误差对比、耗时和当前架构边界。
