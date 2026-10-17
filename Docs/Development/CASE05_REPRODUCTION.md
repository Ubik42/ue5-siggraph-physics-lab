# Case05 同冲量角色对照：源码与复现

[实机视频和完整作品详情](https://ubik42.github.io/?work=siggraph-physics-lab) · [角色物理技术说明](../Cases/Case05_物理驱动角色.md)

## 当前可复查的内容

公开仓库保存自有插件源码、地图、历史实验资料及 2026-10-17 的同冲量 CSV/JSON。完整开发工程使用 UE 5.4.4 和 Lyra 5.4；公开版本是覆盖层，宿主源码、基础插件和 Lyra 原始内容需要自行准备。

本轮验证了完整开发工程编译、同冲量场景运行及 214 份自有代码文件的一致性。全新机器从公开覆盖层恢复运行仍待验证。

## 先复算保存的数据

安装 Python 3，在本仓根目录执行：

```powershell
python Scripts/Validation/check_portfolio_snapshot.py
```

该命令只读文件，不启动 UE。它检查源码清单、数据指纹、360 条样本的有限值和 60 Hz 间隔、四个终态指标、历史记录一致性，以及本次阅读入口的相对链接。通过后说明保存的源码和记录一致；实时运行需要下列宿主。

## 恢复宿主

1. 从 Epic 获取合法的 **Lyra Starter Game 5.4**，安装 UE **5.4.4**、Visual Studio 2022 的 C++ 游戏开发组件和对应 Windows SDK。
2. 在单独的 Lyra 副本中应用公开覆盖层，保留 Lyra 的 `Source`、基础插件和原始 `Content`。使用 `SIGGRAPHPhysicsLab.uproject`，构建目标仍是 `LyraEditor`。不要将覆盖层直接覆盖日常开发目录。
3. 确认 `PhysicsControl`、`PhysicsResearch`、`PhysicsDrivenCharacter` 及 Lyra 宿主插件可用。Manny 与 `PA_Mannequin` 的具体路径已列在检查脚本中。
4. 当前同冲量地图使用 Lyra Manny、Physics Asset、引擎基础几何和个人舞台；其场景不依赖 Assassin 或 Dragon。完整 Hub 的其他案例有各自资产要求，见[归因说明](ATTRIBUTION.md)。

在公开仓目录检查已恢复的宿主，修改下面两个路径：

```powershell
python Scripts/Validation/check_case05_environment.py `
  --project-root "D:\Projects\SIGGRAPHPhysicsLab" `
  --engine-root "C:\Program Files\Epic Games\UE_5.4"
```

检查仅确认已知文件和引擎版本。资产引用是否完整、模块是否能编译及场景能否运行，需要后续实际加载确认。公开覆盖层单独执行此检查会列出缺失的 Lyra 文件，这是预期结果。

## 编译与运行

在 PowerShell 中设置工程与引擎位置：

```powershell
$projectFile = 'D:\Projects\SIGGRAPHPhysicsLab\SIGGRAPHPhysicsLab.uproject'
$engineRoot = 'C:\Program Files\Epic Games\UE_5.4'
& "$engineRoot\Engine\Build\BatchFiles\Build.bat" LyraEditor Win64 Development "-Project=$projectFile" -WaitMutex
if ($LASTEXITCODE -ne 0) { throw '编译失败，请先处理构建输出。' }

& "$engineRoot\Engine\Binaries\Win64\UnrealEditor.exe" $projectFile `
  '/PhysicsDrivenCharacter/Maps/L_PhysicsControlCharacter' `
  -game -d3d12 -windowed -ResX=1600 -ResY=900 -nosound -nop4 `
  -BENCHMARK -FPS=60 -PhysicsDrivenCharacterAuto -PhysicsDrivenCharacterPortfolio `
  '-ExecCmds=CommonLoadingScreen.HoldLoadingScreenAdditionalSecs 0'
```

该场景同时显示两具 Manny。0.5 秒时，两路 `spine_03` 接受相同冲量，记录 6 秒物理数据，自动模式随后退出。首次运行可能需要等待材质和着色器准备。

输出在恢复工程内：

- `Saved/PhysicsResearch/DrivenCharacterRealtime.csv`：每步姿态角误差与 pelvis 位移等数据。
- `Saved/PhysicsResearch/DrivenCharacterRealtime.json`：引擎版本、场景条件和汇总值。
- `Saved/Logs/`：模块、资产加载及运行日志。

主动路 pelvis 的世界目标固定，`MaxForce/MaxTorque=0` 表示不限制控制力与力矩。跨机器结果需要重新采样；本仓保存的精确终态来自指定引擎版本和本机记录。

## 录制开关

在运行参数中增加 `-PhysicsDrivenCharacterRecord`，会通过 `FFrameGrabber` 读取原生视口，输出 JPEG 和 `frames.csv` 到 `Saved/PhysicsResearch/Captures/<时间>/`。6 秒内隔一帧请求采集，索引记录每张图片对应的仿真时间。

录制会增加墙钟耗时，不能用录制过程测性能。当前入口每个独立进程只支持一轮；画面可能仍受初始材质准备影响。已公开视频保留 0.3—6 秒连续画面，附加 2 秒末帧停留。

## 代码入口

文件均在 `Plugins/GameFeatures/PhysicsDrivenCharacter/Source/PhysicsDrivenCharacterRuntime/`：

| 文件 | 可追查的实现 |
| --- | --- |
| `Private/PhysicsControlCharacterRealtimeActor.cpp` | 两路角色、控制参数、固定 pelvis 目标、Tick 阶段、同冲量与 PostPhysics 采样 |
| `Private/PhysicsPortfolioCapture.cpp` | 原生视口尺寸、JPEG 编码、仿真时间索引与结束时排空帧 |
| `Private/PhysicalGetUpDecision.cpp` | 姿态分支、稳定时间和速度拒绝判据 |
| `Private/PhysicalGetUpRealtimeActor.cpp` | ALS 目标动画、Physics Control 创建/销毁与控制权交接 |
| `Private/RecoverableFallComponent.cpp` | 可恢复倒地状态、死亡优先级与服务器请求 |
| `Private/Tests/` | 程序化基准、失败条件与状态合同 |

起身场景还需要 ALS V4 动作与依赖。仓库已有重定向结果和脚本；原始资源由合法资产包提供。`Scripts/Setup/migrate_als_getup_sources.py` 保留历史机器的固定路径，使用前必须按目标工程调整；同冲量演示不需要执行此迁移脚本。

## 测试时间与适用范围

2026-10-17 复测仅覆盖同冲量场景和录像入口；起身、交互、网络、CPU 性能数据位于 `Docs/Validation/M6`，属于历史实验。连续起身录像、干净机器恢复及真实角色有限力矩预算扫描尚未完成。

[返回首页](../../README.md)
