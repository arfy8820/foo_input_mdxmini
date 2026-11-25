#include "../src/engine.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace mdxmini_component;
const auto check = [] {};
void require(bool value, const char* what) { if (!value) throw std::runtime_error(what); }

std::vector<uint8_t> mdx(bool pcm8, bool loop) {
    std::vector<uint8_t> image{'M','D','X',' ','t','e','s','t',13,10,26};
    if (pcm8) image.insert(image.end(), {'s','m','o','k','e','.','p','d','x'});
    image.push_back(0);
    const size_t base = image.size();
    const int tracks = pcm8 ? 16 : 9;
    image.resize(base + 2 * (tracks + 1));
    auto write_offset = [&](int index) {
        auto value = image.size() - base;
        image[base + 2 * index] = static_cast<uint8_t>(value >> 8);
        image[base + 2 * index + 1] = static_cast<uint8_t>(value);
    };
    for (int i = 0; i < tracks; ++i) {
        write_offset(i + 1);
        const auto start = image.size();
        if (i == 0 || i == 1) {
            if (pcm8 && i == 0) image.push_back(0xe8);
            image.insert(image.end(), {0xfd,0,0xfb,15,static_cast<uint8_t>(0x80 + 36 + i * 7),47});
        }
        if (i == 15) image.insert(image.end(), {0xed,4,0x80,25});
        const auto back = static_cast<uint16_t>(start - (image.size() + 3));
        if (loop && i == 0) image.insert(image.end(), {0xf1,static_cast<uint8_t>(back >> 8),static_cast<uint8_t>(back)});
        else image.insert(image.end(), {0xf1,0});
    }
    write_offset(0);
    image.insert(image.end(), {0,7,15, 1,1,1,1, 0,0,0,0, 31,31,31,31,
                              0,0,0,0, 0,0,0,0, 15,15,15,15});
    return image;
}

std::vector<uint8_t> pdx() {
    std::vector<uint8_t> image(768 + 512);
    image[2] = 3; // First sample starts at offset 768.
    image[6] = 2; // 512 bytes of ADPCM.
    for (size_t i = 768; i < image.size(); ++i) image[i] = i % 16 < 8 ? 0x22 : 0xaa;
    return image;
}

std::vector<int16_t> render(Engine& engine, uint16_t mask) {
    engine.reset(check);
    std::array<int16_t, block_frames * 2> out;
    std::vector<int16_t> pcm;
    while (const int frames = engine.render(out.data(), block_frames, mask, check)) {
        pcm.insert(pcm.end(), out.begin(), out.begin() + frames * engine.channels());
        require(pcm.size() < 1000000, "Fixture did not terminate");
    }
    return pcm;
}

bool audible(const std::vector<int16_t>& samples) {
    return std::any_of(samples.begin(), samples.end(), [](int16_t value) { return value != 0; });
}
}

int main(int argc, char** argv) {
    try {
        const auto fm = mdx(false, false);
        const auto mixed = mdx(true, false);
        auto bank = pdx();
        Engine engine(fm, {});
        require(engine.title_bytes() == "MDX test" && engine.tracks() == 9, "MDX header handling");
        const auto duration = engine.measure(check);
        const auto both = render(engine, 0);
        const auto first = render(engine, 0xfffe);
        const auto second = render(engine, 0xfffd);
        require(audible(both) && audible(first) && audible(second) && first != second, "FM channel isolation");
        require(!audible(render(engine, 0xffff)), "FM mute all");
        require(std::llround(duration * engine.rate()) * 2 == both.size(), "Sample-bounded duration");
        std::array<int16_t, block_frames * 2> window;
        engine.seek(1024.0 / engine.rate(), check);
        require(engine.render(window.data(), block_frames, 0, check) == block_frames, "Sample seek");
        require(std::equal(window.begin(), window.end(), both.begin() + block_frames * 2), "Seek waveform");
        engine.reset(check);
        engine.render(window.data(), block_frames, 0xffff, check);
        engine.render(window.data(), block_frames, 0, check);
        require(std::equal(window.begin(), window.end(), both.begin() + block_frames * 2), "Muting changed FM evolution");

        Settings mono;
        mono.rate = 48000;
        mono.mono = true;
        Engine pcm(mixed, mono);
        require(pcm.tracks() == 16 && pcm.pdx_name_bytes() == "smoke.pdx", "PCM8 header handling");
        require(!audible(render(pcm, 0x7fff)), "Missing PDX was not silent");
        pcm.load_pdx(bank);
        const auto sample_track = render(pcm, 0x7fff);
        require(pcm.rate() == 48000 && pcm.channels() == 1 && audible(sample_track), "PDX channel 8 and mono output");
        require(!audible(render(pcm, 0xffff)), "PDX mute all");
        Settings no_fm;
        no_fm.fm_gain = 0;
        Engine fm_gain(fm, no_fm);
        require(!audible(render(fm_gain, 0)), "FM gain zero");
        Settings no_pcm;
        no_pcm.pcm_gain = 0;
        Engine pcm_gain(mixed, no_pcm);
        pcm_gain.load_pdx(bank);
        require(!audible(render(pcm_gain, 0x7fff)) && audible(render(pcm_gain, 0xff00)), "PCM gain zero");
        Engine concurrent(fm, {});
        auto job = std::async(std::launch::async, [&] { return render(concurrent, 0); });
        require(render(engine, 0) == both && job.get() == both, "Concurrent decoder state leaked");
        const auto looped = mdx(false, true);
        Settings one;
        one.loops = 1;
        Settings two = one;
        two.loops = 2;
        Engine once(looped, one), twice(looped, two);
        require(twice.measure(check) > once.measure(check), "Loop count");
        bool threw = false;
        try { Engine broken(std::span(fm).first(12), {}); }
        catch (const std::runtime_error&) { threw = true; }
        require(threw, "Truncated input accepted");
        if (argc > 1) {
            const auto folder = std::filesystem::path(argv[1]);
            std::filesystem::create_directories(folder);
            auto save = [&](const char* name, const std::vector<uint8_t>& image) {
                std::ofstream out(folder / name, std::ios::binary);
                out.write(reinterpret_cast<const char*>(image.data()), image.size());
                require(bool(out), "Could not write fixture");
            };
            save("smoke.mdx", mixed);
            save("smoke.pdx", bank);
        }
        std::cout << "PASS: MDX/PDX, separate chip gain and mute masks, fractional duration, seek, mono/rate, loops and concurrent decoding\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
