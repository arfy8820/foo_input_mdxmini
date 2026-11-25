#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace mdxmini_component {
inline constexpr int maximum_channels = 16;
inline constexpr int block_frames = 1024;
inline constexpr std::array rates{22050, 32000, 44100, 48000, 96000};

struct Settings {
    int rate = 44100;
    int loops = 3;
    int fade = 5;
    int fm_gain = 100;
    int pcm_gain = 100;
    bool mono = false;
    std::string pdx_directory;
    bool operator==(const Settings&) const = default;
};

using Check = std::function<void()>;
class Engine {
public:
    Engine(std::span<const uint8_t> image, Settings settings);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    std::string title_bytes() const;
    std::string pdx_name_bytes() const;
    void load_pdx(std::span<uint8_t> image);
    bool has_pdx() const;
    int tracks() const;
    int rate() const { return settings_.rate; }
    int channels() const { return settings_.mono ? 1 : 2; }
    double measure(const Check& check);
    void reset(const Check& check);
    int render(int16_t* output, int frames, uint16_t mask, const Check& check);
    void seek(double seconds, const Check& check);
    std::array<int, maximum_channels> notes() const;
private:
    struct State;
    Settings settings_;
    std::unique_ptr<State> state_;
};
}
