# M4 / Case 03 验证证据

## CPU Flow Map 首切片

- 自动化：`Automation/index.json`，当前完整 `PhysicsResearch.FlowMap.` 15/15 成功；本节四个基础测试均在其中，机器汇总：`flow_map_foundation_validation.json`。
- 直槽合同：16×8 MAC 网格，`U=17×8`、`V=16×9`，每格 2×2 粒子，共 512 粒子。20 cm/s 均匀速度运行 6 个 1/60 秒固定步，解析位移 2 cm、散度 RMS 0、横向漂移 0、粒子丢失 0；短 Map 每 3 步重置两次，长 Map 尚未重置。
- 旋涡合同：32×32 MAC 网格、每格 1 粒子，共 1024 粒子；解析刚体旋涡在周期域运行 60 个 1/120 秒固定步。散度 RMS 0，探针轨道半径相对误差 `9.86e-7`，backward-map Jacobian 行列式最大误差 `3.47e-4`，粒子丢失 0；116 次周期回卷被独立统计，不混入丢失指标。
- PIC 传输：32×16 网格、每格 2×2 粒子，共 2048 粒子；归一化双线性 P2G 对 `(30,-7) cm/s` 均匀速度的最大重建误差为 0，并保持散度 RMS 0。
- 压力投影：同一周期 MAC 网格上的 50 cm/s 纯压缩波以 1024 次 CPU Jacobi 建立真值路径。散度 RMS 从 `1.38617` 降到 `7.08785e-5`，比值 `5.11e-5`；最大散度从 `1.95090` 降到 `1.08035e-4`，压缩动能代理从 `618.995` 降到 `1.607e-6`。
- 梯度增强传输：32×32 周期域使用每格 1 个确定性抖动粒子传输 `sin(2x)cos(2y)` 标量分量。普通 PIC RMS 为 `0.036025`，加入粒子梯度的一阶 Taylor 项后为 `0.011289`，误差比 `0.3134`。该标量分量门验证论文 impulse 梯度 P2G 的数据数学，不冒充完整向量 impulse 演化。
- 首轮失败：旋涡最初错误使用方形 Clamp 域，角落粒子累计越界 3176 次。轨道半径和散度已经正确，但硬钳位会污染旋涡；正式基准明确改为 Periodic，直槽继续使用 Clamp。
- 梯度门首轮失败：规则 2×2 对称粒子让普通 PIC 的一阶误差自行抵消，梯度/PIC 误差比为 `0.994`，无法证明梯度项贡献。正式门改用确定性畸变粒子而非放宽阈值，误差比降至 `0.313`。
- 压力门首轮只用 512 次 Jacobi，散度降低 99.286%，没有达到既定 99.9% 门；未放宽阈值，正式 CPU 真值预算增至 1024 次后通过。该迭代数不是实时预算，后续 GPU 路径需要独立的收敛/成本消融。
- 边界：基础门证明 MAC 布局、粒子轨迹、RK2、双 Map 生命周期、Jacobian、归一化 PIC P2G、周期压力投影、分量式梯度增强 P2G 与边界语义；后续章节已补齐 CPU 向量闭环和首个 GPU 压力真值门，但 UE 运动障碍地图与 Niagara 展示仍未完成，不能据此宣告 Case 03 完成。

验证入口：

```powershell
Scripts/Validation/validate_m4_flow_map_foundation.ps1
Scripts/Validation/validate_m4_scalar_transport_ablation.ps1
Scripts/Validation/validate_m4_impulse_vortex.ps1
Scripts/Validation/validate_m4_gpu_pressure.ps1
Scripts/Validation/validate_m4_gpu_pressure_session.ps1
Scripts/Validation/validate_m4_gpu_particles.ps1
Scripts/Validation/validate_m4_gpu_impulse_transfer.ps1
Scripts/Validation/validate_m4_gpu_closed_loop.ps1
Scripts/Validation/validate_m4_gpu_map_reset.ps1
Scripts/Validation/validate_m4_gpu_timestamp.ps1
Scripts/Validation/validate_m4_solid_boundary_raster.ps1
Scripts/Validation/validate_m4_gpu_solid_boundary.ps1
```

## 120 步演化态 PIC / Long Map 消融

- 自动化：`ScalarTransportAutomation/index.json`；机器汇总：`scalar_transport_ablation_validation.json`；完整 `PhysicsResearch.FlowMap.` 回归现为 15/15。
- 公平合同：三条路径共享 32×32 周期网格、4096 粒子、120 个 1/60 秒固定步、`(20,10) cm/s` 平移速度和 `sin(2x)cos(2y)` 解析标量。Short PIC 每步重采样 120 次；LongMapPic 与 LongMapGradient 都每 20 步重置，共 6 次。
- 重置周期贡献：Short PIC RMS `0.498927`，相同无梯度 P2G 的 LongMapPic 为 `0.123740`。
- 梯度贡献：相同 6 次重置下，LongMapGradient RMS 为 `0.007644`，相对 LongMapPic 的误差比 `0.0618`，相对 Short PIC 为 `0.0153`。
- 耗散表现：Short PIC 相对 RMS `99.79%`、幅值仅 `0.00207`、能量代理 `1.15e-6`；LongMapGradient 相对 RMS `1.53%`、幅值 `0.9767`、能量代理 `0.2577`，解析能量为 `0.25`。
- 语义：Short PIC 不是“失败的代码”，而是频繁有损 P2G/G2P 的可解释耗散基线；LongMapPic 隔离少重置的贡献；LongMapGradient 再隔离梯度项。当前仍是给定均匀速度下的被动标量/impulse 分量，不是向量 impulse 反向驱动压力速度场的完整 PFM。

## Taylor–Green 向量 impulse / 压力闭环

- 自动化：`ImpulseVortexAutomation/index.json`；机器汇总：`impulse_vortex_validation.json`；完整 `PhysicsResearch.FlowMap.` 回归现为 15/15。
- 闭环：4096 粒子由当前非均匀 MAC 速度 RK2 推进，保存 reference impulse 与 backward-map Jacobian；每步将 `Bᵀm₀` 归一化回写到 MAC 面速度，再执行 128 次周期压力 Jacobi。两路径都运行 120 个 1/240 秒固定步和 120 次压力投影。
- 公平 A/B：Short PIC 每步从压力投影后的速度重置粒子 Map，共 120 次；Long Map 每 20 步重置，共 6 次。网格、粒子、固定步、初始 Taylor–Green 旋涡和压力预算全部相同。
- 结果：Short PIC / LongMapPic / LongMapGradient 速度 RMS 为 `11.1807 / 1.03065 / 0.017909 cm/s`。少重置先把误差降至 Short 的 `9.22%`；相同 6 次重置下，向量梯度项再把误差降至 LongMapPic 的 `1.74%`。三者相对 RMS 为 `79.06% / 7.29% / 0.1266%`。
- 动能：初始 `99.0393`，三路径最终为 `4.3853 / 85.9592 / 100.2291`。梯度路径只超调约 1.20%，因此通过 ±2% 能量门，但不写成严格守恒。
- 不可压缩门：三路径散度 RMS 为 `9.41e-7 / 3.07e-4 / 3.62e-4`；梯度路径最大散度 `7.14e-4`，全部低于 `1e-3`。梯度 Long Map Jacobian 行列式最大误差 `1.84e-4`。
- 边界：该结果形成“非均匀速度推进粒子 → `Bᵀm₀` 与 `BᵀG₀B` 一阶重建 → 压力投影 → 新速度”的 CPU 真闭环。梯度变换忽略 `B` 的空间导数，即论文测试后舍弃的 Hessian 类高阶项；Jacobian 也是一阶更新。下一节只迁移了压力子系统，完整 GPU Flow Map、移动固体和 3D 尚未实现。

## D3D12 RDG 压力投影真值门

- 自动化：`GpuPressureD3D12Automation/index.json`；机器汇总：`gpu_pressure_validation.json`；运行设备为 RTX 4080、D3D12 SM6，隔离测试 1/1 成功。
- 公平合同：CPU 与 GPU 共用 32×16 周期 MAC 压缩波、5 cm 网格间距和 512 次 Jacobi 预算。RDG 精确执行 1 次散度、512 次 ping-pong Jacobi 和 2 次 U/V 压力梯度回写。
- 数值结果：初始散度 RMS `1.38617175`；CPU/GPU 投影后分别为 `0.00988997345 / 0.00989034952`，差值 `3.76e-7`。CPU/GPU 所有面速度最大误差 `3.60012e-5 cm/s`，低于 `1e-4` 门；GPU 消除了 99.2865% 初始散度。
- 工程语义：全局 shader 在常驻 `PhysicsResearchShaders` 模块注册，Case Game Feature 通过公开调度合同使用；输入、RDG Buffer、UAV/SRV ping-pong、读回和 Pass 计数均被测试覆盖。
- 边界：阻塞式 Readback 只用于离线真值验证，不是逐帧运行架构，也没有 GPU timestamp，因此不声称性能收益。下一步需要持久异步资源、完整粒子/Map GPU 闭环和 UE/Chaos 运动障碍作品地图。

## 持久、非阻塞压力会话

- 自动化：`GpuPressureSessionD3D12Automation/index.json`；机器汇总：`gpu_pressure_session_validation.json`；RTX 4080 / D3D12 SM6 隔离测试 1/1 成功。
- 生命周期：FaceU/FaceV、散度与两份压力工作 Buffer 均转换为跨步外部 RDG 资源。游戏线程连续排入 4 步，运行入口不调用 Flush 或 GPU Idle；U/V 使用成对异步 Readback，并以源步号标记快照。
- 精确计数：4 步全部排入并执行，最终快照严格对应第 4 步；完成 2 次读回，Pass 累计为散度 4、Jacobi 512、Apply 8。
- 连续数值门：四次 `128` 迭代 CPU 投影与四个持久 GPU 步共享输入和顺序；最终 CPU/GPU 散度 RMS 为 `0.00989025984 / 0.00989027046`，所有面速度最大误差 `5.30905e-6 cm/s`。
- 边界：自动化结束时才调用阻塞等待来固定最终证据；正常排队、周期读回与 Poll 路径没有等待。尚未加入 GPU timestamp，也尚未把粒子、Map/Jacobian 和 impulse/梯度纳入持久会话。

## 持久 GPU 粒子与 Backward Map

- 自动化：`GpuParticleD3D12Automation/index.json`；机器汇总：`gpu_particle_validation.json`；RTX 4080 / D3D12 SM6 隔离测试 1/1 成功。
- 合同：32×32 周期 Taylor–Green MAC 场、1024 个确定性偏移粒子、`1/240 s` 固定步。GPU 每步用 staggered U/V 双线性采样做 RK2，并以同一中点速度梯度更新每粒子的 `2×2` backward-map Jacobian。
- 生命周期：粒子位置/Jacobian 使用两组持久 ping-pong Buffer，连续排入并执行 20 步，每步恰好 1 个平流 Pass；成对异步快照最终严格对应第 20 步。
- 数值门：相同顺序的 CPU 参考与 GPU 最终位置最大误差 `3.81470e-6 cm`，Jacobian 分量最大误差 `8.34465e-7`，行列式差 `8.34465e-7`。
- 首轮失败：域内粒子也无条件执行周期 `fmod` 时，CPU/HLSL 取模差累计出 `5.11568e-4 cm` 位置误差，未达到 `1e-4 cm` 门。正式实现只在真实越界时回卷，阈值未放宽。
- 边界：当前速度场仍是静态输入；尚未把 impulse/梯度载荷、P2G、压力反馈、Map 重置或 Chaos 障碍合并到同一会话，也没有 GPU timestamp。

## GPU Impulse / 梯度 P2G 真值门

- 自动化：`GpuImpulseD3D12Automation/index.json`；机器汇总：`gpu_impulse_transfer_validation.json`；RTX 4080 / D3D12 SM6 隔离测试 1/1 成功。
- 合同：16×16 周期 MAC 网格、256 个确定性畸变粒子；粒子携带 reference impulse、完整一阶空间梯度与非单位 backward Jacobian。普通与梯度模式共享所有数据，只切换 Taylor 梯度项。
- GPU 数学：先计算 `m=Bᵀm₀`；梯度模式再计算 `G=BᵀG₀B`，在每个逻辑 U/V 面位置使用 `m+G(x_face-x_particle)`。每个 MAC 面主动 Gather 贡献，避免浮点 Atomic 顺序影响真值门。
- 数值结果：普通 impulse P2G 的 CPU/GPU 最大面误差 `2.86102e-6 cm/s`；梯度路径为 `5.72205e-6 cm/s`，两者均低于 `1e-4` 门。每种模式精确执行 U/V 两个 Gather Pass。
- 边界：这是阻塞数学验证，`O(face×particle)` 的确定性 Gather 适合作品当前规模和参考门，不宣称是大网格最终性能架构；尚未与持久平流及压力 Buffer 串联。

## 持久 GPU Flow Map 统一闭环

- 自动化：`GpuClosedLoopD3D12Automation/index.json`；机器汇总：`gpu_closed_loop_validation.json`；RTX 4080 / D3D12 SM6 隔离测试 1/1 成功。
- 单步顺序：同一个持久会话、同一次 RDG 排队内执行“当前投影速度推进粒子/Jacobian → `Bᵀm₀/BᵀG₀B` Gather 到 MAC 面 → 散度 → Pressure Jacobi → U/V 梯度回写”。投影后的面速度直接成为下一次排队输入。
- 精确计数：16×16、256 粒子运行 4 步；平流/Gather/散度/Jacobi/Apply 分别为 `4/8/4/256/8`。速度 U/V 与粒子位置/Jacobian 四份 Buffer 成对异步发布，最终快照严格对应第 4 步。
- CPU 对照：同输入 LongMapGradient CPU 闭环散度 RMS `1.65896e-6`，GPU 为 `1.66251e-6`；所有面速度最大误差 `1.83582e-5 cm/s`。GPU Jacobian 行列式最大偏差 `1.60635e-4`。
- 运行语义：正常 Enqueue 内没有 Flush 或 GPU Idle；验证结束才阻塞收取最终证据。该门首次证明 GPU 上的新压力速度会反馈给下一步粒子，而非三个独立子系统并排运行。
- 边界：该 4 步门本身没有跨越重置边界；后续章节已独立验证 GPU Map Reset 和 timestamp 初始机制。移动固体边界、UE 运行 Actor、表现与玩家交互仍未完成。

## GPU Long Map Reset 生命周期

- 自动化：`GpuMapResetD3D12Automation/index.json`；机器汇总：`gpu_map_reset_validation.json`；RTX 4080 / D3D12 SM6 隔离测试 1/1 成功。
- 同图顺序：每逢第 4 步，先完成粒子平流、Impulse Gather 和压力 Apply，再由 `ResetParticleMapsCS` 从最终投影 U/V 采样新 `m₀/G₀`，并把 backward-map Jacobian 原位设为单位阵。
- Ping-pong 合同：普通步写 Work 并在执行后交换；重置步写回 Current 且不交换。该步四 Buffer 异步快照读取重置后的 Current，而不是平流临时 Buffer。
- 两周期证据：16×16、256 粒子运行 8 步，CPU/GPU 都精确重置 2 次；平流/Gather/散度/Jacobi/Apply/Reset Pass 为 `8/16/8/512/16/2`，最终快照严格对应第 8 步。
- 数值结果：CPU/GPU 散度 RMS 为 `1.60987e-6 / 1.60001e-6`，所有面速度最大误差 `2.00272e-5 cm/s`；第二次重置后 Jacobian 单位阵分量误差和行列式误差均为 0。
- 边界：该门证明重置数学、RDG 排序、payload 更新和持久 Buffer 归属，不是性能证据。下一节已建立 timestamp 初始机制；移动固体边界、UE 运行 Actor、Niagara 表现、交互、失败消融、截图与长序列性能证据仍待建立。

## 非阻塞 GPU Timestamp 初始门

- 自动化：`GpuTimestampD3D12Automation/index.json`；机器汇总：`gpu_timestamp_validation.json`；RTX 4080 / D3D12 SM6 隔离测试 1/1 成功。
- 计时合同：8 组可复用 `RQT_AbsoluteTime` 查询包围完整固定步 RDG 区间；结果查询使用非阻塞模式，槽位占满时不等待 GPU，而是增加 skipped 计数。
- 初始证据：连续排入 32 步，固定步完成数仍为 32；8 个时间样本完成、24 步因槽满显式跳过，二者之和精确覆盖全部固定步。预热 4 步后保留 4 个稳态样本。
- 观测值：全样本均值/P95/P99/最大值为 `4.346/6.347/6.347/6.347 ms`；稳态为 `4.336/6.347/6.347/6.347 ms`。这是本机初始机制读数，不是最终性能预算。
- 边界：快速批量排队会先填满 8 槽，因此当前稳态样本太少，不能作为作品级性能分布。后续 UE 运行地图必须按真实帧长时间采集并导出逐步 CSV。计时区间覆盖完整 RDG 步和当步计划的读回复制，不冒充单 kernel 时间。

## UE / Chaos 运动圆到 MAC 边界数据

- 自动化：`SolidBoundaryAutomation/index.json`；机器汇总：`solid_boundary_raster_validation.json`；隔离测试 1/1、完整 Flow Map 回归 15/15 成功。
- 刚体适配：输入 UE/Chaos 刚体前后 `FTransform`、物理半径和固定步，使用位置差分得到平面线速度，并按 UE Z/Yaw 正方向得到角速度；非法零固定步在栅格化前拒绝。
- Staggered 合同：Cell 中心生成固体占用，U/V 面分别生成独立掩码与固体面速度。面速度使用 `v_body + ω×r`，不把 Cell 掩码错误复用到 staggered face。
- 数值证据：16×16、5 cm 网格中，半径 12 cm 的运动圆覆盖 16 个 Cell，并把所有与固体 Cell 相邻的界面纳入 20 个 U 面和 20 个 V 面。`(-2→0) cm / 0.1 s` 恢复 `20 cm/s`，`6° / 0.1 s` 恢复 `1.04719758 rad/s`；全部固体面无滑移解析残差为 0。
- 边界：这是 CPU 真值与 Chaos pose adapter；下一节已让持久 GPU 速度/压力路径消费该数据。运行 Actor、真实 Chaos Body 生命周期和粒子防穿越仍未完成。

## 持久 GPU 固体边界消费

- 自动化：`GpuSolidBoundaryD3D12Automation/index.json`；机器汇总：`gpu_solid_boundary_validation.json`；RTX 4080 / D3D12 SM6 隔离测试 1/1 成功。
- 版本合同：游戏线程连续提交两份由不同 UE/Chaos Pose 生成的 Cell/U/V 掩码与固体面速度；两个 GPU Enqueue 各自捕获一致数组和版本，最终统计为更新 2、应用 2、最新版本 2。
- 求解顺序：P2G 后的散度在固体面改读刚体速度；固体 Cell 不进入压力未知量，流体邻域使用固体感知 Jacobi；U/V Apply 后再次将所有固体面钉回 `v_body+ω×r`。
- 数值结果：第二版边界包含 16 个固体 Cell、20 个 U 面和 21 个 V 面；最终快照固体面无滑移最大残差为 0。256 次 Jacobi 后，流体 Cell 散度 RMS `0.0190895`、最大值 `0.0194440`，均低于 `0.05` 门。
- 粒子边界：同一版本还上传 1 个解析碰撞圆，两个固定步各执行一次平流后投影。最终 256 个粒子中固体内部数量为 0，相对半径 12 cm 物理表面的最小间隙 `0.0499992 cm`，对应 `0.05 cm` 安全壳。
- 退化门：五份全零掩码继续通过原有 Taylor–Green CPU/GPU 对照，最大面速度误差 `1.81198e-5 cm/s`，证明新增分支没有替换或破坏周期基线。
- 边界：速度、压力和粒子平流已经消费同一运动固体版本。当前最多支持 8 个解析圆；粒子接触投影尚未逐接触重置 Map Jacobian。测试仍由 Pose 数组驱动，而非地图中的 Live Chaos Body；运行 Actor、表现、交互、失败消融和截图仍待完成。

## UE 地图 / Live Chaos / D3D12 实时闭环

- 地图：`/PhysicsFlowMap/Maps/L_FlowMapChaosObstacle`；生成报告 `flowmap_chaos_map_setup.json`。地图使用专用中文 HUD/GameMode，并放置一个 `AFlowMapRealtimeActor`。
- Chaos 联动：Actor 内的球形 `UStaticMeshComponent` 真正启用 Chaos 模拟。每个共享 `UPhysicsExperimentComponent` 固定步读取刚体前后 Pose，在 Actor 局部域恢复线/角速度，栅格化独立 Cell/U/V 掩码和 no-slip 面速度，再把同一版本提交给 GPU 压力路径与粒子碰撞圆。
- 作品 HUD：直接显示固定步、GPU 执行步、快照步、Chaos Pose 数、边界提交/应用/版本、固体 Cell/U/V 面、粒子投影 Pass、压力 Pass、非阻塞 GPU 时间和坏快照拒绝数。
- GPU 数据可视化：576 个青色粒子直接使用异步读回位置；144 个紫色短箭头从同一 GPU 快照的 staggered FaceU/FaceV 重建 cell-centered 速度。箭头不是另跑一套 CPU 场，也不使用预制动画。HUD同步显示最大速度和触及速度预算的 MAC 面数。
- 压力势热图：异步快照增加最终 Jacobi Pressure Buffer，不从速度图伪造压力。576 个 Cell 按同帧最大绝对值归一化：蓝色为负、深色为近零、红色为正，厚度表达相对幅值。截图第 298 快照范围为 `[-205.229, 90.352]`，显著负/正 Cell 为 `127 / 142`。这里明确称“压力势”，不冒充带密度和真实单位换算的 Pa。
- 压力读回真值门：D3D12 `PhysicsResearch.FlowMap.GpuClosedLoop*` 4/4 成功；统一闭环和运动固体测试显式断言压力数组严格为 `Nx×Ny` 且所有值有限。压力读回与 U/V、粒子 A/B 共用同一个源步号和成组 readiness 门，不允许 UI 混用不同固定步。报告：`PressureReadbackD3D12Automation/index.json`。
- D3D12 运行门：交互重置后 389 步执行和 389 次边界应用全部完成，最新边界版本也严格为 389；异步读回 193 次，最新可视快照步 386，坏快照拒绝 0。371 个 GPU 计时样本的稳态平均完整步时间为 `0.708911 ms`。机器报告：`flowmap_realtime_d3d12_validation.json`。
- 交互生命周期：自动脚本实跑空格暂停/继续 2 次、N 单步 1 次、R 全链路重置 1 次、D 调试切换 2 次、E 指标导出 1 次。重置会一起销毁并重建 GPU 会话、清零边界版本/快照统计、复位 Chaos Body 和实验固定步，而不是只清 HUD 数字；导出生成 JSON 与 CSV。
- 稳定性修复：首轮真实帧运行在约第 51 帧暴露粒子 NaN，污染 ISM Bounds 并触发 Renderer ensure。正式实现没有隐藏 ensure：GPU 平流对速度使用基于网格/固定步的 CFL 上限，对非有限向量/Map 做恢复；游戏线程在更新实例前再做有限值和域范围检查。修复后跨越原失败点运行，Renderer/RHI/Shader 错误为 0。
- GUI 首轮失败与修复：第一次 1920×1080 窗口截图暴露 Spectator 默认视点贴近球体，且 Pressure Apply 后的 MAC 速度达到 `3467479.25 cm/s`。正式路径每局把玩家视点显式交给实验 Camera；Pressure Apply 的 U/V 最终出口增加可配置速度预算，Case03 使用 `120 cm/s`，Long Map Reset 周期设为 8 步。新截图第 178 快照最大速度 `113.04 cm/s`、触限面 0，不是靠持续硬钳位维持画面。
- GUI 证据：`Screenshots/Case03_FlowMap_LiveChaos.png` 来自 UE 5.4.4 窗口化 D3D12 游戏运行，包含中文 HUD、完整模拟域、Chaos 球体、576 粒子和 144 个速度箭头；机器说明 `flowmap_visual_gui_validation.json`。人工查看确认相机、颜色分层、域范围和数据文字均可辨识。
- 截图生命周期失败：加入压力热图后的第一张截图在第 180 步仍被 Lyra 额外 2 秒纹理流送 Loading Screen 遮住。正式捕获条件后移到已完成第 300 个 GPU 快照后；最终证据来自实验步 302 / GPU 步 301 / 快照步 298，不把 Loading Screen 当 GUI 验证。
- 失败消融：`-PhysicsResearchFlowMapVelocityBudgetAblation` 共用同一地图、Chaos Body、GPU kernels 和读回，只把速度预算放宽到 `1e9 cm/s`、Map Reset 从 8 延长到 20 步、初始旋涡从 55 提高到 80 cm/s。240 步稳定/失败最大速度为 `87.096 / 3174263.25 cm/s`，放大 `36445.53×`；最大绝对压力势放大 `109626.52×`。失败路径无 NaN、Renderer 也不崩溃，但数值门明确为失败，证明有限值保护不等于算法成功。机器报告 `flowmap_velocity_budget_ablation_validation.json`。
- 回归：`RealtimeRegressionAutomation/index.json` 为完整 `PhysicsResearch.FlowMap.` 15/15；验证入口 `Scripts/Validation/validate_m4_flowmap_realtime.ps1`。
- 边界：当前视觉使用 576 个 GPU 快照驱动的 ISM 粒子、144 个同源 MAC 速度箭头和 576 Cell 压力势热图；失败消融与真实 GUI 截图已经完成。后续章节又补齐同地图 Short Map/PFM 真后端切换、低中高预算和作品级长序列逐步性能 CSV。Niagara 流线属于可选表现增强，不替代或阻塞 Case03 完成门。

## Short Map / PFM 真后端与低中高预算

- 共享核心新增 `NumericalBaseline` 枚举，专门表示“非 Chaos 的数值基线”；Flow Map 不再被迫把 Short PIC/Short Map 错标成 `ChaosBaseline`。Chaos 在六条路径中都只承担真实运动刚体边界，并以 `ChaosRigidBoundary` 单独显示。
- `B` 键会销毁当前 GPU 会话、把实验后端切到 `ShortMapBaseline` 或 `ParticleFlowMapResearch`、复位 Chaos Body/固定步/指标，再按同一初态重建。Short 路径使用 `MapResetSteps=1` 且 `gradient=false`；PFM 使用 `MapResetSteps=8` 且 `gradient=true`，不是 HUD 标签切换。
- `C` 键循环低/中/高档。三档保持相同 240×240 cm 物理域，仅把网格设为 16²/24²/32²、Cell 设为 15/10/7.5 cm、压力迭代设为 32/64/96；粒子数为 256/576/1024，速度箭头为 64/144/256。首版低档错误缩小物理域，Chaos 圆会把粒子合法推出到 `x=-83.8265 cm` 并被域门拒绝；正式方案改为同物理域，不放宽快照阈值。
- 六条独立 UE 5.4.4 D3D12 SM6 运行均完成至少 121 个 GPU 步，边界应用数/版本与步数严格相等，拒绝快照和关键 Renderer/RHI/Shader 错误均为 0。Short 三档 Map Reset 为 121 次；PFM 三档为 15 次。

| 后端 | 档位 | 稳态样本 | 平均 / P95 / P99 GPU 完整步 ms |
| --- | --- | ---: | ---: |
| Short Map | 低 | 103 | 0.369 / 0.453 / 0.507 |
| Short Map | 中 | 103 | 0.722 / 0.936 / 1.116 |
| Short Map | 高 | 104 | 1.108 / 1.386 / 1.544 |
| PFM Gradient | 低 | 105 | 0.377 / 0.507 / 0.693 |
| PFM Gradient | 中 | 103 | 0.729 / 1.009 / 1.028 |
| PFM Gradient | 高 | 103 | 1.117 / 1.388 / 1.416 |

- 单进程交互门从 PFM 中档开始，实跑 `PFM 中 → Short 中 → Short 高 → PFM 高`，完成两次后端切换、一次预算切换和三次全链路重建；最终 PFM 高档再完成 121 步，P95/P99 为 `1.388/1.405 ms`，拒绝快照 0。
- 机器报告：`flowmap_backend_budget_validation.json`、`flowmap_backend_budget_interaction_validation.json`；回归：`BackendBudgetRegressionAutomation/index.json` 为 15/15。当前每条只有约百个稳态样本，足以验证档位和分布字段，但不替代后续作品级长序列逐步 CSV。

## 1200 步逐步 GPU 长序列

- Short Map 与 PFM 中档分别在独立 UE 5.4.4 D3D12 SM6 进程中运行 1201 步；两端均使用 24² 网格、240×240 cm 物理域、64 次压力迭代、同一个 Live Chaos 圆和 16 步预热。Short Map 重置 1201 次，PFM 重置 150 次。
- 每条保存 1199 个源步/timestamp 对，其中 1183 个属于稳态，查询槽跳过 0、拒绝快照 0、Chaos 边界应用数与执行步数严格相等。GPU 查询按就绪顺序完成，第一版导出的 CSV 因而偶尔出现 `113,112`；正式导出按源步号排序，不改变查询或时间值，验证门要求严格递增。
- `capture_m4_flowmap_long_sequence.ps1` 从 CSV 独立重算平均、Nearest-Rank P95/P99 和最大值，再与运行时 JSON 对照到 `1e-6 ms`。Short Map 为 `0.7461 / 1.075 / 1.178 / 1.353 ms`；PFM 为 `0.7357 / 1.053 / 1.159 / 1.312 ms`。
- 这组数据证明非阻塞时间序列、稳定分布和两条权威路径的相同协议，不宣称约 1.4% 的本机均值差是普适加速。论文相对 Neural Flow Maps 的 10–20 倍结论仍不复用。
- 原始证据：`LongSequence/FlowMapLongSequence_ShortMapBaseline_Medium.csv/json`、`LongSequence/FlowMapLongSequence_ParticleFlowMapResearch_Medium.csv/json`；机器汇总：`flowmap_long_sequence_validation.json`。

## 标准基准与作品场景映射

Case03 不把一个好看的漩涡画面当作全部证据。以下五组场景覆盖从解析解、CPU/GPU 对照到 UE 运行态的递进关系，其中前三组已经超过“至少三个标准基准”的作品门：

| 层级 | 场景 | 主要证明 | 关键证据 |
| --- | --- | --- | --- |
| 数值基准 1 | 16×8 直槽均匀流 | MAC 布局、解析位移、零散度、Map 生命周期 | 512 粒子、6 步、位移 2 cm、散度 RMS 0 |
| 数值基准 2 | 32×32 标量平移与 Taylor–Green 旋涡 | Short/Long/Gradient 的耗散拆分、向量 impulse、压力反馈 | 120 步 A/B，速度 RMS `11.1807/1.03065/0.017909 cm/s` |
| 数值基准 3 | 32×16 CPU/GPU 压缩波压力投影 | RDG shader 数学与 CPU 真值一致，而非只证明“能运行” | 最大面误差 `3.60012e-5 cm/s` |
| UE 集成基准 | Live Chaos 运动圆 | Chaos Pose、固定步、边界栅格、GPU 速度/压力/粒子消费同一版本 | 边界版本逐步一致、固体面 no-slip、粒子不留在固体内 |
| 作品场景 | `L_FlowMapChaosObstacle` | PFM/Short 真切换、预算切换、压力热图、交互、长序列性能 | 中文 HUD、三态截图、1200 步 CSV、失败消融 |

这里的“最终作品场景”是研究型可视化场景，不伪装成完整关卡或商品化流体工具。它刻意让权威物理状态、Chaos 边界和指标同时可见；未使用 Assassin、Dragon 或 ALS 资产，也没有用外部美术包掩盖算法证据。

## 作品集三态截图

- `Case03_FlowMap_ShortMapBaseline_Medium.png`：中档 Short Map，HUD 明示每步重置、梯度关闭和 `ChaosRigidBoundary` 持续启用。
- `Case03_FlowMap_ParticleFlowMapResearch_High.png`：高档 PFM，HUD 明示 32²/1024 粒子、8 步重置、梯度开启和更高压力预算。
- `Case03_FlowMap_FailureVelocityBudget.png`：相同地图的速度预算失败消融；画面与 HUD 同时保留数值爆炸证据，运行门按预期为失败而进程保持健壮。
- 三次 UE 5.4.4 D3D12 采集均执行 361 步以上、坏快照 0，图片均大于 750 KB；机器报告 `flowmap_portfolio_states_validation.json`。
- 高档首张图在第 300 步捕获时出现 HUD 字形未完全驻留/边缘裁切，未作为最终证据。捕获点后移到第 450 快照后重新采集；人工原尺寸检查确认最终三张图相机、标签、数值与色块完整可辨。阈值和模拟配置未因截图问题改变。

## 60–75 秒黄金演示路径

1. `0–10 秒`：进入 `L_FlowMapChaosObstacle`，指出真实 Chaos 球体、GPU 粒子、MAC 速度箭头、压力势热图和同源步号。
2. `10–25 秒`：按 `B` 从 PFM 中档切到 Short Map 中档；HUD 的后端、梯度开关、Map Reset 周期和重建计数必须一起变化。
3. `25–40 秒`：按 `C` 切到高预算；展示物理域不变，而网格、粒子、箭头与压力迭代预算增加。
4. `40–50 秒`：再按 `B` 回到 PFM 高档，强调 Chaos 边界始终启用，A/B 改的是个人数值后端而不是把 Chaos 冒充流体解算器。
5. `50–65 秒`：用 `Space` 暂停、`N` 单步、`D` 切换调试层；展示固定步与可视快照生命周期受 UE 运行态控制。
6. `65–75 秒`：按 `E` 导出指标，切到三态截图/长序列 CSV，说明失败配置只改变速度预算与 Reset 周期，并给出可复算 P95/P99。

演示者只需按 `B/C/Space/N/D/E`；不需要打开编辑器蓝图或控制台。`R` 仅在现场状态异常时执行全链路复位，不占常规演示时长。

## UE、Chaos、论文与个人贡献归因

- Epic / Lyra：提供 GameMode、加载与输入/窗口运行框架；本 Case 没有把 Lyra 的现成玩法或特效当作物理成果。
- Unreal Engine：提供 Actor/Component 生命周期、Global Shader、RDG/RHI 资源与 Pass 调度、异步 Readback、GPU timestamp、基础网格和渲染接口。
- Chaos：只负责球形刚体的真实模拟 Pose、速度与场景物理生命周期；它不负责本 Case 的流体压力、粒子或 Flow Map 数值解算。
- 论文：算法方向受 2024 SIGGRAPH《Eulerian-Lagrangian Fluid Simulation on Particle Flow Maps》启发；作品只主张 2D 工程复现子集，不复用论文相对 NFM 的加速结论。
- 个人实现：MAC 数据合同、PFM 粒子与 backward-map Jacobian、impulse/gradient P2G、压力投影、Map Reset、运动边界栅格与碰撞投影、RDG 持久会话、非阻塞性能采样、中文 HUD/数据可视化、A/B 与失败消融、自动化脚本和证据文档。
- 未使用：Case03 没有使用 Assassin、Dragon、ALS 或 Marketplace/Fab 美术资产；Niagara 也不是权威状态或完成依赖。

## Case03 总验收结论

Case03 的范围是“PFM 启发的 2D UE/RDG 流体研究复现 + Live Chaos 运动刚体边界”，不是 3D 自由表面流体、Chaos 内置流体求解器或成品工具。标准数值基准、CPU/GPU 真值、持久 GPU 闭环、真实 Chaos 边界、真后端/预算切换、交互、可解释失败、三态 GUI、长序列逐步时间、黄金路径和归因均已具备；以 `validate_m4_case03_acceptance.ps1` 的机器报告为最终冻结门。
