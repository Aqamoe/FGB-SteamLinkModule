// 走真实 ABI 的端到端冒烟：加载 DLL → query → initialize → 喂合成 OSC → update → 检查提交帧。
//
// 它会绑定 9015 端口（Steam Link 本机的 OSC 输出口），运行前关掉 VRCFT 的
// SteamLink 模块和其它占用 9015 的接收端。
// 因此不挂进 ctest，只由 `cmake --build ... --target smoke-test` 触发。

#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/testing/host_mock.hpp"
#include "fgb_module_api.h"
#include "osc_fixture.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint16_t kPort = 9015;

// 模拟 Valve 的拆包行为：面捕一组 bundle、眼动一组 bundle，外加散消息。
std::vector<std::string> BuildStream() {
    std::vector<std::string> datagrams;
    datagrams.push_back(oscfx::Bundle({
        oscfx::MessageFloats("/sl/xrfb/facew/JawDrop", {0.62f}),
        oscfx::MessageFloats("/sl/xrfb/facew/UpperLidRaiserL", {0.4f}),
        oscfx::MessageFloats("/sl/xrfb/facec/LowerFace", {1.0f}),
    }));
    datagrams.push_back(oscfx::Bundle({
        oscfx::MessageFloats("/sl/xrfb/facew/EyesClosedL", {0.3f}),
        oscfx::MessageFloats("/sl/eyeTrackedGazePoint", {0.2f, 0.1f, -0.95f}),
    }));
    datagrams.push_back(
        oscfx::MessageFloats("/sl/xrfb/facew/LipCornerPullerL", {0.35f}));
    return datagrams;
}

bool SendToSelf(const std::string& bytes) {
    WSADATA data{};
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return false;
    }
    const SOCKET handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(kPort);
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int sent = ::sendto(handle, bytes.data(), static_cast<int>(bytes.size()), 0,
                              reinterpret_cast<sockaddr*>(&target), sizeof(target));
    ::closesocket(handle);
    ::WSACleanup();
    return sent != SOCKET_ERROR;
}

} // namespace

int main(int argc, char** argv) {
    // 崩溃排查用：不缓冲 stdout，冒烟各阶段进度在 CI 日志里也更有用。
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        std::printf("用法：%s <steamlink.dll 路径>\n", argv[0]);
        return 2;
    }

    std::printf("阶段：LoadLibrary %s\n", argv[1]);
    HMODULE library = ::LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    CHECK(library != nullptr);
    if (library == nullptr) {
        std::printf("LoadLibrary 失败，错误码 %lu（DLL 是否已构建？依赖是否在同目录？）\n",
                    ::GetLastError());
        return CheckSummary("steamlink-abi-smoke");
    }

    std::printf("阶段：query\n");
    const auto query = reinterpret_cast<decltype(&fgb_module_query_v2)>(
        ::GetProcAddress(library, "fgb_module_query_v2"));
    CHECK(query != nullptr);

    fgb::kit::testing::HostMock host;
    // HostMock::MakeParams 的 module_dir 指针指向传入的字符串；必须让它活到
    // 本函数结束，否则 params.module_dir 悬垂（Kit 测试桩的已知陷阱）。
    const std::string module_dir = ".";
    FgbInitParams params = host.MakeParams(module_dir);

    const FgbModule* module = query(FGB_MODULE_ABI_V2);
    CHECK(module != nullptr);
    if (module == nullptr) {
        ::FreeLibrary(library);
        return CheckSummary("steamlink-abi-smoke");
    }
    CHECK(module->id == std::string{"com.fgbbridge.steamlink"});
    CHECK_EQ(module->abi_version, FGB_MODULE_ABI_V2);
    CHECK_EQ(module->capabilities,
             static_cast<std::uint32_t>(FGB_CAP_EXPRESSION | FGB_CAP_EYES | FGB_CAP_GAZE));
    CHECK(query(FGB_MODULE_ABI_V2 + 1) == nullptr);  // ABI 不匹配必须拒绝

    std::printf("阶段：initialize（绑定 UDP 9015）\n");
    void* self = nullptr;
    CHECK_EQ(module->initialize(&self, host.services(), &params), 1);
    if (self == nullptr) {
        std::printf("initialize 失败：%s\n", host.LastStatusDetail().c_str());
        ::FreeLibrary(library);
        return CheckSummary("steamlink-abi-smoke");
    }

    std::printf("阶段：空转 update（不应提交帧）\n");
    // 没有数据时不应该提交帧
    module->update(self);
    CHECK(host.committed().empty());

    std::printf("阶段：发送 3 个分包数据报\n");
    const std::vector<std::string> stream = BuildStream();
    for (const auto& datagram : stream) {
        CHECK(SendToSelf(datagram));
    }
    // 分包可能落在不同 tick 里，多轮 update 直到最后一帧把三个数据报汇齐
    bool reassembled = false;
    for (int attempt = 0; attempt < 200 && !reassembled; ++attempt) {
        module->update(self);
        if (!host.committed().empty()) {
            const FgbFrameSlot& latest = host.committed().back();
            reassembled = (latest.attributes & FGB_ATTR_COMBINED_GAZE) != 0 &&
                          latest.shape_valid[host.IndexOf("MouthCornerPullLeft")] != 0;
        }
        if (!reassembled) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    CHECK(!host.committed().empty());

    if (!host.committed().empty()) {
        const FgbFrameSlot& slot = host.committed().back();
        CHECK((slot.attributes & FGB_ATTR_EXPRESSION) != 0);
        CHECK((slot.attributes & FGB_ATTR_LEFT_OPENNESS) != 0);
        CHECK((slot.attributes & FGB_ATTR_COMBINED_GAZE) != 0);
        // 跨包重组：三个数据报的数据应在同一帧里汇齐（最后一帧）
        CHECK_NEAR(slot.shapes[host.IndexOf("JawOpen")], 0.62, 1e-5);
        CHECK_NEAR(slot.shapes[host.IndexOf("EyeWideLeft")], 0.4, 1e-5);
        CHECK_NEAR(slot.shapes[host.IndexOf("MouthCornerPullLeft")], 0.35, 1e-5);
        CHECK(slot.left_eye.openness < 1.0f);
        CHECK_NEAR(slot.left_eye.gaze_dir[2], -0.95f /
                                                 std::sqrt(0.2f * 0.2f + 0.1f * 0.1f +
                                                           0.95f * 0.95f),
                    1e-4);
        CHECK((slot.attributes & (FGB_ATTR_LEFT_POINT | FGB_ATTR_RIGHT_POINT |
                                  FGB_ATTR_POINT_VIEWPORT)) == 0);
        CHECK(host.HasStatus(FGB_STATUS_STARTING) || host.HasStatus(FGB_STATUS_ACTIVE));
    }

    std::printf("阶段：teardown\n");
    module->teardown(self);
    std::printf("阶段：FreeLibrary\n");
    ::FreeLibrary(library);
    return CheckSummary("steamlink-abi-smoke");
}
