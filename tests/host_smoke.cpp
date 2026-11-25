#include <foobar2000/SDK/foobar2000.h>
#include <foobar2000/SDK/menu_helpers.h>
#include "../src/host.h"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

DECLARE_COMPONENT_VERSION("MDX host smoke test", "1", "For the isolated test player.");
VALIDATE_COMPONENT_FILENAME("foo_mdxmini_test.dll");

namespace {
using namespace mdxmini_component;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

class Test : public initquit {
public:
    void on_init() override {
        std::wstring folder(32768, L'\0');
        const auto length = GetEnvironmentVariableW(L"MDXMINI_TEST_DIR", folder.data(), static_cast<DWORD>(folder.size()));
        if (!length || length >= folder.size()) return;
        folder.resize(length);
        fb2k::inMainThread([folder = std::move(folder)] { run(folder); });
    }
private:
    static void run(const std::filesystem::path& folder) {
        std::ofstream log(folder / L"result.txt", std::ios::trunc);
        try {
            require(core_api::is_portable_mode_enabled(), "Test player is not portable");
            namespace fs = std::filesystem;
            pfc::string8 profile;
            require(filesystem::g_get_native_path(core_api::get_profile_path(), profile), "Profile is not local");
            require(fs::equivalent(folder.parent_path() / L"profile", fs::path(to_wide(profile.c_str()))),
                    "Player is using the wrong profile");
            input_entry::ptr entry, candidate;
            service_enum_t<input_entry> inputs;
            while (inputs.next(candidate))
                if (!strcmp(candidate->get_name_(), "MDX (mdxmini)")) entry = candidate;
            require(entry.is_valid() && entry->is_our_path("test.MDX", "MDX"), "MDX input service missing");
            abort_callback_impl abort;
            input_decoder::ptr decoder;
            const auto file = to_utf8((folder / L"smoke.mdx").wstring());
            entry->open(decoder, nullptr, file.c_str(), abort);
            file_info_impl info;
            decoder->get_info(0, info, abort);
            const auto title = info.meta_get("title", 0);
            const auto bank = info.info_get("mdx_pdx_status");
            require(title && !strcmp(title, "MDX test") && bank && !strcmp(bank, "loaded"),
                    "Host did not read MDX title or load its PDX bank");
            decoder->initialize(0, input_flag_no_looping, abort);
            audio_chunk_impl chunk;
            size_t frames = 0;
            bool audible = false;
            while (decoder->run(chunk, abort)) {
                require(chunk.get_channels() == 2 && chunk.get_sample_rate() == 44100, "Wrong host audio format");
                frames += chunk.get_sample_count();
                require(frames < 100000, "Fixture did not finish");
                for (size_t i = 0; i < chunk.get_sample_count() * 2; ++i)
                    audible |= chunk.get_data()[i] != 0;
            }
            require(audible && std::abs(double(frames) / 44100 - info.get_length()) < 0.00001,
                    "Host output and reported duration disagree");
            decoder->seek(0, abort);
            require(decoder->run(chunk, abort), "Host seek after EOF failed");
            const auto absent = to_utf8((folder / L"no-bank" / L"smoke.mdx").wstring());
            entry->open(decoder, nullptr, absent.c_str(), abort);
            decoder->get_info(0, info, abort);
            const auto missing = info.info_get("mdx_pdx_status");
            require(missing && !strcmp(missing, "missing"), "Host did not report a missing PDX");
            bool preferences = false, mixer = false;
            preferences_page::ptr page;
            service_enum_t<preferences_page> pages;
            while (pages.next(page)) preferences |= page->get_guid() == preferences_guid;
            mainmenu_commands::ptr commands;
            service_enum_t<mainmenu_commands> menus;
            while (menus.next(commands)) for (unsigned i = 0; i < commands->get_command_count(); ++i) {
                pfc::string8 name;
                commands->get_name(i, name);
                mixer |= !strcmp(name.c_str(), "MDX channel mixer");
            }
            require(preferences && mixer, "Preferences or mixer service missing");
            log << "PASS: MDX registration, PDX discovery and missing-bank status, PCM, EOF, seek, preferences and mixer menu\n";
        } catch (const std::exception& e) {
            log << "FAIL: " << e.what() << '\n';
        } catch (...) {
            log << "FAIL: foobar2000 service error\n";
        }
        log.close();
        fb2k::inMainThread([] { standard_commands::main_exit(); });
    }
};
initquit_factory_t<Test> test_factory;
}
