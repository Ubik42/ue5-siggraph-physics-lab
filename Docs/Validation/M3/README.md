# M3 / Case 02 验证证据

当前 `PhysicsResearch.SoftBody.` 模块级 NullRHI 自动化位于 `Automation/index.json`：11/11 成功、0 失败、0 警告、0 错误。GPU 测试在 NullRHI 下只验证注册/跳过分支，真实 Compute 证据来自独立 D3D12 SM6 报告。

## 四面体输入合同

- 自动化：`Automation/index.json` 中的 `TetrahedralGrid.Contract`。
- 机器汇总：`tetrahedral_grid_validation.json`
- 结果：低/中/高三档 1/1 自动化通过，精确顶点/单元/边界/固定点数量与解析总体积全部成立。

## Chaos Flesh 隔离 Spike

- 机器记录：`chaos_flesh_isolated_spike.json`
- 结果：插件能挂载，但 `ChaosFleshNodes` 的 `FExtractGEOInt::Value` 在 UE 5.4.4 触发 `LogClass Error`，退出也超时；保持默认关闭。
- 边界：不据此推断 `ChaosFleshEngine` 运行时代码完全不可用。

## 局部 XPBD/Jacobi 悬臂

- 自动化：`LocalXpbdMaterialAutomation/index.json` 中的 `LocalXpbd.Cantilever`；该组合报告同时回归材料压缩。
- 机器汇总：`local_xpbd_validation.json`
- 原始收敛曲线：`local_xpbd_convergence.csv`，包含首固定步 1–64 次迭代的全部残差。
- 运行合同：45 顶点、96 四面体、180 距离约束、96 体积约束；1/60 秒、120 步、每步 64 次局部 Jacobi。
- 最终结果：残差 RMS 0.0350246，最大结构边误差 0.171402，平均体积误差 0.0209630，总体积比 1.009258，自由端下垂 10.7045 cm，固定点漂移 0。
- CPU 参考：当前机器平均/最大固定步约 0.293/0.338 ms；只用于后续同实现回归，不冒充 GPU 证据。

## 位移控制压缩与材料响应

- 自动化：`LocalXpbdMaterialAutomation/index.json`，同时回归悬臂与材料压缩两项。
- 机器汇总：`compression_materials_validation.json`。
- 输入：最大 X 面 9 点向内 8 cm，最小 X 面固定，无重力；均匀软、均匀硬、X 中点分区三组仅切换材料编号。
- 结果：软/硬平均体积误差 0.198760/0.151133，总体积比 0.802146/0.854474；分区总体积比 0.833758；三组目标误差 0。
- 参数边界：该 A/B 使用显式高对比柔度，不覆盖稳定悬臂默认值，也不声称等价于 Neo-Hookean 本构。

## 两模态全局低频校正消融

- 自动化：`LowFrequencyAutomation/index.json` 中的 `LowFrequency.CantileverAblation`。
- 机器汇总：`low_frequency_correction_validation.json`。
- 输入合同：局部与校正路径使用相同 45 顶点/96 四面体、1/60 秒、120 步、每步 32 次迭代；唯一变量是每轮开启线性/二次两个全局模态投影，强度 0.35。
- 结果：最终残差 `0.0641992 → 0.0460289`，比值 71.70%；最大结构边误差 `0.339341 → 0.234055`，平均体积误差 `0.0340260 → 0.0275973`，固定端漂移均为 0。
- 边界：这是用于证明低频全局信息传播收益的工程子集，不是完整 MGPBD、UA-AMG 或 JGS2。

## GPU 约束上传合同

- 自动化：`GpuTopologyAutomation/index.json` 中的 `GpuTopology.Contract`。
- 机器汇总：`gpu_topology_validation.json`。
- 三档结果：低/中/高分别为 45/225/1377 顶点、180/1152/8160 唯一边和 96/768/6144 四面体；边 CSR 关联严格为边数 2 倍，四面体 CSR 关联严格为单元数 4 倍。
- 迭代设计：距离约束 Pass 与体积约束 Pass 各自写每约束修正/Lambda，再由顶点 CSR Gather Pass 聚合回写；避免多个约束线程直接争写顶点位置。
- 当前边界：此门证明持久 GPU Buffer 的确定性输入布局；真实 Compute 正确性由下一节的 D3D12 门证明。

## 持久 D3D12 四面体 XPBD

- 自动化：`GpuXpbdD3D12Automation/index.json` 中的 `GpuXpbd.PersistentCantilever`，设备为 RTX 4080、`DirectX 12 (SM6)` / `D3D12_SM6`。
- 机器汇总：`gpu_xpbd_d3d12_validation.json`。
- 运行合同：低档 45 顶点/180 边/96 四面体，120 个 1/60 秒固定步，每步 32 次；位置历史、两份工作区、Lambda、修正和拓扑 Buffer 跨固定步持久，运行接口只排队，不调用 GPU Idle/Flush。
- 结果：距离/体积/Gather 三类 Pass 各 3840 次，完成 2 次异步快照，最终快照精确对应第 120 步；GPU 残差 `0.0606721`，同预算 CPU 为 `0.0641992`，比值 `0.945061`；最大结构边误差 `0.320076`、平均体积误差 `0.0330536`、自由端下垂 `18.5538 cm`、固定端漂移 0。
- 失败记录：首版 GPU 残差 `0.880155`、固定端漂移 `23.1002 cm`。根因是同一 HLSL 全局资源名跨 Pass 同时声明为 UAV 输出并以 SRV 输入绑定，反射类型错位；拆分输出 UAV/输入 SRV 参数名后通过原门槛。
- 当前边界：验证辅助函数会显式等待 GPU 取得确定性最终快照；运行时 `EnqueueStep/PollReadback` 不等待。求解成本由下一节独立扩展性门证明。

## 三档 GPU timestamp 与扩展性

- 自动化：`GpuXpbdScalingD3D12Automation/index.json` 中的 `GpuXpbd.ResolutionScaling`，同一 RTX 4080 / `DirectX 12 (SM6)` 环境。
- 机器汇总：`gpu_xpbd_scaling_validation.json`；72 个原始样本：`gpu_xpbd_timing_samples.csv`。
- 公平输入：低/中/高均使用同一个 `40×20×20 cm` 物理域、24 个固定步和每步 32 次迭代，仅把网格从 45/180/96 顶点/边/四面体提高到 225/1152/768 与 1377/8160/6144。
- 计时合同：8 槽绝对 GPU timestamp 环围住完整固定步 RDG 图；运行时只轮询已完成查询，不执行逐步 GPU Idle。自动化每步只同步渲染线程提交，排除前 6 步预热；三档各取得 24/24 样本、跳过 0 个，稳态各 18 个。
- 稳态结果：低/中/高的中位数为 `2.954 / 3.486 / 3.864 ms`，P95 为 `3.031 / 3.548 / 3.936 ms`；高档中位数比低档高 30.8%。成本趋势严格递增，但低、中档仍部分处于派发/并行平台区，不宣称线性扩展。
- 物理解释：相同 32 次局部迭代预算下，残差随分辨率从 `0.08855` 升至 `0.25986 / 0.47761`，高档最大边误差达到 `3.10265`。因此本门只通过“调度、计时与成本扩展性”，不通过中/高档物理质量；它直接构成下一步 GPU 低频/多层校正必须击败的基线。

## GPU 两模态低频校正消融

- 自动化：`GpuXpbdLowFrequencyParallelD3D12Automation/index.json` 中的 `GpuXpbd.LowFrequencyAblation`；机器汇总：`gpu_low_frequency_correction_validation.json`。
- 算法路径：每次局部约束求值后先按顶点 Gather；两个解析模态的右端项使用 64 线程组内归约，再由只遍历组部分和的最终归约求系数，最后逐顶点叠加、限幅和回写。关闭路径仍走原始 Gather/Apply。
- 公平消融：中/高档均运行 120 个固定步、每步 32 次迭代，唯一变量为强度 0.35 的两模态校正。校正路径每档精确执行 3840 次语义投影、7680 次两级归约派发和 3840 次应用；局部路径投影数为 0。
- 中档：残差 `0.198919 → 0.158200`（79.53%），最大边误差 `1.36239 → 1.15044`，平均体积误差 `0.057133 → 0.054848`。
- 高档：残差 `0.549605 → 0.359026`（65.32%），最大边误差 `3.37032 → 2.42118`，平均体积误差 `0.096260 → 0.053267`。两档固定端漂移均为 0。
- 计时边界：四条路径各取得 120/120 个非阻塞 timestamp、跳过 0；但不同 A/B 顺序的中位数与 P95 对 GPU 频率/运行顺序高度敏感，正式门只检查样本完整和有限，不把当前分布写成性能加速。后续需单独的反平衡计时协议或 Unreal Insights Pass 级捕获。

## GPU 低频校正 ABBA 反平衡计时

- 自动化：`GpuXpbdCounterbalancedTimingD3D12Automation/index.json` 中的 `GpuXpbd.LowFrequencyCounterbalancedTiming`；机器汇总：`gpu_counterbalanced_timing_validation.json`。
- 协议：中/高档各自按 Local → Corrected → Corrected → Local 建立四个全新 GPU 会话；每个会话 128 步、32 次迭代，排除前 48 步预热，保留 80 个稳态样本。每档 512 个 timestamp 全部完成、跳过 0。
- Pass 归因：每个校正会话相对局部路径，每次迭代新增一次语义投影、两次归约派发和一次应用；128×32 下精确为 `4096 / 8192 / 4096`，而不是把所有额外成本笼统归到“GPU 求解器”。
- 反平衡结果：中档局部/校正的两次会话中位均值为 `4.0315 / 4.6265 ms`，校正开销 `14.76%`；高档为 `4.430 / 5.196 ms`，开销 `17.29%`。校正配对自身中位差低于 0.03%。
- 与质量联合解读：独立同预算收敛门中，中/高档残差至少降低 15%/25%；因此当前结论是“用约 15–17% 的完整步 GPU 中位开销换取显著收敛质量”，不是原始速度加速。
- 边界：ABBA 两端的局部会话中位仍相差约 28–32%，说明机器频率/驱动状态尚未完全锁定。本报告是 RTX 4080 上的协议化证据，不外推为通用性能规律；P95 和全部旧反常结果继续保留。

## TerrorBringer 封闭外壳与表面绑定

- 生成证据：`dragon_softbody_cage_generation.json`；导入证据：`dragon_softbody_cage_import.json`；自动化：`DragonSurfaceBindingAutomation/index.json`；机器汇总：`dragon_surface_binding_validation.json`。
- 原网格问题：TerrorBringer FBX 含 2876 顶点、4684 三角形、1048 边界/非流形边和 28 连通分量，不直接四面体化。
- 派生物：Blender 5.2 以最大边/64 的体素尺度封口后减面到 6000 三角形，结果 0 边界边、0 非流形边、1 连通分量且有符号体积。
- UE 导入：本地资产位于 `/PhysicsSoftBody/DragonDerived/SM_TerrorBringer_SoftBodyCage`，开启 CPU Access 以便构建可 Cook 绑定。该 UAsset 是 Fab 源网格派生物，保留在本地并被 Git 忽略；仓库只跟踪生成/导入脚本和不含几何的统计。
- 绑定合同：4854 个 UE LOD0 表面顶点绑到 `8×8×8` 规则体积 Cage（729 顶点/3072 四面体/81 固定点）；三线性静止重建最大误差 `0.000141 cm`，合成 Cage 变形在表面产生 `39.9993 cm` 最大位移。
- 边界：当前是规则六面体节点的三线性 Cage 绑定，体积求解内部仍用六四面体分解；它不是每个表面点寻找单一四面体的重心坐标绑定，也不是贴体四面体化。

## GPU Cage 驱动 Dragon 动态表面

- 运行地图：`/PhysicsSoftBody/Maps/L_DragonGpuSurface`；地图生成记录：`dragon_gpu_surface_map_setup.json`。
- 真实 D3D12 游戏窗口报告：`dragon_gpu_surface_runtime.json`；机器汇总：`dragon_gpu_surface_runtime_validation.json`；截图：`Screenshots/M3_DragonGpuSurface.png`。
- 运行链路：持久四面体 Buffer 执行 300 个 1/60 秒固定步，按 4 步间隔异步读回 Cage；每份快照经 8 节点三线性权重更新 4854 个 Dragon 表面顶点，并用 `UDynamicMeshComponent` 的顶点位置快速刷新路径提交渲染。
- 结果：GPU 精确执行 300 步，最终快照对齐第 300 步；75 份快照完成并产生 75 次表面更新。Dragon 最大可见形变 `61.3888 cm`，静止绑定误差 `0.000141 cm`，81 个固定 Cage 点最大漂移 0。
- 多尺度证据：两种低频模式在 300×32 次求解迭代中精确执行 9600 次投影与 9600 次应用；中文 HUD 同屏显示固定步、异步快照、Cage/表面规模、绑定误差和固定端漂移。
- 失败记录：首版运行物理结果全部有效，但作品门误把低频校正计数写成“每固定步一次”，报告被正确标记为失败；实现实际是“每求解迭代一次”。门禁改为 `300×32` 后重新启动独立 UE 进程并通过，没有修改物理阈值。
- 当前边界：本门只证明 GPU→CPU 快照→表面绑定→UE 动态渲染的真实闭环；玩家交互、接触消融和替代后端分别由后续独立门验证，最终状态以作品级总验收为准。

## 玩家交互状态门

- 自动交互运行：`dragon_gpu_surface_interaction_runtime.json`；机器汇总：`dragon_gpu_surface_interaction_validation.json`；截图：`Screenshots/M3_DragonGpuSurface_Interaction.png`。
- 控制：空格暂停/继续、`N` 单步、`R` 重置当前后端与 Dragon 表面、`D` 切换 Cage/固定点调试显示、`E` 导出当前指标；`F` 切换标准/弱接触，`B` 在 GPU 与 CPU 研究参考后端之间重建会话。
- 真实进程脚本精确执行 2 次暂停切换、1 次单步、1 次重置、2 次调试显示切换和 1 次手动导出；重置后重新完成 300 步与最终异步快照，所有动态表面质量门保持通过。
- 重置语义：停止并释放原持久 GPU 会话，恢复静止 Cage/表面和数值测量，再创建新会话并将共享实验步号归零；不是只把 HUD 计数清零。
- 边界：基础交互脚本只隔离验证暂停、单步、重置、调试显示和导出；`B` 键真后端切换由 CPU 参考后端章节单独启动、计数和截图，不把 HUD 标签变化当成切换证据。

## GPU 地面接触与单参数失败消融

- 模块级真实 D3D12 自动化：`GpuGroundContactD3D12Automation/index.json` 中的 `GpuXpbd.GroundContactAblation`；机器汇总：`ground_contact_ablation_validation.json`。
- 接触实现：所有距离、体积与两模态低频迭代结束后，每个固定步精确执行一次不可穿透平面投影；固定点始终直接恢复静止位置。接触 Pass 不混入 32 次材料约束迭代，因此计数合同为 `300`，不是 `300×32`。
- 公平 A/B：标准与弱接触使用同一 4854 点/6000 面 Dragon、729 点/3072 四面体 Cage、相同地面高度、重力、阻尼、32 次迭代、300 个固定步与表面绑定；唯一变量是 `ground_contact_stiffness: 1.0 → 0.02`。
- 标准实景：`dragon_gpu_surface_contact_runtime.json` 与 `Screenshots/M3_DragonGpuSurface_Contact.png`；最大穿透 `0 cm`、最终接触顶点 53、固定端漂移 0，300 次接触投影全部执行。
- 弱接触预期失败：`dragon_gpu_surface_weak_contact_runtime.json` 与 `Screenshots/M3_DragonGpuSurface_WeakContact.png`；最大穿透 `64.2407 cm`、最终精确接触点 0、固定端漂移仍为 0。报告中的 `result=passed` 表示“预期失败被成功观测”，不表示该物理配置合格。
- 合成小规模复核：同一 GPU 内核在 120 步悬臂上得到标准/弱接触最大穿透 `0 / 7.99931 cm`，各 120 个接触 Pass，排除 Dragon 表面绑定或截图造成的假象。
- UE 展示：HUD 同屏显示接触配置、刚度、Pass 数、最大穿透与接触点；`F` 可切换标准/弱接触并真正销毁、重建 GPU 会话，地面是 UE `UStaticMeshComponent` 可视层但关闭碰撞，接触响应完全来自研究 Compute Pass。
- 边界：当前只处理无摩擦无限平面法向投影，不冒充 Chaos 碰撞形状、接触流形、摩擦或 CCD；Chaos/Flesh 在本机 UE 5.4.4 的可复现阻塞和 CPU 替代对照见下一节。

## CPU 局部参考后端与 Chaos/Flesh 边界

- 运行报告：`dragon_cpu_local_xpbd_reference_runtime.json`；真实 `B` 键切换报告：`dragon_backend_toggle_runtime.json`；机器汇总：`cpu_reference_backend_validation.json`；截图位于 `Screenshots/M3_DragonCpuLocalXpbdReference.png` 与 `Screenshots/M3_DragonBackendToggle.png`。
- Flesh 结论更新：UE 5.4.4 的 `ChaosFlesh.uplugin` 在正常项目依赖下会同时加载 `ChaosFlesh`、`ChaosFleshEngine` 与存在已记录反射属性缺陷的 `ChaosFleshNodes`。不能只在工程插件里声明 `ChaosFleshEngine` 并绕过所属插件的模块装载，因此没有伪造“runtime-only Flesh 已运行”。
- 替代基线：同一 Dragon 表面、729 点/3072 四面体 Cage、1/60 秒、300 步、每步 32 次迭代、重力、阻尼、两模态低频校正、地面高度与标准接触刚度，分别由持久 D3D12 XPBD 和 CPU 局部 Jacobi XPBD 真后端执行。
- CPU 执行合同：300 步、300 次同步表面更新、9600 次低频投影、300 次接触投影；固定端漂移 0、地面穿透 0、最终接触点 61；平均/最大 CPU 固定步 `5.968 / 10.088 ms`。
- 质量结论：相同 32 次预算下，GPU 标准路径最大表面形变 `37.354 cm`，CPU 局部参考达到 `978.277 cm`。CPU 报告使用 `passed_expected_failure`：表示真实后端、调度、绑定和指标合同执行成功，同时 `physics_quality_gate_passed=false`，不得解读为物理质量合格。
- `B` 键不是标签切换：自动脚本先运行 GPU 30 步，再通过与玩家相同的切换函数释放 GPU 会话、恢复静止 Cage/Dragon、初始化 CPU 求解器并将共享固定步归零，随后 CPU 后端重新完成 300 步；证据中 `backend_toggles=1`。
- 模块级 CPU 接触复核：`CpuContactReferenceAutomation/index.json` 在 NullRHI 下得到标准/弱接触最大穿透 `0 / 8.80725 cm`，说明 CPU 替代后端与 GPU 接触配置具有一致的参数语义。

## Case 02 作品级总验收

- 单一入口：`Scripts/Validation/validate_m3_case02_acceptance.ps1`；机器报告：`case02_acceptance_validation.json`。
- 验收同时要求：NullRHI 模块回归 11/11、14 份组件报告全部通过、真实 D3D12 SM6、低频收敛与 ABBA 成本归因、Dragon 授权派生边界、300 步 UE 动态表面、真实玩家状态控制、标准/弱接触单参数消融、CPU 真后端与预期质量失败、Chaos/Flesh 阻塞证据、6 张非空作品截图，以及 Shader/Actor/Solver/地图实现均存在。
- `passed` 表示 Case 02 已达到可独立复查和作品展示状态，不表示接触已覆盖摩擦/CCD，也不把 CPU 替代路径写成 Chaos Flesh。

验证入口：

```powershell
Scripts/Validation/validate_m3_tetrahedral_grid.ps1
Scripts/Validation/validate_m3_local_xpbd.ps1
Scripts/Validation/validate_m3_compression_materials.ps1
Scripts/Validation/validate_m3_low_frequency_correction.ps1
Scripts/Validation/validate_m3_gpu_topology.ps1
Scripts/Validation/validate_m3_gpu_xpbd_d3d12.ps1
Scripts/Validation/validate_m3_gpu_xpbd_scaling.ps1
Scripts/Validation/validate_m3_gpu_low_frequency_correction.ps1
Scripts/Validation/validate_m3_gpu_counterbalanced_timing.ps1
Scripts/Validation/validate_m3_dragon_surface_binding.ps1
Scripts/Validation/validate_m3_dragon_gpu_surface_runtime.ps1
Scripts/Validation/validate_m3_dragon_interaction.ps1
Scripts/Validation/validate_m3_ground_contact_ablation.ps1
Scripts/Validation/validate_m3_cpu_reference_backend.ps1
Scripts/Validation/validate_m3_case02_acceptance.ps1
```
