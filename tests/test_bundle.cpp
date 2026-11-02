// bundle 重组单测：Valve 会把一组 OSC 参数按网络状况拆成多个 bundle、
// 甚至多个 UDP 数据报，模块必须跨包把消息合并进同一份状态。
#include "osc_fixture.hpp"
#include "steamlink_payload.hpp"

#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/testing/host_mock.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace fgb::kit;
using steamlink::FrameState;
using steamlink::Stats;

namespace {

FgbFrameSlot FreshSlot() {
    FgbFrameSlot slot{};
    slot.struct_size = sizeof(FgbFrameSlot);
    slot.abi_version = FGB_MODULE_ABI_V2;
    slot.shape_count = FGB_MAX_SHAPES;
    return slot;
}

} // namespace

int main() {
    testing::HostMock host_mock;
    Host host{host_mock.services()};
    ShapeTable table;
    steamlink::DeclareTargets(table);
    CHECK(table.Build(host));

    // ---- 单消息数据报（非 bundle）也要能合并 ----
    {
        FrameState state{};
        Stats stats{};
        std::string error;
        const std::string packet = oscfx::MessageFloats("/sl/xrfb/facew/JawDrop", {0.5f});
        CHECK(steamlink::MergeDatagram(packet, state, stats, error));
        CHECK_EQ(stats.messages, static_cast<std::size_t>(1));
        CHECK_NEAR(state.shapes[steamlink::ShapeSlot("JawOpen")], 0.5f, 1e-6);
    }

    // ---- 单个 bundle 内多条消息 ----
    {
        FrameState state{};
        Stats stats{};
        std::string error;
        const std::string packet = oscfx::Bundle({
            oscfx::MessageFloats("/sl/xrfb/facew/JawDrop", {0.4f}),
            oscfx::MessageFloats("/sl/xrfb/facew/LipPuckerL", {0.25f}),
            oscfx::MessageFloats("/sl/xrfb/facew/EyesClosedR", {0.6f}),
            oscfx::MessageFloats("/sl/eyeTrackedGazePoint", {0.1f, -0.2f, -0.97f}),
        });
        CHECK(steamlink::MergeDatagram(packet, state, stats, error));
        CHECK_EQ(stats.messages, static_cast<std::size_t>(4));
        CHECK_EQ(stats.mapped, static_cast<std::size_t>(4));
        CHECK_NEAR(state.shapes[steamlink::ShapeSlot("LipPuckerUpperLeft")], 0.25f, 1e-6);
        CHECK_NEAR(state.eye_closed[1], 0.6f, 1e-6);
        CHECK(state.gaze_valid);
    }

    // ---- 跨多包 / 分包的 bundle 重组 ----
    // 模拟 Valve 把一次面捕 + 眼动拆成两个 bundle 数据报外加一条散消息。
    {
        FrameState state{};
        Stats stats{};
        std::string error;
        const std::string first = oscfx::Bundle({
            oscfx::MessageFloats("/sl/xrfb/facew/UpperLidRaiserL", {0.5f}),
            oscfx::MessageFloats("/sl/xrfb/facew/JawDrop", {0.3f}),
            oscfx::MessageFloats("/sl/xrfb/facec/LowerFace", {1.0f}),  // 已知但不使用
        });
        const std::string second = oscfx::Bundle({
            oscfx::MessageFloats("/sl/eyeTrackedGazePoint", {0.0f, 0.0f, -1.0f}),
            oscfx::MessageFloats("/sl/xrfb/facew/EyesClosedL", {0.7f}),
        });
        const std::string third = oscfx::MessageFloats("/sl/xrfb/facew/MouthLeft", {0.2f});

        CHECK(steamlink::MergeDatagram(first, state, stats, error));
        CHECK(steamlink::MergeDatagram(second, state, stats, error));
        CHECK(steamlink::MergeDatagram(third, state, stats, error));

        CHECK_EQ(stats.messages, static_cast<std::size_t>(6));
        CHECK_EQ(stats.known_ignored, static_cast<std::size_t>(1));
        CHECK_EQ(stats.unknown, static_cast<std::size_t>(0));

        // 三个数据报的数据必须汇在同一帧里
        FgbFrameSlot slot = FreshSlot();
        steamlink::WriteFrame(state, table, &slot);
        CHECK((slot.attributes & FGB_ATTR_EXPRESSION) != 0);
        CHECK_NEAR(slot.shapes[host_mock.IndexOf("EyeWideLeft")], 0.5, 1e-6);
        CHECK_NEAR(slot.shapes[host_mock.IndexOf("JawOpen")], 0.3, 1e-6);
        CHECK_NEAR(slot.shapes[host_mock.IndexOf("MouthLowerLeft")], 0.2, 1e-6);
        CHECK_NEAR(slot.shapes[host_mock.IndexOf("MouthUpperLeft")], 0.2, 1e-6);
        CHECK_NEAR(slot.left_eye.openness, 1.0f - 0.7f, 1e-6);
        CHECK((slot.attributes & FGB_ATTR_COMBINED_GAZE) != 0);
        CHECK_NEAR(slot.combined_gaze_dir[2], -1.0, 1e-6);
    }

    // ---- 同一地址跨包覆盖：后到的值赢（与上游逐地址刷新一致）----
    {
        FrameState state{};
        Stats stats{};
        std::string error;
        CHECK(steamlink::MergeDatagram(oscfx::Bundle({oscfx::MessageFloats(
                                            "/sl/xrfb/facew/JawDrop", {0.4f})}),
                                       state, stats, error));
        CHECK(steamlink::MergeDatagram(oscfx::Bundle({oscfx::MessageFloats(
                                            "/sl/xrfb/facew/JawDrop", {0.8f})}),
                                       state, stats, error));
        CHECK_NEAR(state.shapes[steamlink::ShapeSlot("JawOpen")], 0.8f, 1e-6);
    }

    // ---- 非法 / 被截断的数据报：整包丢弃且不动状态 ----
    {
        FrameState state{};
        Stats stats{};
        std::string error;
        const std::string good =
            oscfx::Bundle({oscfx::MessageFloats("/sl/xrfb/facew/JawDrop", {0.5f})});

        std::string truncated = good.substr(0, good.size() - 3);  // 砍掉尾部参数
        CHECK(!steamlink::MergeDatagram(truncated, state, stats, error));
        CHECK(!error.empty());

        std::string short_header = good.substr(0, 12);  // bundle 头都不完整
        CHECK(!steamlink::MergeDatagram(short_header, state, stats, error));

        const std::string garbage = std::string("this is not osc!!");
        CHECK(!steamlink::MergeDatagram(garbage, state, stats, error));

        CHECK(!steamlink::MergeDatagram("", state, stats, error));

        // 丢弃后状态保持原样
        CHECK_NEAR(state.shapes[steamlink::ShapeSlot("JawOpen")], 0.0f, 1e-6);
        CHECK_EQ(stats.mapped, static_cast<std::size_t>(0));
    }

    // ---- int 类型参数也要能进映射（AsFloat 统一处理）----
    {
        FrameState state{};
        Stats stats{};
        std::string error;
        std::string payload;
        oscfx::PutInt(payload, 1);  // 'i' 类型：1 → 1.0f
        const std::string packet =
            oscfx::MessageRaw("/sl/xrfb/facew/JawDrop", "i", payload);
        CHECK(steamlink::MergeDatagram(packet, state, stats, error));
        CHECK_NEAR(state.shapes[steamlink::ShapeSlot("JawOpen")], 1.0f, 1e-6);
    }

    return CheckSummary("steamlink-bundle");
}
