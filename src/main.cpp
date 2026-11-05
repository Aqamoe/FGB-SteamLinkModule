// Steam Link（Meta Quest 眼动/面捕）→ FaceGaze Bridge 原生模块。
//
// 监听本机 Steam Link 客户端转发的 OSC（UDP 9015），重组被 Valve 拆开的
// bundle，输出 Unified Expressions 表情、睁眼度与眼动追踪的注视方向。
// 不需要 VRCFT，不注入任何进程，也不需要管理员权限。

#include "steamlink_payload.hpp"

#include "fgb/kit/config.hpp"
#include "fgb/kit/module.hpp"

#include <cstdint>
#include <string>

namespace {

constexpr std::uint16_t kDefaultPort = 9015;
constexpr std::uint64_t kNoDataTimeoutNs = 10ull * 1000ull * 1000ull * 1000ull;
constexpr std::uint64_t kWarnIntervalNs = 5ull * 1000ull * 1000ull * 1000ull;

class SteamLinkSource : public fgb::kit::IModuleSource {
public:
    bool Start(fgb::kit::Host& host, const FgbInitParams& params, std::string& error) override {
        fgb::kit::ConfigIni config;
        std::string config_error;
        const std::string directory = params.module_dir != nullptr ? params.module_dir : "";
        bool config_loaded = false;
        // config.ini 是可选项：宿主目录若含 UTF-8 非 ASCII 路径，ConfigIni 内部的
        // 文件系统转换在系统代码页不认时可能抛异常——不能因此砸掉整个模块。
        try {
            config_loaded = config.Load(directory, "config.ini", config_error);
        } catch (const std::exception& e) {
            host.Warn(std::string{"读取 config.ini 异常（路径编码不被系统代码页接受？）："} +
                      e.what());
        }
        if (config_loaded) {
            port_ = static_cast<std::uint16_t>(config.GetInt("port", port_));
            if (!config_error.empty()) {
                host.Warn("config.ini：" + config_error);
            }
            if (!config.UnknownKeys().empty()) {
                host.Warn("config.ini 有未识别的键：" + config.UnknownKeys()[0]);
            }
        }

        fgb::kit::ShapeTable table;
        steamlink::DeclareTargets(table);
        if (!table.Build(host)) {
            error = "宿主没有提供 shape_index，无法映射 Unified Expressions";
            return false;
        }
        if (table.ResolvedCount() < table.Size()) {
            host.Warn("部分 Unified Expressions 形状未被宿主识别，已跳过 " +
                      std::to_string(table.Size() - table.ResolvedCount()) + " 个");
        }
        table_ = std::move(table);

        if (!endpoint_.Bind(port_, error)) {
            return false;
        }
        host.Info("Steam Link 监听 UDP " + std::to_string(port_) +
                  "，等待本机 Steam Link 客户端转发眼动/面捕数据");
        host.StatusStarting("等待 Steam Link 数据");
        return true;
    }

    bool Read(fgb::kit::Host& host, FgbFrameSlot& slot, std::uint64_t now_ns) override {
        bool merged = false;

        fgb::kit::UdpPacket datagram;
        // bundle 会被 Valve 拆到多个数据报里，必须按到达顺序全部合并，
        // 不能像低频来源那样只取最新一包。
        while (endpoint_.Poll(datagram)) {
            const std::string_view text{reinterpret_cast<const char*>(datagram.data.data()),
                                        datagram.data.size()};
            std::string error;
            if (steamlink::MergeDatagram(text, state_, stats_, now_ns, error)) {
                merged = true;
            } else {
                host.WarnThrottled(warn_ns_, now_ns, kWarnIntervalNs, "丢弃一包 OSC：" + error);
            }
        }

        if (stats_.unknown != reported_unknown_ &&
            (unknown_warn_ns_ == 0 || now_ns - unknown_warn_ns_ >= kWarnIntervalNs)) {
            unknown_warn_ns_ = now_ns;
            reported_unknown_ = stats_.unknown;
            host.Warn("Steam Link 累计忽略 " + std::to_string(stats_.unknown) +
                      " 条无法识别的 OSC 消息，首个地址：" + stats_.first_unknown);
        }
        if (stats_.malformed != reported_malformed_ &&
            (malformed_warn_ns_ == 0 || now_ns - malformed_warn_ns_ >= kWarnIntervalNs)) {
            malformed_warn_ns_ = now_ns;
            reported_malformed_ = stats_.malformed;
            host.Warn("Steam Link 累计 " + std::to_string(stats_.malformed) +
                      " 条消息参数缺失或类型不对，相关数据按无效处理");
        }

        // 收包线程的时间戳可能比本帧 now_ns 晚几微秒（同一 steady_clock 的
        // 线程竞态），取较大值，避免 Kit 里 (now - last) 的无符号下溢。
        const std::uint64_t last_packet_ns = endpoint_.LastPacketNs();
        const std::uint64_t sample_ns = last_packet_ns > now_ns ? last_packet_ns : now_ns;
        if (!staleness_.Check(host, last_packet_ns, sample_ns) || !merged) {
            return false;
        }

        steamlink::WriteFrame(state_, table_, &slot, now_ns);
        return true;
    }

    void Stop(fgb::kit::Host& host) override {
        endpoint_.Close();
        host.StatusStopped("Steam Link 已停止");
    }

private:
    fgb::kit::UdpEndpoint endpoint_;
    fgb::kit::ShapeTable table_;
    steamlink::FrameState state_;
    steamlink::Stats stats_;
    fgb::kit::Staleness staleness_{kNoDataTimeoutNs, "Steam Link"};
    std::uint16_t port_ = kDefaultPort;
    std::uint64_t warn_ns_ = 0;
    std::uint64_t unknown_warn_ns_ = 0;
    std::uint64_t malformed_warn_ns_ = 0;
    std::size_t reported_unknown_ = 0;
    std::size_t reported_malformed_ = 0;
};

} // namespace

FGB_KIT_EXPORT(SteamLinkSource, "com.fgbbridge.steamlink", "Steam Link",
               FGB_CAP_EXPRESSION | FGB_CAP_EYES | FGB_CAP_GAZE)
