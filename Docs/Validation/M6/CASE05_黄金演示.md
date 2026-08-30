# Case05 / 55 秒黄金演示

总时长：约 55 秒。目标是让面试官先看到同输入 A/B，再看到状态交接与失败边界，最后落到可复算证据；不把 ALS 动画、Epic PhysicsControl 或 Lyra 网络能力包装成个人求解器。

## 0–12 秒：同冲量真实 Chaos A/B

- 进入 `/PhysicsDrivenCharacter/Maps/L_PhysicsControlCharacter` 的 Portfolio 模式。
- 画面同时保留左侧被动 Chaos 和右侧 Chaos + Physics Control；0.5 秒向两路 `spine_03` 施加同一 `5200 kg·cm/s` 冲量。
- 停在 6 秒结果：左侧倒地、右侧 pelvis 保持约 3cm 位移；HUD 报告 `107.60°` 对 `27.14°`，并强调同资产、同冲量、固定 60Hz、360 帧。

## 12–30 秒：Chaos 状态驱动 ALS 起身交接

- 切换 `/PhysicsDrivenCharacter/Maps/L_PhysicalGetUpHandoff`。
- 同屏展示 Front、Back 与高速拒绝三路：pelvis 朝向决定正反面，grounded、速度、角速度和 0.35 秒稳定保持共同决定是否允许恢复。
- 指出起身阶段由 Epic PhysicsControl 追随重定向后的 ALS 动作；完成时销毁 Control/BodyModifier，再把权威交回动画与 Character Movement。

## 30–42 秒：玩家输入与失败边界

- 用 `1/2` 选择俯卧/仰卧，`P` 施加物理扰动，`Space` 只请求起身，`R` 重置。
- 展示高速通道 `RejectMoving`：请求不等于许可，客户端或输入层不能直接提交 Recovered。
- 口述 DeathState 始终优先，可恢复跌倒使用独立服务器权威状态。

## 42–55 秒：证据与归因

- 打开三份结果：真实骨骼相对角、60cm 高度映射、三进程性能门。
- 核心数字：髋/膝/躯干峰值 `38.65/65.45/51.48° → 5.86/8.70/15.61°`；性能 pooled P95/P99 为端到端 `1.277/1.430ms`、PhysicsControl 更新 `0.0506/0.0943ms`。
- 结尾归因：Chaos 提供刚体、约束、接触和世界查询；Epic PhysicsControl 提供物理驱动桥；ALS 提供起身动作；个人工作是标准基准、相位/权威编排、A/B、失败门、网络归因、性能协议和证据导出。
