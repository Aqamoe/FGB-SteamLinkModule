# FGB SteamLinkModule

[![build](https://github.com/Aqamoe/FGB-SteamLinkModule/actions/workflows/ci.yml/badge.svg)](https://github.com/Aqamoe/FGB-SteamLinkModule/actions/workflows/ci.yml)

**状态：0.1.0，未验证（untested）**。协议解析、映射与 ABI 生命周期有单元测试和合成 OSC
端到端冒烟；真机（Steam Link + Meta Quest 眼动/面捕）验收待补，见 [docs/verification.md](docs/verification.md)。
Valve 未公开该协议，本模块按社区逆向结果实现，版本更新可能失效。

[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge) 的 Steam Link（Meta Quest
眼动/面捕）原生模块。直接接收**本机 Steam Link 客户端**转发到 UDP 9015 的 OSC 数据，
支持 Valve 按网络状况拆分的 bundle 重组，输出眼动注视方向、睁眼度与 Unified
Expressions 面捕表情。不需要 VRCFT，不注入任何进程，也不需要管理员权限。

Steam Link (Meta Quest eye / face tracking) native input module for FaceGaze Bridge.
Reads the OSC stream the local Steam Link PC client forwards to UDP 9015 — including
Valve's split `#bundle` datagrams — and outputs device-grade gaze direction, eye
openness and Unified Expressions face-tracking shapes. No VRCFT, no process
injection, no admin rights.

## 功能 / Features

- **注视**：`/sl/eyeTrackedGazePoint` 的 3D 注视点归一化成 HMD 局部单位向量
  （-Z 前），逐眼 + 合并通道共用这条融合射线；点不可用时**不置方向位，绝不伪造**。
- **睁眼度**：`EyesClosedL/R` + `LidTightener*`，沿用上游公式
  `1 - clamp(closed + closed*squint)`。
- **表情**：56 条 `/sl/xrfb/facew/*`（Quest FaceBlend 口径）→ Unified Expressions，
  与上游 VRCFT 模块同一张映射表。
- **bundle 重组**：每个 UDP 数据报独立解析，跨包按地址累积最新值；
  未知地址、参数缺失、NaN/越界都有计数与日志。
- **断连可恢复**：10 秒没有数据时在模块卡片提示原因，数据恢复后自动回到活跃。

## 不提供 / Not provided

| 数据 | 原因 |
|---|---|
| 2D 注视点 | 协议里没有屏幕/视口坐标点（只有 3D 注视点），因此也**不**声明视口标定（`POINT_VIEWPORT`） |
| 逐眼独立方向 | `/sl/eyeTrackedGazePoint` 是融合射线，左右眼共用；逐眼差异未确认 |
| vergence | 单一射线推不出辐辏距离 |
| 瞳孔 | 协议无此数据（上游 VRCFT 模块硬编码 5mm，本模块直接不提供） |
| 头姿 | 协议中无头姿地址 |

## 环境要求

| 项 | 要求 |
|---|---|
| 系统 | Windows 10 / 11 x64 |
| 宿主 | FaceGaze Bridge（原生模块 ABI v2） |
| 头显 | Meta Quest（支持眼动/面捕的型号），用 Steam Link 串流 PC |
| 其它 | 关闭 VRCFT：它的 SteamLink 模块和本模块抢同一个 9015 端口 |

## 安装

1. 从 Releases 下载 `steamlink-0.1.0.zip`。
2. FaceGaze Bridge →「模块库 → 原生模块 → 安装原生模块 (.zip)」，或「模块」页「从 .zip 安装」。
3. 「模块」页把 **Steam Link** 设为启动，角色按需要分配 `gaze`（注视）与 `expression`（表情）。
4. 重启引擎；头显里 Steam Link 开启 OSC 后模块卡片应显示活跃。

## 配置

模块目录下可选 `config.ini`：

```ini
port = 9015
```

改完重启引擎，并同步修改 Steam Link 设置里的 OSC 输出端口。未知键会被记录一条警告，
不会静默失效。

## 从源码构建

需要 Visual Studio 2022（x64）、CMake ≥ 3.24 与 Node 20（脚本用）。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

SDK 与 Kit 由 `sdk.lock.json` / `kit.lock.json` 在配置阶段拉取并校验 SHA256，仓库里不含 SDK 头文件副本。
本地想指向已解压的 Kit：

```powershell
cmake -S . -B build -A x64 -DFGB_KIT_DIR="../FGB-Module-Kit"
```

打包与测试：

```powershell
cmake --build build --config Release --target package-modules   # build/Release/steamlink-<版本>.zip
ctest --test-dir build -C Release --output-on-failure           # 协议与映射单测
cmake --build build --config Release --target smoke-test        # 真实 ABI + 合成 OSC 冒烟（需 9015 空闲）
node ../FGB-Module-Kit/tools/audit.mjs --module --dir . --tag v0.1.0
```

## 目录结构

```
src/main.cpp                 ABI 生命周期、UDP 接收、bundle 跨包合并、状态上报
src/steamlink_payload.*      OSC 消息映射与帧写入（facew → Unified Expressions）
module/                      fgb-native.json 与图标
tests/                       协议单测、bundle 重组单测、真实 ABI 冒烟
docs/protocol.md             线格式与映射依据（逆向所得，标注确认状态）
docs/verification.md         真机验收清单
cmake/                       依赖锁解析（SDK / Kit 都是拉取，不复制）
```

## 排错

| 现象 | 处理 |
|---|---|
| 卡片显示「无法绑定 UDP 9015：端口已被占用」 | 关闭 VRCFT 或其它 OSC 接收端；或用 `config.ini` 换端口并同步改 Steam Link 设置 |
| 一直提示「还没收到任何数据包」 | 确认头显 Steam Link 设置里「启用 OSC」「分享眼动/面捕数据」都已打开，且正在串流本机 |
| 表情不动、注视正常 | 头显里没开面捕分享，或角色没分给 `expression` |
| 注视方向整体偏/反 | 协议坐标系未公开确认，提交 issue 并附头显与 Steam Link 版本 |
| 日志出现「无法识别的 OSC 地址」 | Valve 改了地址表；把版本号和日志一并提到 issue |

## 致谢 / Attribution

- 线格式与映射依据逆向自 [LinkFT / SteamLink VRCFT Module](https://github.com/ykeara/LinkFT)
  （ykeara，C#/MIT）；本仓库是该线格式的独立 C++ 实现，未包含其源码。
- ABI 头与宿主：[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge)；共享工具链：[FGB-Module-Kit](https://github.com/Aqamoe/FGB-Module-Kit)。

## 许可

Apache License 2.0，见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。
