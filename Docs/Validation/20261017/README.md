# 2026-10-17：角色同冲量复测

[实机视频](https://ubik42.github.io/?work=siggraph-physics-lab) · [恢复与运行](../../Development/CASE05_REPRODUCTION.md) · [完整角色说明](../../Cases/Case05_物理驱动角色.md)

## 文件

| 文件 | 内容 |
| --- | --- |
| [DrivenCharacterRealtime.csv](DrivenCharacterRealtime.csv) | 本机 UE 5.4.4 中的 360 条原始采样 |
| [DrivenCharacterRealtime.json](DrivenCharacterRealtime.json) | 场景参数与导出指标，保留原始字段 |
| [comparison.json](comparison.json) | 本次与历史记录的日期、比较范围、数据 SHA-256 和录像时段 |
| [source-manifest.json](source-manifest.json) | 六个自有插件共 214 份代码、Shader、描述与配置的归一化 SHA-256 |

源码清单来源为完整开发工程 `0fb97cd`。源码比对忽略 UTF-8 BOM 与 CRLF/LF 的差别；CSV/JSON 指纹按文件字节计算。清单不包括 Lyra 宿主、内容资产、生成物和工程历史。

## 测量条件

两路各 22 个刚体；主动路具有 21 个父空间控制及一个固定 pelvis 世界目标。控制力和力矩不限幅。0.5 秒向 `spine_03` 施加 `(0,5200,0) kg·cm/s` 冲量，60 Hz 固定步持续 6 秒。

姿态指标是各刚体相对初始姿态的平均角误差；位移指标是 pelvis 相对初始位置的距离。四个终态指标与 2026-08-30 的 `M6/DrivenCharacterPortfolio.json` 在 1e-6 容差内一致。

原始 JSON 中 `passive_ragdoll_collapse_observed` 与 `research_bounded_landing_pass` 为 false。导出器包含多个场景共用字段；本次场景为 `SharedSpineImpulse`，不能把 JSON 内每个布尔值都解释成此场景的通过条件。离线脚本检查场景类型、输入条件、原始样本和数值，保留这些字段原貌。

## 检查命令

在仓库根目录执行：

```powershell
python Scripts/Validation/check_portfolio_snapshot.py
```

通过表示保存的文件及该组数据一致。它不会运行物理求解，也不证明干净机器部署、起身、弱网或性能测试通过。M6 其他资料保留原实验时间与范围。

## 录像

真实视口按仿真时间记录为 JPEG，公开视频选取 0.3—6 秒，并追加 2 秒末帧停留，总长 7.7 秒。写出 JPEG 会增加墙钟耗时，此次运行不作为性能采样。完整帧序列保留在开发工程，公开仓提供代码与数值数据，视频由个人主页播放。
