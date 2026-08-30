# M6 / Case 05 验证证据

## 首门：平面推撞—跌倒—恢复控制合同

首门使用 SI 单位的程序化倒立摆角色，不声称代表完整人体或 Chaos 多刚体。它先把后续 UE 场景必须遵守的可量测合同固定下来：有限支撑面、显式外部角冲量、有限力矩、重力、地面碰撞、稳定保持、正/反面恢复分支、姿态误差、恢复时间与机械功。

自动化计划覆盖：中等推力站立拒绝、大推力跌倒后恢复、前后方向对称、低力矩预期失败。通过后才进入真实 Skeletal Mesh / Physics Asset / Physics Control，不能用角色动画看起来正常替代这些数值门。

## 首轮结果

- LyraEditor Win64 Development 在 UE 5.4.4 CL 35576357 下编译通过；`PhysicsDrivenCharacterRuntime` 已作为 Case 私有运行时模块进入 Game Feature，并声明 PhysicsResearch、ShooterCore 与 UE Physics Control 依赖。
- 中等推撞：`55 N·m·s` 角冲量下最大倾角 `6.115°`，支撑裕量最低仍为 `0.0841 m`，没有误判为跌倒；最终误差 `1.14e-5°`，峰值力矩 `276.54 N·m`，机械功 `35.29 J`。
- 跌倒恢复：`120 N·m·s` 冲量在 `0.2583 s` 跨出支撑面，落地后 `1.3375 s` 开始起身，跌倒到恢复共 `3.000 s`、其中起身控制 `1.9208 s`；最终误差 `0.00326°`，峰值力矩 `961.60 N·m`，机械功 `1417.18 J`。
- 正反面：`±120 N·m·s` 分别选择 Front/Back 分支；对称模型的恢复时间差和机械功差均为 0。
- 力矩失败：最大力矩降到 `400 N·m`，低于水平姿态峰值重力矩 `662.175 N·m`；角色保持约 `90°`，按 `failed=true / expected_failure=true` 退出，没有瞬移或超预算起身。
- 逐步证据：从 1440 个固定步抽取 125 行并保留所有相位切换，`planar_recovery_trajectory.csv` 覆盖 Balanced/Falling/Settling/Recovering/Recovered，时间与步号严格单调且全部有限。
- 自动化：`PhysicsResearch.DrivenCharacter.` 4/4；机器报告 `planar_recovery_foundation_validation.json` 10/10 检查通过。

## 失败记录与当前边界

首轮增加“单次持续地面接触”门后，默认恢复系数 `0.04` 产生第二次真实小反弹，自动化为 3/4。起身真值需要稳定落地，而非弹性跌落展示，因此正式基准改为完全非弹性地面；阈值和控制预算未放宽，失败报告保存在 `AutomationContactFailure/index.json`。弹性跌落应成为后续独立标准基准，不能混入起身真值后再把多次接触解释成错误。

## 第二门：真实 Manny / Chaos / PhysicsControl 同冲量对照

独立地图 `/PhysicsDrivenCharacter/Maps/L_PhysicsControlCharacter` 同时运行两路真实 Chaos 多刚体：左侧 `ChaosPassiveRagdoll` 只使用 Manny 与 `PA_Mannequin`，右侧 `ChaosPhysicsControlActive` 在相同 Physics Asset 上增加 Epic PhysicsControl 的父子角控制、世界空间 pelvis 控制和 Body Modifier。两路在 `0.5 s` 向 `spine_03` 施加完全相同的 `5200 kg·cm/s` Y 向冲量；Actor 在 PostPhysics 采样 6 秒、360 行，量测 22 个物理骨骼相对初始世界姿态的平均角误差和 pelvis 位移。

- 被动最终姿态误差 `107.604°`、pelvis 位移 `152.214 cm`；主动最终姿态误差 `27.144°`、pelvis 位移 `2.996 cm`。
- 主动/被动最终姿态误差比 `25.23%`，位移比 `1.97%`；正式合同要求前者低于 50%、主动 pelvis 小于 10 cm，`controller_advantage_pass=true`。
- `DrivenCharacterRealtime.csv/json` 保存完整逐帧数据；验证脚本独立复算最终值、极值与比率，16/16 检查通过。
- 中文 GUI 截图清楚显示左侧 Manny 倒地、右侧 Manny 保持参考位置，并同时显示两路指标；本门使用 UE 5.4.4 CL 35576357、D3D12 SM6。
- 回归自动化 `PhysicsResearch.DrivenCharacter.` 仍为 4/4 成功、0 警告。

首轮真实运行不合格：BodyModifier 运动类型在 PrePhysics 消费，而冲量从 PostPhysics 同帧发出，导致研究路 `spine_03` 当帧未模拟；世界 pelvis 目标也未被明确固定，最终位移达到 `867.787 cm`。正式实现先同步释放 pelvis 以下刚体，再由 Modifier 接管，并使用显式初始世界 pelvis 目标；没有放宽最终通过阈值。失败记录见 `PhysicsControlTargetFailure/index.json`。

## 第三门：ALS 正反面起身与物理—动画权威交接

第三门只从本地 ALS V4 工程迁移 `ALS_CLF_GetUp_Front/Back` 及 UE 自动收集的 28 份依赖（共约 16.6 MB），没有迁入 ALS Character、AnimBP 或 Camera 框架。两个动作经独立 IK Rig / IK Retargeter 派生到 Lyra Manny，时长均为 1.5 秒；20 条同名链确定映射，Manny 多出的 8 条 metacarpal 链明确保持未映射，避免模糊匹配把掌骨错误接到手指链。

独立地图 `/PhysicsDrivenCharacter/Maps/L_PhysicalGetUpHandoff` 用三个真实 Manny 通道验证同一状态机：Front/Back 通道从各自动作第一帧构造物理俯卧/仰卧初态，进入 Chaos 刚体后依据 pelvis 朝向、地面接触、线速度、角速度和 `0.35 s` 稳定保持选择分支；随后由 22 个 PhysicsControl 驱动对应 ALS 动画，完成时销毁 Control/BodyModifier、关闭模拟并显式把权威交回动画。高速通道保持 `45.50 cm/s`，按 `RejectMoving` 拒绝起身。

- Front 与 Back 均在 `1.333 s` 开始恢复、`3.083 s` 完成，每路创建 22 个控制器；两路最终在画面中可见。
- 速度拒绝通道保持物理倒地，未创建控制器；`observed_front_and_back`、`both_recovered`、`velocity_rejection_pass` 与总门均为 true。
- `PhysicsResearch.DrivenCharacter.` 回归由 4/4 增至 7/7；独立验证器 16/16 通过。
- 正式运行数据、截图与机器报告分别为 `PhysicalGetUpHandoff.json`、`Screenshots/Case05_ALS_PhysicalGetUp.png` 和 `physical_getup_validation.json`。

迭代过程暴露了三个真实边界：场景查询不能忽略包含地面的整个 Actor；动态跌落不能同时充当确定分支真值；PhysicsControl BodyModifier 若未销毁，会在下一次 PrePhysics 重新夺回模拟权威。正式基准因此用 canonical physical prone pose 隔离分支/交接职责，并在动画完成时显式销毁控制集合；阈值没有为高速拒绝通道放宽。

当前已证明程序化控制合同、真实引擎多刚体扰动拒绝，以及 ALS Front/Back 选择和局部物理—动画权威交接；仍未证明玩家交互、Lyra 独立可恢复状态、GAS/服务器权威、网络复制、三个标准基准、性能或最终美术场景。Case05 仍处于进行中，不能冻结。

## 第四门：玩家交互闭环

同一地图在非基准模式下提供真实键盘入口：`1/2` 选择俯卧/仰卧，`P` 向 pelvis 施加物理角冲量，`Space` 请求起身，`R` 重置当前姿态。自动交互回放不另写捷径，而是直接调用这五个键绑定使用的同一组 C++ 动作函数。

- 固定回放完成 Front → Back → Reset → Push → 高速起身请求，计数精确为选姿 2、推撞 1、请求 3、重置 1。
- 最终高速请求以 `RejectMoving / Rejected` 收口，`interaction_pass=true`；不会因玩家按下 Space 就绕过 Chaos 判据。
- 中文 HUD 同时显示完整按键说明与实时计数；机器验证 7/7、DrivenCharacter 回归仍为 7/7。
- 证据见 `PhysicalGetUpInteraction.json`、`physical_getup_interaction_validation.json` 与 `Screenshots/Case05_ALS_Interaction.png`。

当前“玩家交互”完成门已满足，但它仍是 Case 私有的原生 UE 输入，不冒充 Lyra Enhanced Input、GAS 或网络权威。下一门进入 Lyra 独立可恢复跌倒状态与服务器判据。

## 第五门（基础）：Lyra 可恢复跌倒权威状态

新增 `URecoverableFallComponent` 与纯状态策略，状态为 `Balanced/Falling/Settling/Recovering/Recovered/Failed`，与 `ULyraHealthComponent::DeathState` 平行而非复用。组件复制 FallState 与 Front/Back 分支，只提供一次恢复意图 RPC；服务器物理观测决定状态推进，客户端策略不能自行推进。

- `ULyraHealthComponent::IsDeadOrDying()` 优先：任一恢复状态遇到死亡都进入 Failed，不覆盖 `Status.Death.*`。
- 独立 GAS loose tags 使用 `Status.PhysicsRecoverableFall.*`；进入 Falling 停止 Character Movement，且只在服务器 Recovered、Death 未激活时恢复 Walking。
- 不逐骨骼发送 RPC；网络输入只有 `ServerRequestRecovery` 的单次玩家意图，姿态分支随组件属性复制。
- 新增 AuthorityOnly、DeathPriority、ServerSequence、RejectMoving 四个测试；DrivenCharacter 回归从 7/7 增至 11/11，机器门 10/10。

该门只完成“编译并验证的 Lyra 权威基础”，尚未通过 Lyra Experience / GameFeature Action 装配到真实 Pawn，也尚未做服务器/客户端双进程复制、纠错和带宽验收；这些仍是下一门，不能据此宣称联机完成。

## 第五门（运行）：GameFeature 注入与双进程复制

`PhysicsDrivenCharacter` 的 GameFeatureData 现包含一个个人 C++ Action。Action 复用 `UGameFrameworkComponentManager::AddComponentRequest`，在客户端和服务器世界中把 `URecoverableFallComponent` 注入 `ALyraCharacter`，不修改 LyraCharacter 基类。配置脚本可重复运行且只保留一个 Action。

独立 UE 专用服务端（NetMode 1，端口 7787）与独立游戏客户端（NetMode 3）完成真实连接：客户端 Join succeeded，双方的 `B_Hero_ShooterMannequin_C` 均获得组件。选定玩家序列中，客户端只发送一次恢复意图；服务端在 Settling 接受，并推进 `Balanced→Falling→Settling→Recovering(Front)→Recovered`，客户端收到四次相同状态通知。

- 服务端/客户端 GameFeature 注入、真实 Lyra Pawn、Health/Movement、客户端 ASC 晚绑定、单次 RPC 与完整复制序列，机器门 10/10。
- 两个复制字段为 `FallState:uint8` 与 `RecoveryBranch:uint8`；逐骨骼 RPC 为 0。
- DrivenCharacter 回归仍为 11/11。
- 证据见 `recoverable_fall_gamefeature_setup.json` 与 `recoverable_fall_network_validation.json`。

当前已证明双进程权威与离散状态复制语义，但尚无 Network Profiler 的精确线速字节、丢包下纠错频率和带宽 P95/P99；下一门先补这一网络性能证据，再进入剩余标准基准与最终场景。

## 第五门（弱网证据）：Network Profiler 原始采样

在 UE 引擎包模拟 `PktLag=100 / PktLoss=5` 下，重新启动独立专用服务端与独立客户端，并只在本次探针命令行开启 Network Profiler。客户端仍只发送一次恢复意图；服务端在 Settling 接受，推进到 `Recovering(Front)→Recovered`，客户端最终收到相同状态。探针在客户端 20 秒、服务端 60 秒后先关闭 Profiler 再正常退出，避免强制结束留下零字节或未闭合文件。

- 服务端原始采样 `NetworkProfiles/Case05_Server_100ms_5pct.nprof` 为 154715 bytes；客户端实际游戏会话 `NetworkProfiles/Case05_Client_100ms_5pct.nprof` 为 110017 bytes。
- 两端日志均确认 100ms/5% 生效、Profiler 正常关闭和 CleanExit；机器门 `network_profile_capture_validation.json` 为 8/8。探针改动后的完整 NullRHI 回归保存在 `AutomationNetworkProfileRegression/index.json`，DrivenCharacter 为 11/11、0 警告、0 失败。
- 首轮自动退出捕获了短生命周期 Bot 组件，正式会话没有按计划退出；该次 0/短文件不进入证据。正式实现改为捕获 World 生命周期后再复跑，没有用强制结束的文件替代正式样本。
- 两份 `.nprof` 包含 Lyra 会话、Bot 与其他引擎流量。其文件体积只能证明采样非空且完整，不能直接作为 `URecoverableFallComponent` 带宽。

本门已补齐“真实弱网下仍能权威收敛”和“可复查的原始网络采样”。随后新增 UE 5.4 Network Profiler v14 只读导出器：两份正式采样都必须通过 Magic/Version、全部 token 和 EndOfStream 校验；服务端解析 21606 个事件，客户端解析 4916 个事件。客户端第二会话开头有一个跨 seamless-travel 残留的旧 SendBunch channel-name 索引，导出器只允许该字段保留 `<unresolved:1>`，其他名字引用仍严格越界失败。

选定恢复窗口的归因门 `network_profile_attribution_validation.json` 为 11/11：

- `ServerRequestRecovery` 只出现 1 次，RPC header/parameter/footer 为 `18/11/0 bits`，合计 `29 bits`。
- 服务端 Recovering 组合属性写入 3 次，其中后两次是纠错重发，间隔 `0.328240 / 0.327996 s`；随后 Recovered 再写 1 次 FallState。
- 窗口共 4 次 FallState、3 次 RecoveryBranch，属性 payload 合计 `21 bits`；内容块、property handle 和 payload 的 Profiler 可归因 token 合计 `185 bits`，四次更新样本为 `51/35/35/64 bits`，Nearest-Rank P95/P99 均为 `64 bits`。
- 29-bit RPC 是完整 RPC 序列化段；185-bit 指标不含 Profiler 未单列的 packed content-length，整场 Lyra 的包头、ACK、合包和其他 Actor 流量也不能无歧义拆给组件，因此不换算成“独立 UDP/IP 线速字节”。

网络复制/弱网归因门现已完成。Case05 下一步进入剩余三个标准基准、完整性能门与最终场景，仍不能冻结。

## 标准基准二：高度跌落与顺应吸收

新增 SI 单位的 1-DOF 质心垂直模型，把自由落体、顺应腿弹簧/阻尼、非负且有上限的地面反力、压缩行程和稳定保持写成独立合同。它不是 Chaos 多体结果，而是后续 Manny/Chaos pelvis 高度、接触冲量、峰值反力和触底失败必须消费的程序化真值。

- 标准 0.6m 跌落：理论冲击速度 `3.431035 m/s`，数值结果 `3.433500 m/s`；单次接触，`0.439 s` 内稳定，峰值反力 `12120.42 N`，最大压缩 `0.059071 m`，阻尼耗散 `417.504 J`，地面冲量 `581.342 N·s`。
- 压力失败：0.9m 跌落、`0.12 m` 行程与 `2500 N` 力上限下，压缩达到 `0.122554 m` 并按预期 Failed；没有瞬移到静止高度，也没有突破力上限。
- 完整 `PhysicsResearch.DrivenCharacter.` 回归从 11/11 增至 13/13，0 警告、0 失败；机器门 `vertical_landing_validation.json` 为 13/13。

现有平面恢复是标准基准一，高度跌落/吸收是标准基准二。下一门实现多段关节耦合标准基准三，再将新增合同接到真实 Chaos 角色切片。

## 标准基准三：多段关节耦合恢复

三段 SI 单位模型显式表示髋、膝、躯干的惯量、重力矩、局部 PD 与各自力矩上限；相邻段通过等大反向的弹簧—阻尼力矩耦合。它验证大姿态恢复、远端冲量传播/无耦合消融和低预算失败，不是三个互不相干的单关节控制器，也不冒充训练策略或 Chaos 骨骼结果。

- 标准恢复：初始 RMS `53.619°`，`1.108 s` 后降到 `0.188°`；髋/膝/躯干峰值控制力矩 `261.799/268.781/85.521 N·m`，绝对机械功 `82.474 J`。
- 耦合消融：同一 `12 N·m·s` 躯干冲量下，耦合模型的近端峰值响应 `2.106°`、远端 `11.306°`、传递比 `0.1863`；关闭相邻耦合后近端为 0，两路仍均能恢复。
- 失败门首版错误地假设 `40/30/20 N·m` 必然失败，但耦合仍帮助系统恢复到 `0.263°`；失败报告保存在 `AutomationArticulatedFirstFailure`。正式压力预算 `5/4/3 N·m` 下最终 RMS `51.019°`，三关节均严格触顶并按预期 Failed。
- 完整 DrivenCharacter 回归 16/16、0 警告、0 失败；机器门 `articulated_recovery_validation.json` 为 14/14。

Case05 的三个程序化标准基准现已补满。下一门不是继续增加数学基准，而是把高度/冲量/耦合合同映射到真实 Manny/Chaos/PhysicsControl，随后完成性能与最终美术场景。

## 真实 Chaos 高度映射

同一 `/PhysicsDrivenCharacter/Maps/L_PhysicsControlCharacter` 增加隔离的 `-PhysicsDrivenCharacterDropMapping` 模式，不改变旧同冲量参数。两路 Manny/`PA_Mannequin` 在 Physics Asset 创建前同时抬高 60cm；Drop 模式严格锁定到 0.5s 后释放。被动路进入 pelvis 以下 Chaos ragdoll，主动路使用同一 22 刚体和 22 个 PhysicsControl，pelvis 世界目标按 `½gt²` 下降并在 60cm 截止，避免瞬时位置拉拽冒充落地控制。

- 正式协议固定 `-BENCHMARK -FPS=60`，6 秒精确 360 个 PostPhysics 样本；被动峰值下落 `325.282 cm/s`，与 60cm 理论自由落体 `342.929 cm/s` 同量级且误差小于 30cm/s。
- 被动 ragdoll 最终平均姿态误差 `118.347°`、pelvis 位移 `174.143 cm`、最大下落 `143.932 cm`，明确作为坍塌基线。
- 主动路峰值下落 `298.966 cm/s`、最大下落 `62.039 cm`、最终 pelvis 位移 `56.975 cm`；最终平均姿态误差 `10.475°`，仅为被动的 `8.85%`。
- `chaos_drop_mapping_validation.json` 从 CSV 独立复算速度、落差、最大/最终姿态和 360 步合同，16/16；完整 DrivenCharacter 回归仍为 16/16，旧同冲量验证器也继续通过。

六次失败协议保留 JSON 摘要：普通组件移动与 TeleportPhysics 都没有解决 Physics Asset 创建时机；BlendWeight 调用顺序重新开启组件模拟；瞬时下移 pelvis 目标制造过大速度；非固定步 NullRHI 让被动约束能量爆发；只限帧率仍没有固定 360 步。正式门由构造期高度、Drop 专用锁定、逐刚体释放、弹道目标与 UE Benchmark 固定步共同收口。

高度合同现已映射到真实 Chaos。

## 真实 Chaos 骨骼耦合 A/B

旧同冲量场景新增三处父子相对转角采样：髋为 `thigh_l` 相对 `pelvis`，膝为 `calf_l` 相对 `thigh_l`，躯干为 `spine_03` 相对 `spine_02`。两路仍共享 Manny、`PA_Mannequin`、22 个刚体、初态和 `spine_03` 的 `5200 kg·cm/s` 冲量；左路保留 Physics Asset 的 Chaos 关节约束，右路在同一系统上增加 22 个 PhysicsControl。这里隔离的是额外控制层，不把左路错误描述为“无耦合刚体”。

- Chaos 约束基线的髋/膝/躯干峰值相对角误差为 `38.654° / 65.450° / 51.480°`；增加 PhysicsControl 后为 `5.857° / 8.695° / 15.607°`，三处均降到基线一半以下。
- 以 `max(髋,膝)/躯干` 表示的近端响应比由 `1.2714` 降至 `0.5571`。它只是相对角响应比，不宣称力、冲量或能量传递率。
- `DrivenCharacterJointCoupling.csv/json` 保存 360 个固定 60Hz PostPhysics 样本；`chaos_joint_coupling_validation.json` 独立复算六个峰值与两路比率，12/12 通过。
- 新代码增量编译通过；旧同冲量验证、60cm 高度验证均继续通过；完整 `PhysicsResearch.DrivenCharacter.` 回归 16/16、0 警告、0 失败，报告位于 `AutomationJointCouplingRegression`。

真实高度与骨骼耦合合同均已映射。

## PhysicsControl / Chaos 性能门

性能模式仍使用同一地图、两具 Manny 和相同冲量，场景共 44 个 Chaos Physics Asset 刚体，其中主动路有 22 个 PhysicsControl。仅性能模式把地面扩大到 `200m × 200m`，避免 30 秒采样后被动角色离开碰撞域；控制参数、刚体和冲量不变。正式协议连续启动三个独立 UE 5.4 游戏进程，确认没有并发 UnrealEditor 后，以 NullRHI、`-BENCHMARK -FPS=60` 每轮采 1800 帧，丢弃前 300 帧，共聚合 4500 个测量样本。

- 隔离地图端到端 PostPhysics 墙钟间隔的 pooled P50/P95/P99 为 `1.066 / 1.277 / 1.430 ms`；三轮各自 P99 为 `1.363 / 1.577 / 1.429 ms`。
- 个人量测组件包裹 Epic `UPhysicsControlComponent::TickComponent` 的完整父类调用，覆盖 target cache、22 个 Control 和 Body Modifier；pooled P50/P95/P99 为 `0.0446 / 0.0506 / 0.0943 ms`，三轮各自 P99 为 `0.0884 / 0.1084 / 0.0678 ms`。
- 三轮被动 pelvis 最终位移均为 `279.32 cm`、峰值下落速度 `205.33 cm/s`，主动 pelvis 约 `2.99 cm`，没有再出现离开地面后的无界下落。
- 机器门 `physics_control_performance_validation.json` 为 12/12；旧冲量、高度门继续通过，完整 DrivenCharacter 回归 16/16、0 警告、0 失败。

端到端墙钟不命名为 Chaos 求解器独占时间，也不减去 PhysicsControl 时间；NullRHI 结果是本机 CPU 的隔离物理场景证据，不是跨平台 Shipping 帧预算。

## 最终 Portfolio 场景与 Case05 总验收

`-PhysicsDrivenCharacterPortfolio` 在运行时动态生成 6 个无碰撞舞台网格和 2 盏冷暖分区灯，不修改地图资产、角色碰撞或物理配置。最终 D3D12 游戏进程以 1600×900、固定 60Hz 运行；同冲量数值仍逐值等于冻结门：被动/主动最终姿态误差 `107.604/27.144°`，主动 pelvis 位移 `2.996cm`。正式截图为 `Screenshots/Case05_PhysicsControl_Portfolio.png`，中文 HUD 同时显示 A/B、指标和归因边界。

55 秒黄金演示见 `CASE05_黄金演示.md`：先展示同冲量 A/B，再展示 ALS Front/Back 与高速拒绝，最后落到真实骨骼、高度、网络与性能证据。最终 Portfolio 门 12/12；完整 DrivenCharacter 回归 16/16、0 警告、0 失败。

`case05_completion_validation.json` 汇总 14 份子系统报告、两个独立地图、四张真实截图、三个标准基准、网络、性能与最终场景，10/10 通过。M6 / Case05 已完成并冻结；后续只在 M7 做统一入口、打包和跨 Case 交付，不回到 Case05 增加可选算法。
