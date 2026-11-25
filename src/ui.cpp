#include <foobar2000/SDK/foobar2000-lite.h>
#include <foobar2000/SDK/coreDarkMode.h>
#include <foobar2000/SDK/initquit.h>
#include <foobar2000/SDK/menu.h>
#include <foobar2000/SDK/modeless_dialog.h>
#include <foobar2000/SDK/preferences_page.h>
#include <foobar2000/SDK/ui.h>
#include <commctrl.h>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include "host.h"

namespace mdxmini_component {
namespace {
class Window {
public:
    virtual ~Window() { if (hwnd) DestroyWindow(hwnd); }
    HWND hwnd = nullptr;
protected:
    void create(HWND parent, bool embedded, const wchar_t* title) {
        const auto module = core_api::get_my_instance();
        const wchar_t* class_name = L"MDXMini.Component.6297260D";
        WNDCLASSW wc{};
        wc.lpfnWndProc = procedure;
        wc.hInstance = module;
        wc.lpszClassName = class_name;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        RegisterClassW(&wc);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
        InitCommonControlsEx(&controls);
        dpi_ = window_dpi(parent);
        const DWORD style = embedded ? WS_CHILD | WS_VISIBLE : WS_OVERLAPPEDWINDOW;
        if (!CreateWindowExW(WS_EX_CONTROLPARENT, class_name, title, style | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, pixels(500), pixels(330), parent, nullptr, module, this))
            throw std::runtime_error("Could not create the MDX window");
    }

    HWND control(int id, const wchar_t* cls, const wchar_t* text, DWORD style = 0, DWORD ex = 0) {
        const auto child = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style,
            0, 0, 1, 1, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            core_api::get_my_instance(), nullptr);
        if (!child) throw std::runtime_error("Could not create an MDX control");
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
        return child;
    }

    HWND item(int id) const { return GetDlgItem(hwnd, id); }
    int pixels(int value) const { return MulDiv(value, dpi_, 96); }
    void move(int id, int x, int y, int width, int height) {
        MoveWindow(item(id), pixels(x), pixels(y), std::max(1, pixels(width)), std::max(1, pixels(height)), TRUE);
    }
    int width() const {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        return MulDiv(rect.right, 96, dpi_);
    }
    fb2k::CCoreDarkModeHooks dark;
    virtual LRESULT dispatch(UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_DPICHANGED) {
            dpi_ = HIWORD(wp);
            if (!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD)) {
                const auto rect = reinterpret_cast<const RECT*>(lp);
                SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
                             rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            SendMessageW(hwnd, WM_SIZE, 0, 0);
            return 0;
        }
        if (msg == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
private:
    unsigned dpi_ = 96;
    static unsigned window_dpi(HWND hwnd) {
        const auto get_dpi = reinterpret_cast<UINT (WINAPI*)(HWND)>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        if (get_dpi) if (const unsigned value = get_dpi(hwnd)) return value;
        return 96;
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->hwnd = hwnd;
            self->dpi_ = window_dpi(hwnd);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        try {
            if (msg == WM_NCDESTROY) {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                self->hwnd = nullptr;
                return DefWindowProcW(hwnd, msg, wp, lp);
            }
            return self->dispatch(msg, wp, lp);
        } catch (const std::exception& e) {
            MessageBoxW(hwnd, to_wide(e.what()).c_str(), L"MDX", MB_OK | MB_ICONERROR);
            return msg == WM_CREATE ? -1 : 0;
        }
    }
};

constexpr DWORD dropdown = WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL;
void add_choice(HWND control, const std::wstring& text) {
    if (SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())) < 0)
        throw std::runtime_error("Could not add an MDX selector item");
}
void select(HWND control, int index) { SendMessageW(control, CB_SETCURSEL, index, 0); }
int selection(HWND control) { return static_cast<int>(SendMessageW(control, CB_GETCURSEL, 0, 0)); }
std::wstring window_text(HWND hwnd) {
    std::wstring text(GetWindowTextLengthW(hwnd) + 1, L'\0');
    text.resize(GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size())));
    return text;
}

enum { Rate=100, Loops, Fade, FM, PCM, Mono, PdxFolder, PrefHelp };
class Preferences : public preferences_page_instance, public Window {
public:
    explicit Preferences(preferences_page_callback::ptr callback)
        : callback_(callback), saved_(host_settings()), draft_(saved_) {}
    void initialize(HWND parent) { create(parent, true, L"MDX (mdxmini) preferences"); }
    HWND get_wnd() override { return hwnd; }
    t_uint32 get_state() override {
        t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (valid_ && draft_ != saved_) state |= preferences_state::changed | preferences_state::needs_restart_playback;
        return state;
    }
    void apply() override {
        if (!valid_) return;
        save_settings(draft_);
        saved_ = draft_;
        callback_->on_state_changed();
    }
    void reset() override {
        draft_ = {};
        show_settings();
        callback_->on_state_changed();
    }
private:
    preferences_page_callback::ptr callback_;
    Settings saved_, draft_;
    bool ready_ = false, updating_ = false, valid_ = true;

    void label(int id, const wchar_t* text) { control(id + 50, L"STATIC", text); }
    void slider(int id, const wchar_t* title) {
        label(id, title);
        const auto control_handle = control(id, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_HORZ | TBS_TOOLTIPS);
        SendMessageW(control_handle, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    }
    void show_settings() {
        updating_ = true;
        select(item(Rate), static_cast<int>(std::find(rates.begin(), rates.end(), draft_.rate) - rates.begin()));
        select(item(Loops), draft_.loops - 1);
        select(item(Fade), draft_.fade - 1);
        SendMessageW(item(FM), TBM_SETPOS, TRUE, draft_.fm_gain);
        SendMessageW(item(PCM), TBM_SETPOS, TRUE, draft_.pcm_gain);
        SendMessageW(item(Mono), BM_SETCHECK, draft_.mono ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextW(item(PdxFolder), to_wide(draft_.pdx_directory).c_str());
        updating_ = false;
        update_help();
    }
    void update_help() {
        valid_ = draft_.pdx_directory.empty() || std::filesystem::path(to_wide(draft_.pdx_directory)).is_absolute();
        const auto text = valid_ ? L"Apply to save. Settings take effect when playback is reopened.\r\n"
                                   L"MDX banks are searched beside the track first, then in the optional folder."
                                 : L"Enter an absolute PDX folder path or leave the field blank.";
        SetWindowTextW(item(PrefHelp), text);
    }
    void changed() {
        const int rate = selection(item(Rate));
        if (rate < 0) return;
        draft_.rate = rates[rate];
        draft_.loops = selection(item(Loops)) + 1;
        draft_.fade = selection(item(Fade)) + 1;
        draft_.fm_gain = static_cast<int>(SendMessageW(item(FM), TBM_GETPOS, 0, 0));
        draft_.pcm_gain = static_cast<int>(SendMessageW(item(PCM), TBM_GETPOS, 0, 0));
        draft_.mono = SendMessageW(item(Mono), BM_GETCHECK, 0, 0) == BST_CHECKED;
        draft_.pdx_directory = to_utf8(window_text(item(PdxFolder)));
        update_help();
        preferences_page_instance::ptr keep(this);
        callback_->on_state_changed();
    }
    void layout() {
        const auto w = width();
        int y = 10;
        for (int id : {Rate, Loops, Fade, FM, PCM}) {
            move(id + 50, 10, y + 3, 162, 20);
            move(id, 178, y, w - 188, id == FM || id == PCM ? 26 : 225);
            y += 33;
        }
        move(Mono, 10, y, w - 20, 24);
        move(PdxFolder + 50, 10, y + 36, w - 20, 20);
        move(PdxFolder, 10, y + 58, w - 20, 24);
        move(PrefHelp, 10, y + 93, w - 20, 53);
    }
    LRESULT dispatch(UINT msg, WPARAM wp, LPARAM lp) override {
        if (msg == WM_CREATE) {
            label(Rate, L"Sample rate (Hz)");
            const auto rate = control(Rate, WC_COMBOBOXW, L"", dropdown);
            for (const auto value : rates) add_choice(rate, std::to_wstring(value));
            label(Loops, L"Loop count");
            const auto loops = control(Loops, WC_COMBOBOXW, L"", dropdown);
            label(Fade, L"Loop fade speed (ticks per step)");
            const auto fade = control(Fade, WC_COMBOBOXW, L"", dropdown);
            for (int i = 1; i <= 20; ++i) {
                add_choice(loops, std::to_wstring(i));
                add_choice(fade, std::to_wstring(i));
            }
            slider(FM, L"YM2151 FM gain");
            slider(PCM, L"PDX sample gain");
            control(Mono, L"BUTTON", L"Mono output", WS_TABSTOP | BS_AUTOCHECKBOX);
            label(PdxFolder, L"Optional PDX sample folder");
            const auto folder = control(PdxFolder, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
            SendMessageW(folder, EM_SETLIMITTEXT, 1024, 0);
            control(PrefHelp, L"STATIC", L"");
            show_settings();
            dark.AddDialogWithControls(hwnd);
            ready_ = true;
            layout();
            return 0;
        }
        if (msg == WM_SIZE && ready_) { layout(); return 0; }
        if (ready_ && !updating_) {
            if (msg == WM_HSCROLL && (reinterpret_cast<HWND>(lp) == item(FM) || reinterpret_cast<HWND>(lp) == item(PCM))) {
                changed();
                return 0;
            }
            if (msg == WM_COMMAND) {
                const int id = LOWORD(wp), notification = HIWORD(wp);
                if (((id == Rate || id == Loops || id == Fade) && notification == CBN_SELCHANGE) ||
                    (id == Mono && notification == BN_CLICKED) || (id == PdxFolder && notification == EN_CHANGE))
                    changed();
                return 0;
            }
        }
        return Window::dispatch(msg, wp, lp);
    }
};

class PreferencesPage : public preferences_page_v3 {
public:
    const char* get_name() override { return "MDX (mdxmini)"; }
    GUID get_guid() override { return preferences_guid; }
    GUID get_parent_guid() override { return guid_input; }
    preferences_page_instance::ptr instantiate(HWND parent, preferences_page_callback::ptr callback) override {
        auto instance = fb2k::service_new<Preferences>(callback);
        instance->initialize(parent);
        return instance;
    }
};
preferences_page_factory_t<PreferencesPage> preferences_factory;

enum { Status=200, ChannelLabel, Channel, Mute, Solo, Unmute, ActivityLabel, Activity, OpenPreferences };
class Mixer : public Window {
public:
    ~Mixer() override { if (hwnd) DestroyWindow(hwnd); }
    void initialize(HWND parent, bool embedded) {
        create(parent, embedded, L"MDX channel mixer");
        if (!embedded) {
            modeless_dialog_manager::g_add(hwnd);
            registered_ = true;
            ShowWindow(hwnd, SW_SHOWNORMAL);
        }
    }
private:
    bool ready_ = false, registered_ = false;
    std::weak_ptr<Session> session_;
    std::wstring activity_text_;

    void layout() {
        const int w = width();
        move(Status, 10, 10, w - 20, 40);
        move(ChannelLabel, 10, 55, w - 20, 19);
        move(Channel, 10, 77, w - 20, 270);
        move(Mute, 10, 108, w - 20, 24);
        move(Solo, 10, 139, 128, 26);
        move(Unmute, 144, 139, 100, 26);
        move(ActivityLabel, 10, 178, w - 20, 19);
        move(Activity, 10, 200, w - 20, 24);
        move(OpenPreferences, 10, 233, 134, 25);
    }
    void refresh() {
        const auto current = active_session();
        const auto previous = session_.lock();
        if (current != previous || (current == nullptr && SendMessageW(item(Channel), CB_GETCOUNT, 0, 0) > 0)) {
            session_ = current;
            SendMessageW(item(Channel), CB_RESETCONTENT, 0, 0);
            if (current) {
                for (int i = 0; i < current->tracks; ++i) {
                    const auto title = i < 8 ? L"YM2151 FM " + std::to_wstring(i + 1)
                                             : L"PDX sample " + std::to_wstring(i - 7);
                    add_choice(item(Channel), title);
                }
                select(item(Channel), 0);
                auto title = current->title.empty() ? L"Playing MDX" : to_wide(current->title);
                if (current->needs_pdx) title += current->has_pdx ? L"\r\nPDX sample bank loaded." : L"\r\nPDX bank missing. Sample tracks are silent.";
                SetWindowTextW(item(Status), title.c_str());
            } else {
                SetWindowTextW(item(Status), L"Play an MDX track to use the mixer.");
            }
        }
        for (const int id : {Channel, Mute, Solo, Unmute}) {
            const bool enabled = current != nullptr;
            if ((IsWindowEnabled(item(id)) != FALSE) != enabled) EnableWindow(item(id), enabled);
        }
        const int index = selection(item(Channel));
        std::wstring activity;
        if (current && index >= 0 && index < current->tracks) {
            const int solo = current->solo.load();
            const int checked = (current->muted.load() & (1u << index)) ? BST_CHECKED : BST_UNCHECKED;
            if (SendMessageW(item(Mute), BM_GETCHECK, 0, 0) != checked)
                SendMessageW(item(Mute), BM_SETCHECK, checked, 0);
            const auto solo_label = solo == index ? L"Clear &solo" : L"&Solo selected";
            if (window_text(item(Solo)) != solo_label) SetWindowTextW(item(Solo), solo_label);
            int note;
            {
                std::lock_guard lock(current->note_mutex);
                note = current->notes[index];
            }
            if (index >= 8 && !current->has_pdx) activity = L"No PDX samples loaded";
            else if (note < 0) activity = L"Idle";
            else if (index >= 8) activity = L"Sample index " + std::to_wstring(note);
            else {
                constexpr const wchar_t* names[]{L"C",L"C sharp",L"D",L"D sharp",L"E",L"F",L"F sharp",L"G",L"G sharp",L"A",L"A sharp",L"B"};
                activity = std::wstring(names[note % 12]) + L", MDX note " + std::to_wstring(note);
            }
            if (solo >= 0) activity += solo == index ? L" | Soloed" : L" | Another channel is soloed";
        } else {
            if (SendMessageW(item(Mute), BM_GETCHECK, 0, 0) != BST_UNCHECKED)
                SendMessageW(item(Mute), BM_SETCHECK, BST_UNCHECKED, 0);
            if (window_text(item(Solo)) != L"&Solo selected") SetWindowTextW(item(Solo), L"&Solo selected");
            activity = L"No active channel";
        }
        if (GetFocus() != item(Activity) && activity != activity_text_) {
            SetWindowTextW(item(Activity), activity.c_str());
            activity_text_ = std::move(activity);
        }
    }
    void command(int id) {
        if (id == OpenPreferences) { ui_control::get()->show_preferences(preferences_guid); return; }
        const auto current = session_.lock();
        const int index = selection(item(Channel));
        if (!current || index < 0 || index >= current->tracks) return;
        if (id == Mute) {
            const auto bit = static_cast<uint16_t>(1u << index);
            if (SendMessageW(item(Mute), BM_GETCHECK, 0, 0) == BST_CHECKED) current->muted.fetch_or(bit);
            else current->muted.fetch_and(static_cast<uint16_t>(~bit));
        }
        if (id == Solo) current->solo = current->solo == index ? -1 : index;
        if (id == Unmute) { current->muted = 0; current->solo = -1; }
        refresh();
    }
    LRESULT dispatch(UINT msg, WPARAM wp, LPARAM lp) override {
        if (msg == WM_CREATE) {
            control(Status, L"STATIC", L"Play an MDX track to use the mixer.", SS_NOPREFIX);
            control(ChannelLabel, L"STATIC", L"Sound channel");
            control(Channel, WC_COMBOBOXW, L"", dropdown | WS_DISABLED);
            control(Mute, L"BUTTON", L"&Mute selected channel", WS_TABSTOP | BS_AUTOCHECKBOX | WS_DISABLED);
            control(Solo, L"BUTTON", L"&Solo selected", WS_TABSTOP | WS_DISABLED);
            control(Unmute, L"BUTTON", L"&Unmute all", WS_TABSTOP | WS_DISABLED);
            control(ActivityLabel, L"STATIC", L"Channel activity");
            control(Activity, L"EDIT", L"No active channel", ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE);
            control(OpenPreferences, L"BUTTON", L"&Preferences...", WS_TABSTOP);
            dark.AddDialogWithControls(hwnd);
            ready_ = true;
            layout();
            SetTimer(hwnd, 1, 100, nullptr);
            return 0;
        }
        if (msg == WM_SIZE && ready_) { layout(); return 0; }
        if (msg == WM_GETMINMAXINFO) {
            auto info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize = {pixels(320), pixels(315)};
            return 0;
        }
        if (msg == WM_TIMER && wp == 1) { refresh(); return 0; }
        if (msg == WM_COMMAND && ready_) {
            if (LOWORD(wp) == IDCANCEL && registered_) { DestroyWindow(hwnd); return 0; }
            if (HIWORD(wp) == BN_CLICKED) { command(LOWORD(wp)); return 0; }
            if (LOWORD(wp) == Channel && HIWORD(wp) == CBN_SELCHANGE) { refresh(); return 0; }
        }
        if (msg == WM_DESTROY) {
            KillTimer(hwnd, 1);
            if (registered_) { modeless_dialog_manager::g_remove(hwnd); registered_ = false; }
            return 0;
        }
        return Window::dispatch(msg, wp, lp);
    }
};

std::unique_ptr<Mixer> popup;
void show_mixer() {
    if (!popup || !popup->hwnd) {
        auto instance = std::make_unique<Mixer>();
        instance->initialize(core_api::get_main_window(), false);
        popup = std::move(instance);
    }
    ShowWindow(popup->hwnd, SW_RESTORE);
    SetForegroundWindow(popup->hwnd);
}
class Lifecycle : public initquit { public: void on_quit() override { popup.reset(); } };
initquit_factory_t<Lifecycle> lifecycle;

constexpr GUID mixer_guid{0xf7e32651,0xc5d9,0x411c,{0xad,0x7c,0x6c,0xf4,0x72,0x3f,0xe2,0xf0}};
class MixerMenu : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { return 1; }
    GUID get_command(t_uint32) override { return mixer_guid; }
    void get_name(t_uint32, pfc::string_base& out) override { out = "MDX channel mixer"; }
    bool get_description(t_uint32, pfc::string_base& out) override {
        out = "Mute or solo Yamaha YM2151 and PDX channels during MDX playback.";
        return true;
    }
    GUID get_parent() override { return mainmenu_groups::view; }
    t_uint32 get_sort_priority() override { return sort_priority_dontcare; }
    void execute(t_uint32, service_ptr_t<service_base>) override { show_mixer(); }
};
mainmenu_commands_factory_t<MixerMenu> mixer_factory;
}
}
