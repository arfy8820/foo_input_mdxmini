#pragma once
#include "engine.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string_view>
#include <windows.h>

namespace mdxmini_component {
inline constexpr GUID preferences_guid{0x6297260d,0x2a1e,0x41c4,{0xa8,0x17,0x78,0xe3,0x56,0x9f,0x2c,0x20}};

struct Session {
    Session(std::string path, int tracks, std::string title, bool has_pdx, bool needs_pdx)
        : path(std::move(path)), tracks(tracks), title(std::move(title)), has_pdx(has_pdx), needs_pdx(needs_pdx) {
        notes.fill(-1);
    }
    const std::string path;
    const int tracks;
    const std::string title;
    const bool has_pdx;
    const bool needs_pdx;
    std::atomic<uint16_t> muted{0};
    std::atomic<int> solo{-1};
    std::mutex note_mutex;
    std::array<int, maximum_channels> notes;
    uint16_t render_mask() const {
        const int selected = solo.load();
        return selected < 0 ? muted.load() : static_cast<uint16_t>(~(1u << selected));
    }
};

Settings host_settings();
void save_settings(const Settings& settings);
void register_session(const std::shared_ptr<Session>& session);
std::shared_ptr<Session> active_session();
inline std::wstring to_wide(std::string_view value, unsigned codepage = CP_UTF8) {
    if (value.empty()) return {};
    const auto size = static_cast<int>(value.size());
    const int length = MultiByteToWideChar(codepage, 0, value.data(), size, nullptr, 0);
    std::wstring result(length, L'\0');
    if (length) MultiByteToWideChar(codepage, 0, value.data(), size, result.data(), length);
    return result;
}
inline std::string to_utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const auto size = static_cast<int>(value.size());
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), size, nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    if (length) WideCharToMultiByte(CP_UTF8, 0, value.data(), size, result.data(), length, nullptr, nullptr);
    return result;
}
}
