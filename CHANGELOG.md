# 更新日志

遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 与语义化版本。

## [未发布]

## [0.1.0] - 未发布
### 新增
- Steam Link OSC（本机 UDP 9015，`/sl/*`）→ 眼动注视方向、睁眼度与 Unified Expressions 面捕表情。
- bundle 跨包重组：Valve 按网络状况拆分的多个 `#bundle` 数据报按地址累积合并。
- 56 条 `/sl/xrfb/facew/*` → Unified Expressions 映射（与上游 VRCFT 模块同表）。
- 未知地址 / 参数缺失 / NaN / 越界全部计数并清洗，不静默污染帧。
- 注视点不可用时不置任何方向属性（绝不伪造 (0,0,-1)）。
- `config.ini` 支持覆盖端口。
- 协议单测 + bundle 单测 + 通过真实 ABI 的端到端冒烟测试。

### 已知限制
- Valve 未公开该协议，实现基于社区逆向（ykeara/LinkFT），未真机验证，状态为未验证。
- `/sl/eyeTrackedGazePoint` 的坐标系（是否头部局部、单位）未公开确认。
- 上游两处自标「需要再核对」的后处理（MouthUpperDeepen / LipSuckUpper）未移植。
