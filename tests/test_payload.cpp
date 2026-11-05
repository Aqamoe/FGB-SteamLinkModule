#include "steamlink_payload.hpp"

#include "fgb/kit/module.hpp"
#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/testing/host_mock.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace fgb::kit;
using steamlink::FrameState;
using steamlink::Stats;

namespace {

// 具名构造函数而不是 lambda：MSVC 14.36 对「捕获数组 + lambda 内下标」会 ICE。
OscArgument FloatArg(float value) {
    OscArgument argument;
    argument.type = OscType::Float;
    argument.number = value;
    return argument;
}

OscArgument StringArg(std::string_view value) {
    OscArgument argument;
    argument.type = OscType::String;
    argument.text = std::string{value};
    return argument;
}

OscMessage FloatMessage(std::string_view address, std::vector<float> values) {
    OscMessage message;
    message.address = std::string{address};
    for (const float value : values) {
        message.arguments.push_back(FloatArg(value));
    }
    return message;
}

struct ExpectedPair {
    std::string_view address;
    std::string_view first;
    std::string_view second;  // 空串 = 单目标
};

// 与 docs/protocol.md 的映射表一致（逆向自上游 VRCFT 模块）。
constexpr ExpectedPair kExpected[] = {
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

float UnitLength(const float (&v)[3]) {
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// 多数用例不关心时间，统一用同一个宿主时刻；注视失效用例直接调原函数。
constexpr std::uint64_t kNow = 1'000'000'000ull;

void Merge(const OscMessage& message, FrameState& state, Stats& stats) {
    steamlink::MergeMessage(message, state, stats, kNow);
}

void Write(const FrameState& state, const ShapeTable& table, FgbFrameSlot* slot) {
    steamlink::WriteFrame(state, table, slot, kNow);
}

} // namespace

int main() {
    testing::HostMock host_mock;
    Host host{host_mock.services()};
    ShapeTable table;
    steamlink::DeclareTargets(table);
    CHECK(table.Build(host));
    CHECK_EQ(table.ResolvedCount(), table.Size());  // 目标名必须都是真实形状

    // ---- 已知地址逐项映射：每条 /sl/xrfb/facew/* 都要落到正确的形状 ----
    for (const auto& expected : kExpected) {
        FrameState state{};
        Stats stats{};
        Merge(FloatMessage(expected.address, {0.7f}), state, stats);
        CHECK_EQ(stats.mapped, static_cast<std::size_t>(1));
        CHECK_EQ(stats.unknown, static_cast<std::size_t>(0));
        FgbFrameSlot slot{};
        slot.struct_size = sizeof(FgbFrameSlot);
        slot.abi_version = FGB_MODULE_ABI_V2;
        slot.shape_count = FGB_MAX_SHAPES;
        Write(state, table, &slot);
        CHECK_NEAR(slot.shapes[host_mock.IndexOf(expected.first)], 0.7, 1e-6);
        if (!expected.second.empty()) {
            CHECK_NEAR(slot.shapes[host_mock.IndexOf(expected.second)], 0.7, 1e-6);
        }
    }

    // ---- 注视点：3D 点归一化成方向；双眼与合并通道共用融合射线 ----
    {
        FrameState state{};
        Stats stats{};
        Merge(FloatMessage("/sl/eyeTrackedGazePoint", {0.2f, 0.0f, -0.9f}), state, stats);
        CHECK(state.gaze_valid);
        FgbFrameSlot slot{};
        slot.struct_size = sizeof(FgbFrameSlot);
        slot.abi_version = FGB_MODULE_ABI_V2;
        slot.shape_count = FGB_MAX_SHAPES;
        Write(state, table, &slot);
        CHECK((slot.attributes & FGB_ATTR_LEFT_GAZE) != 0);
        CHECK((slot.attributes & FGB_ATTR_RIGHT_GAZE) != 0);
        CHECK((slot.attributes & FGB_ATTR_COMBINED_GAZE) != 0);
        CHECK_NEAR(UnitLength(slot.left_eye.gaze_dir), 1.0, 1e-5);
        CHECK(slot.left_eye.gaze_dir[2] < 0.0f);  // -Z 前
        CHECK(slot.left_eye.gaze_dir[0] > 0.0f);  // +X 右
        CHECK_NEAR(slot.left_eye.gaze_dir[0], slot.combined_gaze_dir[0], 1e-6);
        CHECK_NEAR(slot.left_eye.gaze_confidence, 0.8f, 1e-6);
        CHECK_NEAR(slot.combined_gaze_confidence, 0.9f, 1e-6);
        // 上游没有 2D 注视点：点位属性与视口标定位一律不许出现
        CHECK((slot.attributes & FGB_ATTR_LEFT_POINT) == 0);
        CHECK((slot.attributes & FGB_ATTR_RIGHT_POINT) == 0);
        CHECK((slot.attributes & FGB_ATTR_POINT_VIEWPORT) == 0);
    }

    // ---- 注视超过 500ms 未更新：停止输出方向，不沿用旧值 ----
    {
        constexpr std::uint64_t t0 = 1'000'000'000ull;
        FrameState state{};
        Stats stats{};
        steamlink::MergeMessage(FloatMessage("/sl/eyeTrackedGazePoint", {0.2f, 0.0f, -0.9f}),
                                state, stats, t0);
        FgbFrameSlot fresh{};
        fresh.struct_size = sizeof(FgbFrameSlot);
        fresh.abi_version = FGB_MODULE_ABI_V2;
        fresh.shape_count = FGB_MAX_SHAPES;
        steamlink::WriteFrame(state, table, &fresh, t0 + 500ull * 1000ull * 1000ull);
        CHECK((fresh.attributes & FGB_ATTR_COMBINED_GAZE) != 0);  // 恰好 500ms 仍有效

        FgbFrameSlot stale{};
        stale.struct_size = sizeof(FgbFrameSlot);
        stale.abi_version = FGB_MODULE_ABI_V2;
        stale.shape_count = FGB_MAX_SHAPES;
        steamlink::WriteFrame(state, table, &stale, t0 + 501ull * 1000ull * 1000ull);
        CHECK((stale.attributes & (FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE |
                                   FGB_ATTR_COMBINED_GAZE)) == 0);
        // 面捕与睁眼度不受注视过期影响
        CHECK((stale.attributes & FGB_ATTR_EXPRESSION) != 0);
        CHECK((stale.attributes & FGB_ATTR_LEFT_OPENNESS) != 0);
    }

    // ---- 只有「2D 点」形态的数据时，绝不置方向属性（不伪造 (0,0,-1)）----
    {
        FrameState state{};
        Stats stats{};
        // 只有两个浮点参数的注视消息（等价于 2D 点）必须按无效处理
        Merge(FloatMessage("/sl/eyeTrackedGazePoint", {0.5f, 0.5f}), state, stats);
        CHECK(!state.gaze_valid);
        CHECK_EQ(stats.malformed, static_cast<std::size_t>(1));
        FgbFrameSlot slot{};
        slot.struct_size = sizeof(FgbFrameSlot);
        slot.abi_version = FGB_MODULE_ABI_V2;
        slot.shape_count = FGB_MAX_SHAPES;
        Write(state, table, &slot);
        CHECK((slot.attributes & FGB_ATTR_LEFT_GAZE) == 0);
        CHECK((slot.attributes & FGB_ATTR_RIGHT_GAZE) == 0);
        CHECK((slot.attributes & FGB_ATTR_COMBINED_GAZE) == 0);
        // 方向字段保持全零也不能被当成「看正前方」——属性位没置就无效
        CHECK_NEAR(slot.left_eye.gaze_dir[2], 0.0f, 1e-6);

        // 完全没收到注视消息时同样不置方向位
        FrameState no_gaze{};
        Stats ignored{};
        Merge(FloatMessage("/sl/xrfb/facew/JawDrop", {0.5f}), no_gaze, ignored);
        FgbFrameSlot plain{};
        plain.struct_size = sizeof(FgbFrameSlot);
        plain.abi_version = FGB_MODULE_ABI_V2;
        plain.shape_count = FGB_MAX_SHAPES;
        Write(no_gaze, table, &plain);
        CHECK((plain.attributes & (FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE |
                                   FGB_ATTR_COMBINED_GAZE)) == 0);
        CHECK((plain.attributes & FGB_ATTR_LEFT_OPENNESS) != 0);  // 睁眼度仍然给
    }

    // ---- 睁眼度：EyesClosed* + LidTightener*（上游公式 1-clamp(c+c*s)）----
    {
        FrameState state{};
        Stats stats{};
        Merge(FloatMessage("/sl/xrfb/facew/EyesClosedL", {0.3f}), state, stats);
        Merge(FloatMessage("/sl/xrfb/facew/LidTightenerL", {0.5f}), state, stats);
        CHECK_EQ(stats.mapped, static_cast<std::size_t>(2));
        FgbFrameSlot slot{};
        slot.struct_size = sizeof(FgbFrameSlot);
        slot.abi_version = FGB_MODULE_ABI_V2;
        slot.shape_count = FGB_MAX_SHAPES;
        Write(state, table, &slot);
        CHECK_NEAR(slot.left_eye.openness, 1.0f - (0.3f + 0.3f * 0.5f), 1e-6);
        CHECK_NEAR(slot.right_eye.openness, 1.0f, 1e-6);
        CHECK((slot.attributes & FGB_ATTR_LEFT_OPENNESS) != 0);
        CHECK((slot.attributes & FGB_ATTR_RIGHT_OPENNESS) != 0);
        CHECK_NEAR(slot.shapes[host_mock.IndexOf("EyeSquintLeft")], 0.5, 1e-6);
    }

    // ---- NaN / 越界清洗 ----
    {
        FrameState state{};
        Stats stats{};
        Merge(FloatMessage("/sl/xrfb/facew/JawDrop",
                           {std::numeric_limits<float>::quiet_NaN()}),
              state, stats);
        CHECK_NEAR(state.shapes[steamlink::ShapeSlot("JawOpen")], 0.0f, 1e-6);
        Merge(FloatMessage("/sl/xrfb/facew/JawDrop", {5.0f}), state, stats);
        CHECK_NEAR(state.shapes[steamlink::ShapeSlot("JawOpen")], 1.0f, 1e-6);
        Merge(FloatMessage("/sl/xrfb/facew/JawDrop", {-2.0f}), state, stats);
        CHECK_NEAR(state.shapes[steamlink::ShapeSlot("JawOpen")], 0.0f, 1e-6);
        Merge(FloatMessage("/sl/xrfb/facew/EyesClosedR",
                                             {std::numeric_limits<float>::quiet_NaN()}),
                                state, stats);
        CHECK_NEAR(state.eye_closed[1], 0.0f, 1e-6);
        // NaN 注视点必须让注视整体失效，而不是污染方向
        Merge(
            FloatMessage("/sl/eyeTrackedGazePoint",
                         {std::numeric_limits<float>::quiet_NaN(), 0.0f, -1.0f}),
            state, stats);
        CHECK(!state.gaze_valid);
        // 零向量同样不置方向位
        FrameState zero{};
        Stats zero_stats{};
        Merge(FloatMessage("/sl/eyeTrackedGazePoint", {0.0f, 0.0f, 0.0f}), zero,
                                zero_stats);
        CHECK(zero.gaze_valid);  // 三个有限浮点，但方向由 WriteFrame 兜底
        FgbFrameSlot slot{};
        slot.struct_size = sizeof(FgbFrameSlot);
        slot.abi_version = FGB_MODULE_ABI_V2;
        slot.shape_count = FGB_MAX_SHAPES;
        Write(zero, table, &slot);
        CHECK((slot.attributes & (FGB_ATTR_LEFT_GAZE | FGB_ATTR_RIGHT_GAZE |
                                  FGB_ATTR_COMBINED_GAZE)) == 0);
    }

    // ---- 未知地址忽略计数 ----
    {
        FrameState state{};
        Stats stats{};
        Merge(FloatMessage("/avatar/parameters/LeftEyeX", {0.1f}), state, stats);
        Merge(FloatMessage("/avatar/parameters/LeftEyeLid", {0.1f}), state,
                                stats);
        Merge(FloatMessage("/tracking/eye/CenterVecFull", {0.1f}), state, stats);
        Merge(FloatMessage("/sl/xrfb/facec/LowerFace", {0.1f}), state, stats);
        Merge(FloatMessage("/sl/xrfb/facew/EyesLookUpL", {0.1f}), state, stats);
        // Valve 实际发拼错的 Tounge...；正确拼写也接受，两种都不算未知地址
        Merge(FloatMessage("/sl/xrfb/facew/ToungeTipInterdental", {0.5f}), state, stats);
        Merge(FloatMessage("/sl/xrfb/facew/TongueTipInterdental", {0.5f}), state, stats);
        CHECK_EQ(stats.known_ignored, static_cast<std::size_t>(7));
        CHECK_EQ(stats.unknown, static_cast<std::size_t>(0));

        Merge(FloatMessage("/sl/some/new/thing", {0.1f}), state, stats);
        Merge(FloatMessage("/other/address", {0.1f}), state, stats);
        CHECK_EQ(stats.unknown, static_cast<std::size_t>(2));
        CHECK_EQ(stats.first_unknown, std::string{"/sl/some/new/thing"});
        // 未知地址不得写入任何形状
        for (std::size_t i = 0; i < steamlink::kShapeSlots; ++i) {
            CHECK_NEAR(state.shapes[i], 0.0f, 1e-6);
        }
        CHECK_EQ(stats.messages, static_cast<std::size_t>(9));

        // 类型不对的参数按 malformed 计数，不进映射
        OscMessage bad;
        bad.address = "/sl/xrfb/facew/JawDrop";
        bad.arguments.push_back(StringArg("jawOpen"));
        Merge(bad, state, stats);
        CHECK_EQ(stats.malformed, static_cast<std::size_t>(1));
        CHECK_NEAR(state.shapes[steamlink::ShapeSlot("JawOpen")], 0.0f, 1e-6);
    }

    // ---- 断流判定：Staleness 报错一次、恢复后回到活跃 ----
    {
        testing::HostMock mock;
        Host logger{mock.services()};
        Staleness staleness{10ull * 1000ull * 1000ull * 1000ull, "Steam Link"};
        const std::uint64_t t0 = 1000ull * 1000ull * 1000ull;
        CHECK(!staleness.Check(logger, 0, t0));  // 一帧都没收到
        CHECK(mock.HasStatus(FGB_STATUS_ERROR));
        CHECK_EQ(mock.CountStatuses(FGB_STATUS_ERROR), static_cast<std::size_t>(1));  // 只报一次

        CHECK(staleness.Check(logger, t0, t0 + 1ull * 1000ull * 1000ull * 1000ull));
        CHECK(mock.HasStatus(FGB_STATUS_ACTIVE));

        CHECK(!staleness.Check(logger, t0, t0 + 11ull * 1000ull * 1000ull * 1000ull));
        CHECK_EQ(mock.CountStatuses(FGB_STATUS_ERROR), static_cast<std::size_t>(2));
        CHECK(!staleness.Check(logger, t0, t0 + 12ull * 1000ull * 1000ull * 1000ull));
        CHECK_EQ(mock.CountStatuses(FGB_STATUS_ERROR), static_cast<std::size_t>(2));  // 不刷屏

        // 恢复：重新收到包后回到活跃并允许再次报错
        CHECK(staleness.Check(logger, t0 + 12ull * 1000ull * 1000ull * 1000ull,
                              t0 + 12ull * 1000ull * 1000ull * 1000ull));
        CHECK_EQ(mock.CountStatuses(FGB_STATUS_ACTIVE), static_cast<std::size_t>(2));
    }

    return CheckSummary("steamlink-payload");
}
