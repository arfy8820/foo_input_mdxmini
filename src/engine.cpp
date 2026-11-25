#include "engine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>

extern "C" {
#include "mdxmini.h"
#include "class.h"
}

// The inherited YM2151 core uses shared scratch registers and tables.
namespace {
std::timed_mutex synthesis_mutex;
auto lock_synthesis(const mdxmini_component::Check& check) {
    std::unique_lock lock(synthesis_mutex, std::defer_lock);
    check();
    while (!lock.try_lock_for(std::chrono::milliseconds(10))) check();
    return lock;
}
}

extern "C" void* _get_mdx2151(songdata* data) { return data->mdx2151; }
extern "C" void* _get_mdxmml_ym2151(songdata* data) { return data->mdxmml_ym2151; }
extern "C" void* _get_pcm8(songdata* data) { return data->pcm8; }

namespace mdxmini_component {
namespace {
constexpr int seconds_limit = 20 * 60;
using Mdx = std::unique_ptr<MDX_DATA, decltype(&mdx_close_mdx)>;
using Pdx = std::unique_ptr<PDX_DATA, decltype(&mdx_close_pdx)>;

Mdx read_mdx(std::span<const uint8_t> image) {
    constexpr std::array<uint8_t, 3> delimiter{13, 10, 26};
    const auto title_end = std::search(image.begin(), image.end(), delimiter.begin(), delimiter.end());
    if (title_end == image.end() || title_end - image.begin() >= MDX_MAX_TITLE_LENGTH)
        throw std::runtime_error("Invalid MDX title header");
    const auto pdx_begin = title_end + delimiter.size();
    const auto pdx_end = std::find(pdx_begin, image.end(), 0);
    if (pdx_end == image.end() || pdx_end - pdx_begin > MDX_MAX_PDX_FILENAME_LENGTH - 5)
        throw std::runtime_error("Invalid MDX sample bank name");
    const size_t base = pdx_end - image.begin() + 1;
    if (image.size() - base < 20 || image.size() > 4 * 1024 * 1024)
        throw std::runtime_error("Invalid MDX track table");
    const auto offset = [&](size_t index) {
        return base + 256u * image[base + index * 2] + image[base + index * 2 + 1];
    };
    const size_t first = offset(1);
    if (first < base + 20 || first >= image.size())
        throw std::runtime_error("Invalid MDX first track offset");
    const int tracks = image[first] == MDX_SET_PCM8_MODE ? 16 : 9;
    const size_t data_start = base + 2 * (tracks + 1);
    if (data_start > image.size() || offset(0) < data_start || offset(0) > image.size())
        throw std::runtime_error("Invalid MDX voice table");

    Mdx mdx(static_cast<MDX_DATA*>(std::calloc(1, sizeof(MDX_DATA))), mdx_close_mdx);
    if (!mdx) throw std::bad_alloc();
    mdx->data = static_cast<unsigned char*>(std::calloc(image.size() + 8, 1));
    if (!mdx->data) throw std::bad_alloc();
    std::memcpy(mdx->data, image.data(), image.size());
    std::copy(image.begin(), title_end, mdx->data_title);
    std::copy(pdx_begin, pdx_end, mdx->pdx_name);
    mdx->haspdx = pdx_begin != pdx_end;
    mdx->base_pointer = static_cast<int>(base);
    mdx->length = static_cast<int>(image.size());
    mdx->voice_data_offset = static_cast<int>(offset(0));
    mdx->tracks = tracks;
    mdx->ispcm8mode = tracks == 16;
    for (int i = 0; i < tracks; ++i) {
        const auto start = offset(i + 1);
        if (start < data_start || start > image.size())
            throw std::runtime_error("Invalid MDX track offset");
        mdx->mml_data_offset[i] = static_cast<int>(start);
    }
    if (mdx_get_voice_parameter(mdx.get()) != 0) throw std::runtime_error("Invalid MDX voice data");
    return mdx;
}
}

struct Engine::State {
    explicit State(Mdx image) : mdx(std::move(image)) {}
    Mdx mdx;
    Pdx pdx{nullptr, mdx_close_pdx};
    songdata core{};
    void* sequencer = nullptr;
    int64_t scheduled = 0;
    int64_t fraction = 0;
    int remaining = 0;

    void clear() {
        if (sequencer) mdx_parse_mml_ym2151_async_finalize(&core);
        if (core.pcm8) _pcm8_finalize(core.pcm8);
        if (core.mdx2151) _mdx2151_finalize(core.mdx2151);
        if (core.mdxmml_ym2151) _mdxmml_ym2151_finalize(core.mdxmml_ym2151);
        core = {};
        sequencer = nullptr;
    }

    int next_tick() {
        const int rate = mdx->dsp_speed;
        const auto limit = int64_t(seconds_limit) * rate;
        if (scheduled >= limit) return 0;
        const bool next = mdx_parse_mml_ym2151_async(&core) != 0;
        if (mdx->invalid_data) throw std::runtime_error("Invalid MDX event stream");
        if (!next) return 0;
        fraction += int64_t(rate) * mdx_parse_mml_get_tempo(sequencer);
        const auto frames = std::min(fraction / 1000000, limit - scheduled);
        fraction %= 1000000;
        scheduled += frames;
        return static_cast<int>(frames);
    }
};

Engine::Engine(std::span<const uint8_t> image, Settings settings)
    : settings_(std::move(settings)), state_(std::make_unique<State>(read_mdx(image))) {
    if (std::find(rates.begin(), rates.end(), settings_.rate) == rates.end() ||
        settings_.loops < 1 || settings_.loops > 20 || settings_.fade < 1 || settings_.fade > 20 ||
        settings_.fm_gain < 0 || settings_.fm_gain > 100 || settings_.pcm_gain < 0 || settings_.pcm_gain > 100)
        throw std::runtime_error("Invalid MDX playback settings");
}

Engine::~Engine() {
    std::lock_guard lock(synthesis_mutex);
    state_->clear();
}

std::string Engine::title_bytes() const { return state_->mdx->data_title; }
std::string Engine::pdx_name_bytes() const { return state_->mdx->pdx_name; }
bool Engine::has_pdx() const { return state_->pdx != nullptr; }
int Engine::tracks() const { return state_->mdx->tracks; }

void Engine::load_pdx(std::span<uint8_t> image) {
    if (image.empty() || image.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid PDX bank size");
    Pdx parsed(mdx_open_pdx(image.data(), static_cast<long>(image.size())), mdx_close_pdx);
    if (!parsed) throw std::runtime_error("Invalid or oversized PDX sample table");
    state_->pdx = std::move(parsed);
}

void Engine::reset(const Check& check) {
    auto lock = lock_synthesis(check);
    auto& state = *state_;
    state.clear();
    auto& mdx = *state.mdx;
    mdx.dsp_speed = rate();
    mdx.fm_volume = settings_.fm_gain * 127 / 100;
    mdx.pcm_volume = settings_.pcm_gain * 127 / 100;
    mdx.max_infinite_loops = settings_.loops;
    mdx.fade_out_speed = settings_.fade;
    mdx.invalid_data = 0;
    mdx.mute_mask = 0;
    mdx.is_use_fm = mdx.is_use_fm_voice = mdx.is_use_ym2151 = mdx.is_use_pcm8 = 1;
    mdx.pdx_enable = state.pdx != nullptr;
    state.scheduled = state.fraction = state.remaining = 0;
    state.core.mdx2151 = _mdx2151_initialize();
    state.core.mdxmml_ym2151 = _mdxmml_ym2151_initialize();
    state.core.pcm8 = _pcm8_initialize();
    if (!state.core.mdx2151 || !state.core.mdxmml_ym2151 || !state.core.pcm8) throw std::bad_alloc();
    state.sequencer = mdx_parse_mml_ym2151_async_initialize(&mdx, state.pdx.get(), &state.core);
    if (!state.sequencer) throw std::runtime_error("Could not initialize MDX synthesis");
}

double Engine::measure(const Check& check) {
    reset(check);
    bool next = true;
    while (next) {
        auto lock = lock_synthesis(check);
        for (int i = 0; i < 128 && next; ++i) next = state_->next_tick() != 0;
    }
    const auto frames = state_->scheduled;
    reset(check);
    return double(frames) / rate();
}

int Engine::render(int16_t* output, int frames, uint16_t mask, const Check& check) {
    auto lock = lock_synthesis(check);
    if (!state_->sequencer || frames < 0 || frames > block_frames)
        throw std::runtime_error("Invalid MDX render state");
    const auto channel_count = channels();
    std::array<int16_t, block_frames * 2> stereo;
    state_->mdx->mute_mask = mask;
    int written = 0;
    while (written < frames) {
        if (!state_->remaining) state_->remaining = state_->next_tick();
        if (!state_->remaining) break;
        const auto count = std::min(frames - written, state_->remaining);
        auto destination = output + written * channel_count;
        mdx_parse_mml_ym2151_make_samples(channel_count == 2 ? destination : stereo.data(), count, &state_->core);
        if (channel_count == 1) {
            for (int i = 0; i < count; ++i) destination[i] = (int(stereo[i*2]) + stereo[i*2+1]) / 2;
        }
        state_->remaining -= count;
        written += count;
    }
    return written;
}

void Engine::seek(double seconds, const Check& check) {
    reset(check);
    const auto position = std::isfinite(seconds) ? std::clamp(seconds, 0.0, double(seconds_limit)) : 0.0;
    auto frames = static_cast<int64_t>(std::llround(position * rate()));
    std::array<int16_t, block_frames * 2> scratch;
    while (frames > 0) {
        const auto requested = static_cast<int>(std::min<int64_t>(frames, block_frames));
        if (render(scratch.data(), requested, 0, check) != requested) break;
        frames -= requested;
    }
}

std::array<int, maximum_channels> Engine::notes() const {
    std::array<int, maximum_channels> notes;
    notes.fill(-1);
    for (int i = 0; i < tracks(); ++i) notes[i] = state_->mdx->track[i].note;
    return notes;
}
}
