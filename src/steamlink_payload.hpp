#pragma once
// Steam Link OSC 数据流的解析与帧写入（跨数据包的 bundle 重组）。
//
// 上游线格式（Valve 未公开，逆向自社区 VRCFT 模块，见 docs/protocol.md）：
//   OSC over UDP 9015，本机 Steam Link 客户端把头显的眼动/面捕数据转成 OSC 发出。
//   - /sl/eyeTrackedGazePoint        ：3 个 float，注视点（头部局部空间的 3D 点）
//   - /sl/xrfb/facew/<FaceBlend 名>  ：1 个 float [0,1]，面捕系数（Meta FaceBlend 口径）
//   - /sl/xrfb/facew/EyesClosedL/R   ：1 个 float [0,1]，闭眼度
//   - /sl/xrfb/facec/*、/avatar/parameters/*、/tracking/eye/*：已知但不使用
// Valve 会按网络状况把一组参数拆成多个 bundle（甚至多个数据报），所以状态
// 必须跨包累积：每个数据报各自完整解析，命中的地址逐地址覆盖。

#include "fgb/kit/host.hpp"
#include "fgb/kit/osc.hpp"
#include "fgb/kit/shape_names.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace steamlink {

// 表情权重按 FGB Unified Expressions 的顺序存放（下标 = 名字在
// kUnifiedExpressionNames 中的位置），写帧时直接查表，不做二次改名。
constexpr std::size_t kShapeSlots = fgb::kit::kUnifiedExpressionCount;

struct FrameState {
    std::array<float, kShapeSlots> shapes{};
    float eye_closed[2] = {0.0f, 0.0f};  // [0]=左 [1]=右，EyesClosedL/R
    float gaze_point[3] = {0.0f, 0.0f, 0.0f};
    bool gaze_valid = false;  // 最近一次注视点消息可用（参数齐全且有限）

    void Reset() { *this = FrameState{}; }
};

struct Stats {
    std::size_t messages = 0;       // 合并进状态的 OSC 消息总数
    std::size_t mapped = 0;         // 命中映射（含注视点与眼睑特判）
    std::size_t known_ignored = 0;  // 文档里出现但本模块不用的地址
    std::size_t unknown = 0;        // 未知地址，忽略并计数
    std::size_t malformed = 0;      // 参数缺失或类型不对
    std::string first_unknown;      // 第一条未知地址，日志用
};

// 解析一个 UDP 数据报（单消息或 #bundle，允许多条消息）并合并进 state。
// 返回 false 表示整个数据报非法（调用方丢弃并记日志），此时 state 不变。
bool MergeDatagram(std::string_view packet, FrameState& state, Stats& stats, std::string& error);

// 一条 OSC 消息 → 状态更新。单独暴露给测试，便于逐地址验证。
void MergeMessage(const fgb::kit::OscMessage& message, FrameState& state, Stats& stats);

// 登记本模块会写的全部 Unified Expressions 目标名。
void DeclareTargets(fgb::kit::ShapeTable& table);

// 写表情 + 睁眼度 + 注视方向，并置上对应的 attributes 位。
// 注视点不可用（没收到 /sl/eyeTrackedGazePoint、参数不足或含 NaN/Inf/零向量）
// 时不置任何方向位——绝不伪造 (0,0,-1)。上游没有 2D 注视点，本模块也
// 从不置 LEFT/RIGHT_POINT 或 POINT_VIEWPORT。
void WriteFrame(const FrameState& state, const fgb::kit::ShapeTable& table, FgbFrameSlot* slot);

// 查目标名在 kUnifiedExpressionNames 中的下标；未收录返回 kShapeSlots。
std::size_t ShapeSlot(std::string_view name);

} // namespace steamlink
