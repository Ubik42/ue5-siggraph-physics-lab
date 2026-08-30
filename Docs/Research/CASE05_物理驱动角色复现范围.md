# Case 05：物理驱动角色复现范围

## 作品主张

本 Case 定位为“受 NCP、MaskedMimic、ControlVAE 启发的 UE 物理角色控制工程复现”，不声称训练或复现这些论文的神经控制器。论文提供三个架构启发：NCP 将可执行低层技能与任务选择分离；MaskedMimic 将部分目标条件与任务 FSM 分离；ControlVAE 将昂贵训练脚手架与运行时真实物理回放分离。当前作品实现可解释、确定性的状态机与物理控制器，以量化指标验证同样的工程边界。

## 实际实现范围

第一标准基准是 SI 单位下的平面角色质心倒立摆：75 kg 角色、0.9 m 质心高度、60 kg·m² 转动惯量、有限支撑面、角冲量、重力、有限力矩和地面碰撞。控制器按 `1/240 s` 固定步推进，并显式经历 `Balanced → Falling → Settling → Recovering → Recovered/Failed`。

- 中等推力验证站立扰动拒绝，不允许为了“总能起身”把所有冲击都记作跌倒。
- 大推力必须跨出支撑面、发生落地、稳定判定、选择正/反面起身分支，再以光滑目标和重力补偿 PD 恢复。
- 每步记录姿态误差、角速度、支撑裕量、目标角、控制力矩、相位和绝对机械功；汇总恢复成功、恢复时间、峰值力矩、能量、碰撞次数与最终误差。
- 低于峰值重力矩的控制预算保留为预期失败，不能靠增加超预算力矩或瞬移回正掩盖。

该平面模型是程序化控制真值，不是 Chaos 替代品。标准基准二加入质心垂直跌落与顺应腿吸收，量测冲击速度、峰值反力、压缩行程、稳定时间、阻尼耗散和触底失败；标准基准三加入髋/膝/躯干惯量、重力矩、独立力矩预算和等反向相邻耦合，量测大姿态恢复、远端扰动传播与低预算失败。真实引擎层已经让 Manny Skeletal Mesh、`PA_Mannequin` 的 22 个 Chaos 刚体和 22 个 PhysicsControl 控制消费同一高度或外部冲量，并用父子相对转角量测髋/膝/躯干局部响应；同时以 Chaos pelvis 朝向、接触和速度选择 ALS Front/Back 起身，显式把 PhysicsControl 权威交还动画。高度、骨骼耦合与三进程性能合同均已完成，下一步只剩最终美术场景。

## 已验证的真实引擎切片

- 独立 Game Feature 地图：`/PhysicsDrivenCharacter/Maps/L_PhysicsControlCharacter`，不依赖 Editor Python 执行运行逻辑。
- 两路共享 Manny、`PA_Mannequin`、初始姿态、地面、固定帧率和 `spine_03` 冲量；差异只在主动路的 PhysicsControl 数据路径。
- PhysicsControl 创建 pelvis 以下 Body Modifier、父子空间姿态控制与一个显式世界空间 pelvis 控制；个人代码负责目标位姿、tick 相位、冲量时序、指标、判据和证据导出。
- 6 秒结果中主动姿态误差为被动的 `25.23%`，主动 pelvis 位移为被动的 `1.97%`；这被描述为确定性姿态控制/扰动拒绝，不包装成神经策略或完整自主起身。

## UE / Chaos / Lyra 分层

| 层 | 权威职责 | 本 Case 的实现边界 |
| --- | --- | --- |
| Lyra Gameplay | PawnData 初始化、ASC/GAS、受击事件、服务器权威、重生与输入 | 物理状态请求由服务器验证；客户端只预测或表现，不自行决定恢复成功 |
| Character Movement | 非物理常规移动、胶囊与导航 | 进入全身物理时暂停普通移动权威；恢复成功后才重新交接 |
| Chaos | Physics Asset 刚体、关节、接触、碰撞冲量与世界查询 | 提供真实多刚体状态，不冒充个人策略或学习控制器 |
| Physics Control | 从 Skeletal Animation/目标姿态生成物理控制、分肢控制与 Body Modifier | 作为 UE 5.4 Experimental 运行时桥；配置、相位调度、预算和失败判定由个人代码负责 |
| ALS | Ragdoll/Flail、正反面 Get Up、Land Roll 动画和姿态逻辑参考 | 只迁移明确动画/曲线或重定向结果，不覆盖 Lyra Character/AnimBP/网络架构 |
| 研究控制层 | 相位状态机、目标条件、力矩/能量预算、恢复判定、失败分类 | 先以确定性程序化基准验证，再映射到 UE Skeletal Mesh |

Lyra 的 `ULyraPawnExtensionComponent` 和 `ULyraHeroComponent` 已提供 PawnData/ASC 初始化边界，`ULyraHealthComponent` 的 DeathState 已复制并通过 Gameplay Event 驱动死亡流程。Case05 的“可恢复物理失衡”不能挪用 DeathState：它必须使用独立的可复制状态，避免受击跌倒与死亡/重生互相污染。

## 网络权威合同

- 服务器接收受击/推撞结果，决定是否从 Locomotion 进入 Physical/Fallen；冲量、状态序号、起始姿态和控制配置构成可复制快照。
- 自主客户端可以先播放命中反应，但不能自行提交 `Recovered`。服务器根据姿态误差、接触、速度和稳定保持时间确认恢复，再恢复 Character Movement/GAS 输入。
- 模拟代理消费低频物理状态与相位，局部平滑 Skeletal Mesh；不逐骨骼发送 Reliable RPC。当前 100ms 延迟/5% 丢包实测为一次 29-bit 恢复 RPC，恢复窗口 4 次 FallState 写入、3 次 RecoveryBranch 写入，其中两次为约 328ms 间隔的纠错重发；目标属性 payload 合计 21 bits，Profiler 可归因 token 合计 185 bits，四次更新 P95/P99 为 64 bits。
- DeathState 优先级高于可恢复状态；死亡开始时取消恢复控制，走 Lyra 既有死亡能力和重生生命周期。

上述 RPC bits 是 UE Profiler 记录的完整 RPC 序列化段；属性 payload 和 token 归因也是原始事件的精确值，但不含 Profiler 未单列的 packed content-length，也无法把同包其他 Lyra 流量、包头、ACK 与重传开销无歧义拆给单个组件，因此不称为独立 UDP/IP 线速字节。整场 SocketSendTo 字节只保留为会话证据，不用于组件归因。

## 未实现与禁止夸大

- 未训练 NCP、MaskedMimic、ControlVAE、AMP 或其他 RL/VAE 策略；没有神经 latent、动作先验或 world model。
- 未实现论文的 120 Hz 大规模并行训练、动作数据覆盖或跨角色泛化；ALS 动画存在不等于神经控制器已复现。
- 已接入真实 Manny 多刚体、PhysicsControl、ALS Front/Back Get Up、玩家输入、GameFeature 注入、独立可恢复 GAS 标签和 Lyra 双进程服务器权威复制；这些仍是确定性工程控制，不是神经策略。
- 平面恢复、高度跌落/吸收和多段关节耦合三个标准基准已完成；60cm 高度合同与髋/膝/躯干真实骨骼级 A/B 均已映射到 Manny/Chaos/PhysicsControl。三进程性能门、最终 Portfolio 场景和 55 秒黄金演示均已通过；Case05 已冻结。
- 多刚体 Chaos/Physics Control、同输入基线切换、交互、失败、网络边界、性能和最终场景均已有机器证据；`case05_completion_validation.json` 10/10 后冻结。
