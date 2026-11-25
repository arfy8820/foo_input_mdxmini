#include <foobar2000/SDK/foobar2000-lite.h>
#include <foobar2000/SDK/cfg_var.h>
#include <foobar2000/SDK/playback_control.h>
#include "host.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace mdxmini_component {
namespace {
cfg_string config({0xbe030e2b,0xa15f,0x4793,{0xb9,0xb4,0x4a,0x6f,0x91,0xb9,0xb2,0x20}}, "");
std::mutex registry_mutex;
std::vector<std::weak_ptr<Session>> sessions;
}

Settings host_settings() {
    const auto stored = config.get();
    std::istringstream input(stored.c_str());
    Settings settings;
    if (!(input >> settings.rate >> settings.loops >> settings.fade >> settings.fm_gain >>
          settings.pcm_gain >> settings.mono >> std::quoted(settings.pdx_directory))) return {};
    if (std::find(rates.begin(), rates.end(), settings.rate) == rates.end() ||
        settings.loops < 1 || settings.loops > 20 || settings.fade < 1 || settings.fade > 20 ||
        settings.fm_gain < 0 || settings.fm_gain > 100 || settings.pcm_gain < 0 || settings.pcm_gain > 100) return {};
    return settings;
}

void save_settings(const Settings& settings) {
    std::ostringstream output;
    output << settings.rate << ' ' << settings.loops << ' ' << settings.fade << ' '
           << settings.fm_gain << ' ' << settings.pcm_gain << ' ' << settings.mono << ' '
           << std::quoted(settings.pdx_directory);
    config = output.str().c_str();
}

void register_session(const std::shared_ptr<Session>& session) {
    std::lock_guard lock(registry_mutex);
    std::erase_if(sessions, [](const auto& weak) { return weak.expired(); });
    sessions.emplace_back(session);
}

std::shared_ptr<Session> active_session() {
    metadb_handle_ptr track;
    if (!playback_control::get()->get_now_playing(track)) return {};
    std::lock_guard lock(registry_mutex);
    for (auto it = sessions.rbegin(); it != sessions.rend(); ++it) {
        if (auto session = it->lock(); session && session->path == track->get_path()) return session;
    }
    return {};
}

}
