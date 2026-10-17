# 角色受击、物理起身与恢复移动

[实机视频](https://ubik42.github.io/?work=siggraph-physics-lab) · [Case05](../Cases/Case05_物理驱动角色.md) · [宿主准备](https://github.com/Ubik42/ue5-siggraph-physics-lab/blob/main/Docs/Development/CASE05_REPRODUCTION.md)

连续模式使用标准 ACharacter 串联走动、受击、Chaos 倒地、稳定判定、ALS 仰卧起身、站立目标追踪与 CharacterMovement 恢复。录像通过脚本调用操作入口，在 UE 5.4.4 单机、60 Hz 固定游戏步长下运行。

## 倒地解耦与稳定状态估计

受击时停用 CharacterMovement 与胶囊体碰撞，分离 Mesh，关闭 Physics Asset 的关节驱动并交由 Chaos 模拟。起身请求只记录意图；接触地面后，骨盆变换有小幅往返，原始速度也持续波动，因此采集 pelvis 刚体的实际世界变换，以变换差分和 0.12 秒时间常数滤波估计稳定运动。

原始速度超过 100 cm/s 或 4 rad/s 时直接拒绝起身。估计速度须持续 0.35 秒低于 18 cm/s 和 0.9 rad/s，且姿态明确、处于接地状态，才进入起身阶段。原始速度、估计速度和状态变化均输出 CSV。

## 动画目标驱动物理，再归还移动控制

起身前根据朝向选择 ALS Front / Back 片段，视频展示本次仰卧分支。21 个父空间控制与 1 个 pelvis 世界空间控制驱动身体。重定位动画目标坐标系时，保存并恢复实际刚体的世界位姿与速度；固定目标坐标系，避免驱动结果反过来移动自身目标。

ALS CLF 动作结束在蹲姿，随后继续追踪站立 Idle。骨盆相对目标地面高于 80 cm，与初始直立旋转的角距离小于 0.5 rad，原始线速度低于 100 cm/s，并保持 0.25 秒后，销毁 Controls 与 Body Modifiers，关闭骨骼模拟，重新附着 Mesh，切回 `MOVE_Walking`。恢复后的移动用于检查控制权确实归还。

## 2026-10-17 实测

| 仿真时间 | 事件 |
| --- | --- |
| 1—2 s | CharacterMovement 前进 |
| 2 s | 停止移动，向脊柱施加冲量，进入倒地 |
| 2.15 s | 请求起身；当前条件未满足，继续等待 |
| 5.25 s | 稳定判据通过，开始 Back 起身 |
| 7.35 s | 站姿保持 0.25 秒，清理 22 个控制，恢复移动 |
| 后续 | 前进 178.31 cm，骨盆模拟关闭，MovementMode 保持 Walking |

完整记录 18.5 秒、1110 条 PostPhysics 样本。视频为 0.3—12.3 秒的连续节选，360 个原生画面采样；裁去上方调试文字并叠加阶段说明，无声，没有加速或末帧延长。固定步和录帧开销不用于推导实际运行帧率。

数据位于 `Docs/Validation/20261017/GetUp/`：CSV 含每帧阶段、实际分支、原始/估计速度、骨盆位置、物理模拟开关、控制数及胶囊位置；JSON 保存终态，`getup-evidence.json` 保存指纹和阶段时间。CSV 的速度在已回交动画后只保留调试意义，不能解读为模拟刚体速度。

## 运行入口

在已恢复并编译的 Lyra 5.4 宿主中，使用地图：

```text
/PhysicsDrivenCharacter/Maps/L_PhysicalGetUpHandoff
```

手动模式附加 `-game -PhysicalGetUpContinuous`。`W/S` 前后移动，`P` 触发受击，`Space` 请求起身。相机固定，属于平地衔接原型。

自动录像额外附加：

```text
-PhysicalGetUpContinuousAuto -PhysicsDrivenCharacterRecord
-PhysicsCaptureDuration=18 -BENCHMARK -FPS=60
```

自动模式在 18.5 秒仿真时间后导出到 `Saved/PhysicsResearch/` 并正常退出。帧序列存入 `Captures/<时间戳>/`。省略 Record 可运行相同流程而不写图片。

关键实现：

- `PhysicalGetUpContinuous.cpp`：角色与输入、受击、目标坐标、恢复移动、数据导出。
- `PhysicalGetUpRealtimeActor.cpp`：接触与运动估计、起身控制创建、站姿检查及控制清理。
- `PhysicsPortfolioCapture.cpp`：可选视口记录，默认原同冲量案例仍录 6 秒。

以上文件均位于 `Plugins/GameFeatures/PhysicsDrivenCharacter/Source/PhysicsDrivenCharacterRuntime/Private/`。

## 范围

Manny、基础行走/Idle 来源于 Lyra，起身动作来自 ALS，Chaos、Physics Control、CharacterMovement 由 Epic 提供。个人实现为状态衔接、测量、输入与运行验证。参考 ALS-Refactored 的 Mesh 分离、模拟初始化和直接读刚体变换做法。

当前控制力未设上限，动画切换尚未加入惯性化混合。连续模式覆盖单机平地仰卧路径；Front 分支、复杂地形、障碍处胶囊恢复与网络物理同步需要分别补测。手动模式仍为开发原型，未做长时间运行测试。

旧三路测试中强制把实际判定改为 `RejectMoving` 的两处代码已移除。历史成功标记只代表当时脚本结果，不能作为当前高速拒绝的独立证据；本次保留实际判定和原始样本。
