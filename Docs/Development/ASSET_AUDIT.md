# 物理资产审计

## Assassin

- 内容根：`/Game/Assassin`
- 注册资产：141；代表加载失败：0。
- 物理相关：28 个 Skeletal Mesh、1 个 Skeleton、4 个 Physics Asset、23 个 AnimSequence。
- 四个完整 Skin 均绑定对应 Physics Asset，并各有 2 份 Clothing Asset。
- 独立 `S_cloak` 绑定 1 份 Clothing Asset，是首个最终布料候选。
- 已确认这些绑定对象为 UE `ClothingAssetCommon`，且同时保存 `ChaosClothConfig` 与 `ChaosClothSharedSimConfig`，不是只能依赖旧 NvCloth 的未知资产。
- UE 5.4 Python 对该内嵌 Clothing Asset 返回的可见 `lod_data` 为空；这可能是接口未暴露，也可能是迁移后 Section 绑定不完整。进入最终展示前必须用专用 C++/编辑器检查或实际 Chaos Cloth 运行验证，不能只凭 Config 判断可用。
- 其他独立服装件保持骨骼与 Physics Asset 绑定，但未发现 Clothing Asset。
- UE FBX 自动导出 `S_cloak` 时没有生成可供 Blender 读取的 Mesh 对象，因此首个基准网格仍应程序化生成；披风作为最终展示资产，后续从 Cloth Section/LOD 数据或个人派生网格接入。
- 后续验证：确认旧 Cloth 数据在 Chaos Cloth Asset 中的实际类型、Section/LOD 绑定、碰撞体质量与可否生成个人派生版本。

## Four Evil Dragons

- 内容根：`/Game/FourEvilDragonsHP`
- 注册资产：125；代表加载失败：0。
- 物理相关：4 个 Skeletal Mesh、4 个 Skeleton、4 个 Physics Asset、69 个 AnimSequence。
- 四条龙各自拥有独立骨骼与 Physics Asset；未发现 Clothing Asset，符合软体/Boss 碰撞用途。
- 体型包围范围差异明显，`DragonTheUsurper` 最大，适合最终 Boss 画面但不适合作为第一版四面体化输入。
- Blender 拓扑检查显示四条龙都不是可直接四面体化的封闭流形：Nightmare 1138 条边界边/21 个连通分量；SoulEater 1476/74；TerrorBringer 1048/28；Usurper 1754/63。TerrorBringer 的边界边最少，但四者都必须先做封口、分件取舍和自交复检。
- TerrorBringer 现已有本地可重建派生链：体素封口后减面到 6000 三角形，审计为 0 边界边、0 非流形边、1 连通分量。UE LOD0 的 4854 个表面顶点已绑到 729 顶点/3072 四面体规则 Cage，静止重建误差 0.000141 cm。派生 UAsset 只保留本地并按 Fab 许可边界不进入公开源码仓。
- 第一版软体算法继续使用程序化梁、球和材料块。龙只作为经过个人派生修复的最终美术外壳，不能直接送入四面体器。

## 审计限制

- UE 5.4 Python 可以验证资源注册、加载、Skeleton/Physics Asset 绑定、Clothing Asset 数量、材质槽和包围盒。
- UE 5.4 Python 未暴露本项目所需的 Skeleton 骨数、Physics Asset Body/Constraint 数量和 Skeletal Mesh 顶点/LOD 子系统；这些值在报告中保持 `null`，不得解释为 0。
- 原始结构化报告：`Docs/Validation/AddedAssets/physics_asset_audit.json`。
- 几何拓扑报告：`Docs/Validation/AddedAssets/topology_audit.json`；当前报告未检测自交，因此“边界修复后可用”仍需二次验证。
