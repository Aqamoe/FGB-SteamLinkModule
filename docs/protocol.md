# Steam Link OSC 协议要点（逆向所得）

> **重要声明**：Valve **没有公开** Steam Link 的这套 OSC 协议。本文与代码里的
> 映射全部逆向自社区 VRCFT 模块 [ykeara/LinkFT](https://github.com/ykeara/LinkFT)
> （C#/MIT，registry 名 “SteamLink VRCFT Module”，DLL `SteamLinkVRCFTModule.dll`）
> 及其附带的[非官方规格笔记](https://github.com/ykeara/LinkFT/blob/main/docs/SteamLinkSpec.md)。
> 该上游自己也标注 “EXPERIMENTAL and may change without notice”。
> 所有「已确认」仅指**与上游接收/解析代码一致**，头显固件或 Steam Link 版本更新
> 都可能改变格式，**必须真机复核**（见 docs/verification.md）。

## 传输

- OSC 1.0 over **UDP 9015**（Steam Link 设置中的 “OSC Output Port”，上游默认 9015）。
  【已确认：上游 `OSCHandler` 以 `DEFAULT_PORT = 9015` 绑定 `127.0.0.1`】
- 发送方是**本机**的 Steam Link PC 客户端：头显的眼动/面捕数据经串流通道回到 PC，
  由 PC 端转成 OSC 发到 9015。因此不需要路由器/防火墙放行，但同机只能有一个接收者。
  【已确认：上游只绑 127.0.0.1；本模块绑 INADDR_ANY:9015，兼容远程发送端】
- **bundle 拆分**：一次面捕/眼动的参数会被按网络状况拆成**多个 bundle、多个数据报**
  （上游规格笔记原文：“Bundles appear to be somewhat fluid based on Network Conditions”，
  其示例即把同一组 `/sl/xrfb/facew/*` 拆成 3 个 BUNDLE）。每个数据报自身是完整可解析的
  OSC 包（`#bundle` 或单消息）；「重组」= 跨数据报按地址累积最新值，而不是字节级拼接。
  【已确认：上游逐数据报独立解析、写入同一份状态表】

## 地址与语义

### 眼动（头显里开启 “Share eye tracking data”）

| 地址 | 值 | 语义与用法 | 状态 |
|---|---|---|---|
| `/sl/eyeTrackedGazePoint` | 3 × float | **注视点是 3D 点**（上游规格：3 floats，representing a 3d point in space the user is looking at），不是 2D。本模块按上游用法把它当作**头部局部、-Z 前**的方向向量归一化后写 gaze 方向 | 【已确认】（3D/float×3 与上游一致）；坐标系假定头部局部（上游 `atan2(x, -z)` 的用法隐含），**绝对坐标系与单位（米？射线？）未确认** |
| `/sl/xrfb/facew/EyesClosedL` / `EyesClosedR` | 1 × float | 闭眼度，`[0,1]`（1=全闭）。睁眼度 `1 - clamp(closed + closed*squint)`，squint 取 `LidTightenerL/R` | 【已确认：上游特判写入 eyelids[] 并同公式】 |
| `/avatar/parameters/LeftEyeX/Y`、`RightEyeX/Y` | 1 × float | VRC 头像旧参数（眼球偏转）。与注视点重复，值域未核对 | 【未确认；本模块忽略并计数】 |
| `/avatar/parameters/*EyeLid*`、`*SqueezeToggle`、`*WidenToggle` | float/int | VRC 头像眼睑参数/开关 | 【未确认；忽略】 |
| `/tracking/eye/CenterVecFull` | float × 3? | 疑似注视中心向量 | 【未确认：上游未使用；忽略】 |
| `/tracking/eye/EyesClosedAmount` | 1 × float | 疑似合并闭眼度 | 【未确认：上游未使用；忽略】 |

**没有 2D 屏幕注视点**。上游协议里不存在逐眼 2D 坐标，因此本模块从不置
`FGB_ATTR_LEFT/RIGHT_POINT`，也**绝不**置 `FGB_ATTR_POINT_VIEWPORT`（ADR-0033 的
「已标定到该眼显示视口」条件无从确认）。方向来自 3D 注视射线，属真实眼动数据；
当注视消息缺失、参数不足 3 个、含 NaN/Inf、为零向量，或**距上次有效注视超过
500ms** 时，本模块**不置任何方向位**，绝不伪造 `(0,0,-1)`、也不沿用旧方向。
500ms 是为了在头显关闭「分享眼动数据」而面捕仍在发送时及时失效。

### 面捕（头显里开启 “Share Face tracking data”）

`/sl/xrfb/facew/<名>`，1 × float，权重语义为 **Meta Quest 面捕（XR Faceblend，
`facew` ≈ FaceBlend weights）** 的系数名，上游按 `[0,1]` 直接使用（`EyesLook*`
按 faceblend 文档是 `[-1,1]`，本模块不使用）。【系数命名已确认；值域除 `EyesLook*`
外未逐一核对，真机需要确认是否出现 >1 或负值】

`/sl/xrfb/facec/LowerFace`、`UpperFace`：疑似分区域置信度/开关（facec ≈ confidence?），
上游未使用。【未确认；忽略并计数】

## facew → Unified Expressions 映射

目标名全部使用 FGB 的 Unified Expressions 88 形状（`shape_names.hpp` 口径），
与上游 VRCFT 的 `UnifiedExpressions` 枚举一一对应【已确认：逐条取自上游
`mapOSCDirectXRFBUnifiedExpressions` 字典】：

| 来源（/sl/xrfb/facew/） | 目标形状 |
|---|---|
| UpperLidRaiserL/R | EyeWideLeft/Right |
| LidTightenerL/R | EyeSquintLeft/Right |
| InnerBrowRaiserL/R | BrowInnerUpLeft/Right |
| OuterBrowRaiserL/R | BrowOuterUpLeft/Right |
| BrowLowererL/R | BrowPinchLeft/Right + BrowLowererLeft/Right |
| JawDrop / JawSidewaysLeft / JawSidewaysRight / JawThrust | JawOpen / JawLeft / JawRight / JawForward |
| MouthLeft / MouthRight | MouthLower+MouthUpper（同侧两份） |
| ChinRaiserT / ChinRaiserB | MouthRaiserUpper / MouthRaiserLower |
| DimplerL/R | MouthDimpleLeft/Right |
| LipsToward | MouthClosed |
| LipCornerPullerL/R | MouthCornerPull + MouthCornerSlant（同侧两份） |
| LipCornerDepressorL/R | MouthFrownLeft/Right |
| LowerLipDepressorL/R | MouthLowerDownLeft/Right |
| UpperLipRaiserL/R | MouthUpperUpLeft/Right（上游标注 “something odd here”，真机复核） |
| LipTightenerL/R | MouthTightenerLeft/Right |
| LipPressorL/R | MouthPressLeft/Right |
| LipStretcherL/R | MouthStretchLeft/Right |
| LipPuckerL/R | LipPuckerUpper+LipPuckerLower（同侧两份） |
| LipFunnelerLB/LT/RB/RT | LipFunnelLower/Upper × 左右 |
| LipSuckLB/LT/RB/RT | LipSuckLower/Upper × 左右 |
| CheekPuffL/R | CheekPuffLeft/Right |
| CheekSuckL/R | CheekSuckLeft/Right |
| CheekRaiserL/R | CheekSquintLeft/Right |
| NoseWrinklerL/R | NoseSneerLeft/Right |
| TongueRetreat / TongueTipAlveolar / TongueOut | TongueBendDown / TongueCurlUp / TongueOut |
| ToungeTipInterdental / TongueTipInterdental | **不映射**（UE 88 形状里没有对应项），两种拼写都按已知地址接受 |
| EyesLookDown/Left/Right/Up × L/R | **不使用**（注视以 `/sl/eyeTrackedGazePoint` 为准），忽略并计数 |

真机日志确认 Valve 实际发送的是拼错的 `/sl/xrfb/facew/ToungeTipInterdental`
（`Tounge`，不是 `Tongue`）。本模块同时接受正确拼写 `TongueTipInterdental`，
两种都不会计入未知地址；Valve 以后修掉拼写或继续保留都能工作。该系数在 Unified
Expressions 里没有对应形状，因此只接收、不驱动表情。

上游字典还提到可能存在的 `FrontDorsalPalate`、`MidDorsalPalate`、
`BackDorsalVelar`（注释掉，未映射到 UE）。【未确认是否真的发送；若发送会计入未知地址，
日志每 5 秒最多提示一次】

上游模块还有两处自标 “?? fix some weird ft things need to check” 的后处理：
`MouthUpperDeepen* = max(0, MouthUpperUp* − NoseSneer*)` 与
`LipSuckUpper* = min(1 − MouthUpper*^(1/6), LipSuckUpper*)`。本模块**未移植**这两处
启发式（上游自己不确定），真机验收时若发现上唇内翻/上提失真再考虑加入。

## 语义与置信度

- 注视射线是**融合后的单一 3D 点**，无逐眼方向、也无从推vergence：
  左右眼与合并通道共用同一条归一化射线。逐眼 `gaze_confidence = 0.8`、
  合并 `0.9`（设备级眼动，高于摄像头估计；「融合射线冒充逐眼」的折价见上）。
- 睁眼度 `openness_confidence = 1.0`（上游同样直接采信）。
- 不提供瞳孔（协议中无此数据）、头姿（协议中无此地址）、2D 注视点。

## 能力声明依据

- `expression`：facew 映射为已确认的数据通路。
- `eyes`：`EyesClosedL/R` + `LidTightener*` 给出逐眼睁眼度（属性级声明，无瞳孔）。
- `gaze`：`/sl/eyeTrackedGazePoint` 是真实眼动的注视射线（3D），可写方向；
  没有 2D 点，所以不声明任何视口标定。

## 冲突

同一端口只能有一个接收者。VRCFT（其 SteamLink 模块）、其它 OSC 中继都可能占
9015；绑定失败时模块卡片会给出端口与原因。改端口：在模块目录写 `config.ini`：

```ini
port = 9016
```

并在 Steam Link 设置里同步改 “OSC Output Port”。
