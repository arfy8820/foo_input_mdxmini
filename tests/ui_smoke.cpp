#include <foobar2000/SDK/foobar2000.h>
#include <foobar2000/SDK/component.h>
#include <oleacc.h>
#include <iostream>
#include <stdexcept>
#include "../src/host.h"

namespace {
mdxmini_component::Settings stored;
std::shared_ptr<mdxmini_component::Session> playing;
}
namespace mdxmini_component {
Settings host_settings() { return stored; }
void save_settings(const Settings& settings) { stored = settings; }
std::shared_ptr<Session> active_session() { return playing; }
}
#include "../src/ui.cpp"
extern "C" foobar2000_client* __cdecl foobar2000_get_interface(foobar2000_api*, HINSTANCE);

namespace {
using namespace mdxmini_component;
void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
std::wstring accessible_name(HWND window, long child = CHILDID_SELF) {
    IAccessible* accessible = nullptr;
    require(SUCCEEDED(AccessibleObjectFromWindow(window, OBJID_CLIENT, IID_IAccessible,
        reinterpret_cast<void**>(&accessible))), "No native accessibility object");
    VARIANT id{};
    id.vt = VT_I4;
    id.lVal = child;
    BSTR name = nullptr;
    const auto result = accessible->get_accName(id, &name);
    const std::wstring value = name ? name : L"";
    SysFreeString(name);
    accessible->Release();
    require(result == S_OK, "Control or item has no accessible name");
    return value;
}

class Callback : public preferences_page_callback {
public:
    void on_state_changed() override { ++calls; }
    int calls = 0;
};

void preferences(HWND parent) {
    auto callback = fb2k::service_new<Callback>();
    service_impl_single_t<PreferencesPage> factory;
    auto page = factory.instantiate(parent, callback);
    const auto window = page->get_wnd();
    const auto rate = GetDlgItem(window, Rate);
    require(accessible_name(rate) == L"Sample rate (Hz)", "Sample rate selector name");
    require(accessible_name(GetDlgItem(window, FM)) == L"YM2151 FM gain", "FM gain name");
    require(accessible_name(GetDlgItem(window, PCM)) == L"PDX sample gain", "PDX gain name");
    require(callback->calls == 0, "Preference initialization signaled a user edit");
    SendMessageW(rate, WM_KEYDOWN, VK_DOWN, 0);
    require((page->get_state() & preferences_state::changed) != 0, "Sample rate change not staged");
    page->apply();
    require(stored.rate == 48000, "Sample rate not saved");
    SetWindowTextW(GetDlgItem(window, PdxFolder), L"relative");
    require((page->get_state() & preferences_state::changed) == 0, "Invalid folder can be applied");
    page->reset();
    page->apply();
    require(stored == Settings{}, "Defaults not restored");
    DestroyWindow(window);
    page.release();
}

void mixer(HWND parent) {
    Mixer mixer;
    mixer.initialize(parent, true);
    playing = std::make_shared<Session>("smoke.mdx", 16, "MDX test", true, true);
    const auto window = mixer.hwnd;
    SendMessageW(window, WM_TIMER, 1, 0);
    const auto channel = GetDlgItem(window, Channel);
    const auto mute = GetDlgItem(window, Mute);
    require(accessible_name(channel) == L"Sound channel", "Channel selector name");
    require(GetNextDlgTabItem(window, channel, FALSE) == mute, "Mute is outside the expected Tab order");
    require(SendMessageW(channel, CB_GETCOUNT, 0, 0) == 16, "PCM8 channel count");
    COMBOBOXINFO combo{sizeof(combo)};
    require(GetComboBoxInfo(channel, &combo) != FALSE, "No dropdown accessibility object");
    for (int i = 0; i < 16; ++i) {
        const auto expected = i < 8 ? L"YM2151 FM " + std::to_wstring(i + 1) : L"PDX sample " + std::to_wstring(i - 7);
        require(accessible_name(combo.hwndList, i + 1) == expected, "Missing channel name");
        if (i) SendMessageW(channel, WM_KEYDOWN, VK_DOWN, 0);
        require(selection(channel) == i, "Keyboard cannot reach channel");
    }
    SendMessageW(mute, BM_CLICK, 0, 0);
    require(playing->render_mask() == 0x8000, "Mute did not target PDX channel 8");
    const auto solo = GetDlgItem(window, Solo);
    SendMessageW(solo, BM_CLICK, 0, 0);
    require(playing->render_mask() == 0x7fff, "Solo did not isolate PDX channel 8");
    SendMessageW(solo, BM_CLICK, 0, 0);
    require(playing->render_mask() == 0x8000, "Clearing solo lost the mute state");
    SendMessageW(GetDlgItem(window, Unmute), BM_CLICK, 0, 0);
    require(playing->render_mask() == 0, "Unmute all");
    playing.reset();
    SendMessageW(window, WM_TIMER, 1, 0);
    require(!IsWindowEnabled(channel), "Mixer remained active after playback stopped");
}
}

int main() {
    try {
        foobar2000_get_interface(nullptr, GetModuleHandleW(nullptr));
        const auto parent = CreateWindowExW(0, L"STATIC", L"MDX tests", WS_OVERLAPPED,
            0, 0, 800, 700, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(parent != nullptr, "Could not create test window");
        preferences(parent);
        mixer(parent);
        DestroyWindow(parent);
        std::cout << "PASS: preferences Apply/Reset, native accessibility, all 16 channels, mute and solo\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
