#include "steamlink_payload.hpp"

#include "fgb/kit/arkit.hpp"  // CleanUnit：NaN/Inf 清洗 + [0,1] 裁剪

#include <cmath>

namespace steamlink {

using fgb::kit::CleanUnit;
using fgb::kit::OscArgument;
using fgb::kit::OscMessage;

namespace {

constexpr std::string_view kGazePointAddress = "/sl/eyeTrackedGazePoint";
constexpr std::string_view kEyesClosedLeft = "/sl/xrfb/facew/EyesClosedL";
constexpr std::string_view kEyesClosedRight = "/sl/xrfb/facew/EyesClosedR";

// 注视方向置信度：Quest 眼动是设备级追踪，但 /sl/eyeTrackedGazePoint 是
// **融合后的单一注视射线**，左右眼共用，逐眼独立方向未确认（见 docs/protocol.md）。
constexpr float kPerEyeGazeConfidence = 0.8f;
constexpr float kCombinedGazeConfidence = 0.9f;

// 一条 /sl/xrfb/facew/* 地址最多驱动两个 Unified Expressions 形状
// （与上游 VRCFT 模块的映射一致，extra 为空表示单目标）。
struct FaceMapEntry {
    std::string_view address;
    std::string_view target;
    std::string_view extra;
};

constexpr std::string_view kPrefixFaceC = "/sl/xrfb/facec/";
constexpr std::string_view kPrefixAvatarParams = "/avatar/parameters/";
constexpr std::string_view kPrefixTrackingEye = "/tracking/eye/";

// 注视点超过这个时间没有更新就失效：头显关掉「分享眼动数据」而面捕继续时，
// 不能继续输出最后一次方向。
constexpr std::uint64_t kGazeTimeoutNs = 500ull * 1000ull * 1000ull;

// 面捕里带眼球方向的 faceblend 项（EyesLook*）不使用：注视以眼动追踪的
// /sl/eyeTrackedGazePoint 为准，两者同时输出会互相打架。
// TongueTipInterdental 在 Unified Expressions 里没有对应形状，Valve 实际发的是
// 拼错的 /sl/xrfb/facew/ToungeTipInterdental；两种拼写都当已知地址接受，
// 避免拼写修正或保留哪个版本时变成未知地址告警。
constexpr std::string_view kIgnoredFacew[] = {
    "/sl/xrfb/facew/EyesLookDownL",        "/sl/xrfb/facew/EyesLookDownR",
    "/sl/xrfb/facew/EyesLookLeftL",        "/sl/xrfb/facew/EyesLookLeftR",
    "/sl/xrfb/facew/EyesLookRightL",       "/sl/xrfb/facew/EyesLookRightR",
    "/sl/xrfb/facew/EyesLookUpL",          "/sl/xrfb/facew/EyesLookUpR",
    "/sl/xrfb/facew/ToungeTipInterdental", "/sl/xrfb/facew/TongueTipInterdental",
};

// clang-format off
constexpr FaceMapEntry kFaceMap[] = {
    {"/sl/xrfb/facew/UpperLidRaiserL", "EyeWideLeft", ""},
    {"/sl/xrfb/facew/UpperLidRaiserR", "EyeWideRight", ""},
    {"/sl/xrfb/facew/LidTightenerL", "EyeSquintLeft", ""},
    {"/sl/xrfb/facew/LidTightenerR", "EyeSquintRight", ""},
    {"/sl/xrfb/facew/InnerBrowRaiserL", "BrowInnerUpLeft", ""},
    {"/sl/xrfb/facew/InnerBrowRaiserR", "BrowInnerUpRight", ""},
    {"/sl/xrfb/facew/OuterBrowRaiserL", "BrowOuterUpLeft", ""},
    {"/sl/xrfb/facew/OuterBrowRaiserR", "BrowOuterUpRight", ""},
    {"/sl/xrfb/facew/BrowLowererL", "BrowPinchLeft", "BrowLowererLeft"},
    {"/sl/xrfb/facew/BrowLowererR", "BrowPinchRight", "BrowLowererRight"},
    {"/sl/xrfb/facew/JawDrop", "JawOpen", ""},
    {"/sl/xrfb/facew/JawSidewaysLeft", "JawLeft", ""},
    {"/sl/xrfb/facew/JawSidewaysRight", "JawRight", ""},
    {"/sl/xrfb/facew/JawThrust", "JawForward", ""},
    {"/sl/xrfb/facew/MouthLeft", "MouthLowerLeft", "MouthUpperLeft"},
    {"/sl/xrfb/facew/MouthRight", "MouthLowerRight", "MouthUpperRight"},
    {"/sl/xrfb/facew/ChinRaiserT", "MouthRaiserUpper", ""},
    {"/sl/xrfb/facew/ChinRaiserB", "MouthRaiserLower", ""},
    {"/sl/xrfb/facew/DimplerL", "MouthDimpleLeft", ""},
    {"/sl/xrfb/facew/DimplerR", "MouthDimpleRight", ""},
    {"/sl/xrfb/facew/LipsToward", "MouthClosed", ""},
    {"/sl/xrfb/facew/LipCornerPullerL", "MouthCornerPullLeft", "MouthCornerSlantLeft"},
    {"/sl/xrfb/facew/LipCornerPullerR", "MouthCornerPullRight", "MouthCornerSlantRight"},
    {"/sl/xrfb/facew/LipCornerDepressorL", "MouthFrownLeft", ""},
    {"/sl/xrfb/facew/LipCornerDepressorR", "MouthFrownRight", ""},
    {"/sl/xrfb/facew/LowerLipDepressorL", "MouthLowerDownLeft", ""},
    {"/sl/xrfb/facew/LowerLipDepressorR", "MouthLowerDownRight", ""},
    {"/sl/xrfb/facew/UpperLipRaiserL", "MouthUpperUpLeft", ""},
    {"/sl/xrfb/facew/UpperLipRaiserR", "MouthUpperUpRight", ""},
    {"/sl/xrfb/facew/LipTightenerL", "MouthTightenerLeft", ""},
    {"/sl/xrfb/facew/LipTightenerR", "MouthTightenerRight", ""},
    {"/sl/xrfb/facew/LipPressorL", "MouthPressLeft", ""},
    {"/sl/xrfb/facew/LipPressorR", "MouthPressRight", ""},
    {"/sl/xrfb/facew/LipStretcherL", "MouthStretchLeft", ""},
    {"/sl/xrfb/facew/LipStretcherR", "MouthStretchRight", ""},
    {"/sl/xrfb/facew/LipPuckerL", "LipPuckerLowerLeft", "LipPuckerUpperLeft"},
    {"/sl/xrfb/facew/LipPuckerR", "LipPuckerLowerRight", "LipPuckerUpperRight"},
    {"/sl/xrfb/facew/LipFunnelerLB", "LipFunnelLowerLeft", ""},
    {"/sl/xrfb/facew/LipFunnelerLT", "LipFunnelUpperLeft", ""},
    {"/sl/xrfb/facew/LipFunnelerRB", "LipFunnelLowerRight", ""},
    {"/sl/xrfb/facew/LipFunnelerRT", "LipFunnelUpperRight", ""},
    {"/sl/xrfb/facew/LipSuckLB", "LipSuckLowerLeft", ""},
    {"/sl/xrfb/facew/LipSuckLT", "LipSuckUpperLeft", ""},
    {"/sl/xrfb/facew/LipSuckRB", "LipSuckLowerRight", ""},
    {"/sl/xrfb/facew/LipSuckRT", "LipSuckUpperRight", ""},
    {"/sl/xrfb/facew/CheekPuffL", "CheekPuffLeft", ""},
    {"/sl/xrfb/facew/CheekPuffR", "CheekPuffRight", ""},
    {"/sl/xrfb/facew/CheekSuckL", "CheekSuckLeft", ""},
    {"/sl/xrfb/facew/CheekSuckR", "CheekSuckRight", ""},
    {"/sl/xrfb/facew/CheekRaiserL", "CheekSquintLeft", ""},
    {"/sl/xrfb/facew/CheekRaiserR", "CheekSquintRight", ""},
    {"/sl/xrfb/facew/NoseWrinklerL", "NoseSneerLeft", ""},
    {"/sl/xrfb/facew/NoseWrinklerR", "NoseSneerRight", ""},
    {"/sl/xrfb/facew/TongueRetreat", "TongueBendDown", ""},
    {"/sl/xrfb/facew/TongueTipAlveolar", "TongueCurlUp", ""},
    {"/sl/xrfb/facew/TongueOut", "TongueOut", ""},
};
// clang-format on

bool Numeric(const OscArgument& argument) {
    return argument.type == fgb::kit::OscType::Int || argument.type == fgb::kit::OscType::Float ||
           argument.type == fgb::kit::OscType::Double;
}

void MergeGazePoint(const OscMessage& message, FrameState& state, Stats& stats,
                    std::uint64_t now_ns) {
    if (message.arguments.size() < 3 || !Numeric(message.arguments[0]) ||
        !Numeric(message.arguments[1]) || !Numeric(message.arguments[2])) {
        // 参数不齐 / 类型不对：把注视判为不可用，而不是保留旧值冒充新数据。
        ++stats.malformed;
        state.gaze_valid = false;
        state.gaze_ns = 0;
        return;
    }
    const float x = message.arguments[0].AsFloat(0.0f);
    const float y = message.arguments[1].AsFloat(0.0f);
    const float z = message.arguments[2].AsFloat(0.0f);
    state.gaze_point[0] = x;
    state.gaze_point[1] = y;
    state.gaze_point[2] = z;
    state.gaze_ns = now_ns;
    state.gaze_valid = std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    ++stats.mapped;
}

void MergeEyelid(std::string_view address, const OscMessage& message, FrameState& state,
                 Stats& stats) {
    if (message.arguments.empty() || !Numeric(message.arguments[0])) {
        ++stats.malformed;
        return;
    }
    const float closed = CleanUnit(message.arguments[0].AsFloat(0.0f));
    if (address == kEyesClosedLeft) {
        state.eye_closed[0] = closed;
    } else {
        state.eye_closed[1] = closed;
    }
    ++stats.mapped;
}

void MergeWeight(const FaceMapEntry& entry, float value, FrameState& state) {
    const std::size_t slot = ShapeSlot(entry.target);
    if (slot < kShapeSlots) {
        state.shapes[slot] = value;
    }
    if (entry.extra.empty()) {
        return;
    }
    const std::size_t extra_slot = ShapeSlot(entry.extra);
    if (extra_slot < kShapeSlots) {
        state.shapes[extra_slot] = value;
    }
}

} // namespace

std::size_t ShapeSlot(std::string_view name) {
    for (std::size_t i = 0; i < kShapeSlots; ++i) {
        if (fgb::kit::kUnifiedExpressionNames[i] == name) {
            return i;
        }
    }
    return kShapeSlots;
}

void MergeMessage(const OscMessage& message, FrameState& state, Stats& stats,
                  std::uint64_t now_ns) {
    ++stats.messages;
    const std::string_view address{message.address};
    if (address == kGazePointAddress) {
        MergeGazePoint(message, state, stats, now_ns);
        return;
    }
    if (address == kEyesClosedLeft || address == kEyesClosedRight) {
        MergeEyelid(address, message, state, stats);
        return;
    }
    if (message.arguments.empty() || !Numeric(message.arguments[0])) {
        ++stats.malformed;
        return;
    }
    const float value = CleanUnit(message.arguments[0].AsFloat(0.0f));
    for (const auto& entry : kFaceMap) {
        if (entry.address == address) {
            MergeWeight(entry, value, state);
            ++stats.mapped;
            return;
        }
    }
    for (const auto& ignored : kIgnoredFacew) {
        if (ignored == address) {
            ++stats.known_ignored;
            return;
        }
    }
    if (address.starts_with(kPrefixFaceC) || address.starts_with(kPrefixAvatarParams) ||
        address.starts_with(kPrefixTrackingEye)) {
        ++stats.known_ignored;
        return;
    }
    ++stats.unknown;
    if (stats.first_unknown.empty()) {
        stats.first_unknown = std::string{address};
    }
}

bool MergeDatagram(std::string_view packet, FrameState& state, Stats& stats,
                   std::uint64_t now_ns, std::string& error) {
    const auto messages = fgb::kit::OscReader::Parse(packet, error);
    if (!messages.has_value()) {
        return false;
    }
    for (const auto& message : *messages) {
        MergeMessage(message, state, stats, now_ns);
    }
    return true;
}

void DeclareTargets(fgb::kit::ShapeTable& table) {
    for (const auto& entry : kFaceMap) {
        table.Declare(entry.target);
        if (!entry.extra.empty()) {
            table.Declare(entry.extra);
        }
    }
}

namespace {

float Openness(float closed, float squint) {
    // 与上游一致：1 - clamp(closed + closed * squint)。CleanUnit 已在合并时
    // 清洗过，Clamp01 对 NaN 返回 0，保证任何输入都落在 [0,1]。
    const float penalty = closed + closed * squint;
    return 1.0f - fgb::kit::Clamp01(penalty);
}

} // namespace

void WriteFrame(const FrameState& state, const fgb::kit::ShapeTable& table, FgbFrameSlot* slot,
                std::uint64_t now_ns) {
    slot->attributes |= FGB_ATTR_EXPRESSION;
    for (std::size_t i = 0; i < kShapeSlots; ++i) {
        const float value = state.shapes[i];
        if (value > 0.0f) {
            table.Write(slot, fgb::kit::kUnifiedExpressionNames[i], value);
        }
    }

    // 睁眼度：EyesClosed* 直接给闭眼量，LidTightener*（EyeSquint）加深闭合。
    const std::size_t squint_left = ShapeSlot("EyeSquintLeft");
    const std::size_t squint_right = ShapeSlot("EyeSquintRight");
    const float squint_l =
        squint_left < kShapeSlots ? state.shapes[squint_left] : 0.0f;
    const float squint_r =
        squint_right < kShapeSlots ? state.shapes[squint_right] : 0.0f;
    slot->attributes |= FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_RIGHT_OPENNESS;
    slot->left_eye.openness = Openness(state.eye_closed[0], squint_l);
    slot->left_eye.openness_confidence = 1.0f;
    slot->right_eye.openness = Openness(state.eye_closed[1], squint_r);
    slot->right_eye.openness_confidence = 1.0f;

    // 注视：3D 点归一化成 HMD 局部单位向量（-Z 前）。点不可用（缺消息、
    // 参数不足、NaN/Inf、零向量、超过 500ms 未更新）时一个方向位都不置——
    // 绝不伪造或沿用旧方向。
    if (!state.gaze_valid) {
        return;
    }
    if (now_ns < state.gaze_ns || now_ns - state.gaze_ns > kGazeTimeoutNs) {
        return;
    }
    const float gx = state.gaze_point[0];
    const float gy = state.gaze_point[1];
    const float gz = state.gaze_point[2];
    if (!std::isfinite(gx) || !std::isfinite(gy) || !std::isfinite(gz)) {
        return;
    }
    const float squared = gx * gx + gy * gy + gz * gz;
    if (!(squared > 1e-12f)) {  // 零向量（含 NaN 兜底）没有方向可言
        return;
    }
    const float length = std::sqrt(squared);
    const float direction[3] = {gx / length, gy / length, gz / length};

    slot->attributes |= FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE | FGB_ATTR_COMBINED_GAZE;
    slot->left_eye.gaze_dir[0] = direction[0];
    slot->left_eye.gaze_dir[1] = direction[1];
    slot->left_eye.gaze_dir[2] = direction[2];
    slot->left_eye.gaze_confidence = kPerEyeGazeConfidence;
    slot->right_eye.gaze_dir[0] = direction[0];
    slot->right_eye.gaze_dir[1] = direction[1];
    slot->right_eye.gaze_dir[2] = direction[2];
    slot->right_eye.gaze_confidence = kPerEyeGazeConfidence;
    slot->combined_gaze_dir[0] = direction[0];
    slot->combined_gaze_dir[1] = direction[1];
    slot->combined_gaze_dir[2] = direction[2];
    slot->combined_gaze_confidence = kCombinedGazeConfidence;
    // 上游没有 2D 注视点：不置 LEFT/RIGHT_POINT，更不置 POINT_VIEWPORT。
}

} // namespace steamlink
