#include <foobar2000/SDK/foobar2000-lite.h>
#include <foobar2000/SDK/componentversion.h>
#include <foobar2000/SDK/input_impl.h>
#include <foobar2000/SDK/input_file_type.h>
#include <foobar2000/SDK/audio_chunk.h>
#include "host.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

DECLARE_COMPONENT_VERSION("MDX input (mdxmini)", "0.1.0",
    "X68000 MDX decoder with YM2151, PDX/PCM8 and a live channel mixer.\n"
    "Based on mdxmini by BouKiCHi and Misty De Meo.\n"
    "https://github.com/mistydemeo/mdxmini");
VALIDATE_COMPONENT_FILENAME("foo_input_mdxmini.dll");

namespace mdxmini_component {
namespace {
constexpr size_t mdx_limit = 4 * 1024 * 1024;
constexpr size_t pdx_limit = 16 * 1024 * 1024;
namespace fs = std::filesystem;

std::vector<uint8_t> read_mdx(const file::ptr& source, abort_callback& abort) {
    const auto size = source->get_size(abort);
    if (size != filesize_invalid && size > mdx_limit)
        throw exception_io_data("MDX exceeds 4 MiB");
    source->reopen(abort);
    std::vector<uint8_t> result;
    std::array<uint8_t, 32768> buffer;
    while (const auto count = source->read(buffer.data(), buffer.size(), abort)) {
        abort.check();
        if (count > mdx_limit - result.size()) throw exception_io_data("MDX exceeds 4 MiB");
        result.insert(result.end(), buffer.begin(), buffer.begin() + count);
    }
    return result;
}

fs::path bank_filename(std::string_view name) {
    auto filename = to_wide(name, 932);
    if (filename.empty() || filename == L"." || filename == L".." ||
        filename.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos ||
        filename.find_first_of(L"\r\n\t") != std::wstring::npos)
        throw std::runtime_error("MDX specifies an invalid PDX filename");
    fs::path result(filename);
    if (_wcsicmp(result.extension().c_str(), L".pdx") != 0) result += L".pdx";
    const auto stem = result.stem().wstring();
    if (!_wcsicmp(stem.c_str(), L"CON") || !_wcsicmp(stem.c_str(), L"PRN") ||
        !_wcsicmp(stem.c_str(), L"AUX") || !_wcsicmp(stem.c_str(), L"NUL") ||
        (stem.size() == 4 && stem[3] >= L'0' && stem[3] <= L'9' &&
         (!_wcsnicmp(stem.c_str(), L"COM", 3) || !_wcsnicmp(stem.c_str(), L"LPT", 3))))
        throw std::runtime_error("MDX specifies a reserved PDX filename");
    return result;
}

bool read_pdx(const fs::path& path, Engine& engine, abort_callback& abort) {
    std::error_code error;
    if (!fs::is_regular_file(path, error)) return false;
    const auto size = fs::file_size(path);
    if (!size || size > pdx_limit) throw std::runtime_error("PDX size is outside the supported range");
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not open the PDX bank");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    for (size_t offset = 0; offset < bytes.size();) {
        abort.check();
        const auto count = std::min<size_t>(32768, bytes.size() - offset);
        file.read(reinterpret_cast<char*>(bytes.data() + offset), count);
        if (!file) throw std::runtime_error("Could not read the PDX bank");
        offset += count;
    }
    engine.load_pdx(bytes);
    return true;
}

class Input : public input_stubs {
public:
    void open(file::ptr hint, const char* path, t_input_open_reason reason, abort_callback& abort) {
        if (reason == input_open_info_write) throw exception_tagging_unsupported();
        path_ = path;
        file_ = hint;
        input_open_file_helper(file_, path, reason, abort);
        const auto settings = host_settings();
        const auto image = read_mdx(file_, abort);
        try {
            engine_ = std::make_unique<Engine>(image, settings);
            title_ = to_utf8(to_wide(engine_->title_bytes(), 932));
            const auto bank = engine_->pdx_name_bytes();
            if (!bank.empty()) {
                const auto filename = bank_filename(bank);
                bank_ = to_utf8(filename.wstring());
                pfc::string8 native;
                if (filesystem::g_get_native_path(path, native)) {
                    const auto folder = fs::path(to_wide(native.c_str())).parent_path();
                    if (!folder.empty()) read_pdx(folder / filename, *engine_, abort);
                }
                if (!engine_->has_pdx() && !settings.pdx_directory.empty()) {
                    const fs::path folder(to_wide(settings.pdx_directory));
                    if (folder.is_absolute()) read_pdx(folder / filename, *engine_, abort);
                }
            }
            duration_ = engine_->measure([&] { abort.check(); });
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& e) {
            throw exception_io_data(e.what());
        }
    }

    void get_info(file_info& info, abort_callback& abort) {
        abort.check();
        info.reset();
        if (!title_.empty()) info.meta_set("title", title_.c_str());
        info.info_set("codec", "MDX (mdxmini)");
        info.info_set("encoding", "synthesized");
        info.info_set("mdx_fm_chip", "Yamaha YM2151");
        info.info_set_int("mdx_tracks", engine_->tracks());
        info.info_set_int("samplerate", engine_->rate());
        info.info_set_int("channels", engine_->channels());
        info.info_set_int("bitspersample", 16);
        if (!bank_.empty()) {
            info.info_set("mdx_pdx_bank", bank_.c_str());
            info.info_set("mdx_pdx_status", engine_->has_pdx() ? "loaded" : "missing");
        }
        info.set_length(duration_);
    }

    t_filestats get_file_stats(abort_callback& abort) { return file_->get_stats(abort); }
    t_filestats2 get_stats2(unsigned flags, abort_callback& abort) { return file_->get_stats2_(flags, abort); }

    void decode_initialize(unsigned flags, abort_callback& abort) {
        engine_->reset([&] { abort.check(); });
        session_.reset();
        if ((flags & input_flag_playback) && !(flags & input_flag_testing_integrity)) {
            session_ = std::make_shared<Session>(path_, engine_->tracks(), title_, engine_->has_pdx(), !bank_.empty());
            register_session(session_);
        }
    }

    bool decode_run(audio_chunk& chunk, abort_callback& abort) {
        std::array<int16_t, block_frames * 2> pcm;
        int frames;
        try {
            frames = engine_->render(pcm.data(), block_frames, session_ ? session_->render_mask() : 0,
                                     [&] { abort.check(); });
        } catch (const exception_aborted&) {
            throw;
        } catch (const std::exception& e) {
            throw exception_io_data(e.what());
        }
        publish_notes();
        if (!frames) return false;
        const int channels = engine_->channels();
        chunk.set_data_fixedpoint(pcm.data(), frames * channels * sizeof(int16_t), engine_->rate(),
                                  channels, 16, audio_chunk::g_guess_channel_config(channels));
        return true;
    }

    void decode_seek(double seconds, abort_callback& abort) {
        engine_->seek(seconds, [&] { abort.check(); });
        publish_notes();
    }
    bool decode_can_seek() { return true; }
    void retag(const file_info&, abort_callback&) { throw exception_tagging_unsupported(); }
    void remove_tags(abort_callback&) { throw exception_tagging_unsupported(); }
    static bool g_is_our_content_type(const char*) { return false; }
    static bool g_is_our_path(const char*, const char* extension) { return stricmp_utf8(extension, "mdx") == 0; }
    static const char* g_get_name() { return "MDX (mdxmini)"; }
    static GUID g_get_preferences_guid() { return preferences_guid; }
    static GUID g_get_guid() { return {0xab561ea8,0x65da,0x468c,{0xb0,0x96,0xab,0xe6,0x16,0xac,0xab,0x98}}; }
private:
    void publish_notes() {
        if (!session_) return;
        std::lock_guard lock(session_->note_mutex);
        session_->notes = engine_->notes();
    }
    file::ptr file_;
    std::unique_ptr<Engine> engine_;
    std::shared_ptr<Session> session_;
    std::string path_, title_, bank_;
    double duration_ = 0;
};

input_singletrack_factory_t<Input> factory;
DECLARE_FILE_TYPE("X68000 MDX", "*.MDX");
}
}
