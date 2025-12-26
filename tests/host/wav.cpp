#include <Arduino.h>
#include <fstream>
#include <vector>
#include <algorithm>
#include "AudioFileSourceSTDIO.h"
#include "AudioOutputSTDIO.h"
#include "AudioGeneratorWAV.h"

static constexpr std::streamoff WAV_HEADER_BYTES = 44;
static constexpr int BYTES_PER_24BIT_SAMPLE = 3;

static void write_le16(std::ofstream &f, uint16_t v) {
    f.put(v & 0xff);
    f.put((v >> 8) & 0xff);
}

static void write_le32(std::ofstream &f, uint32_t v) {
    f.put(v & 0xff);
    f.put((v >> 8) & 0xff);
    f.put((v >> 16) & 0xff);
    f.put((v >> 24) & 0xff);
}

static void write_junk_mono_8(const char *path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    uint32_t dataSize = 2;
    uint32_t junkSize = 4;
    uint32_t fmtSize = 16;
    uint32_t riffSize = 4 + (8 + junkSize) + (8 + fmtSize) + (8 + dataSize);

    f.write("RIFF", 4);
    write_le32(f, riffSize);
    f.write("WAVE", 4);

    f.write("JUNK", 4);
    write_le32(f, junkSize);
    f.write("\0\0\0\0", junkSize);

    f.write("fmt ", 4);
    write_le32(f, fmtSize);
    write_le16(f, 1);      // PCM
    write_le16(f, 1);      // channels
    write_le32(f, 8000);   // sample rate
    write_le32(f, 8000);   // byte rate
    write_le16(f, 1);      // block align
    write_le16(f, 8);      // bits per sample

    f.write("data", 4);
    write_le32(f, dataSize);
    f.put(0x00);
    f.put(0xff);
}

static void write_pcm24_stereo(const char *path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    uint32_t dataSize = BYTES_PER_24BIT_SAMPLE * 2;  // 2 channels, 1 frame
    uint32_t fmtSize = 16;
    uint32_t riffSize = 4 + (8 + fmtSize) + (8 + dataSize);

    f.write("RIFF", 4);
    write_le32(f, riffSize);
    f.write("WAVE", 4);

    f.write("fmt ", 4);
    write_le32(f, fmtSize);
    write_le16(f, 1);       // PCM
    write_le16(f, 2);       // channels
    write_le32(f, 44100);   // sample rate
    write_le32(f, 44100 * 2 * BYTES_PER_24BIT_SAMPLE);
    write_le16(f, BYTES_PER_24BIT_SAMPLE * 2);       // block align
    write_le16(f, 24);      // bits per sample

    f.write("data", 4);
    write_le32(f, dataSize);
    f.put(0xff);
    f.put(0xff);
    f.put(0x7f);            // Left: max positive
    f.put(0x00);
    f.put(0x00);
    f.put(0x80);            // Right: max negative
}

static bool decode_wav(const char *input, const char *output) {
    AudioFileSourceSTDIO in(input);
    AudioOutputSTDIO out;
    out.SetFilename(output);
    AudioGeneratorWAV wav;
    if (!wav.begin(&in, &out)) {
        return false;
    }
    while (wav.loop()) { /* noop */ }
    wav.stop();
    return true;
}

static std::vector<int16_t> read_samples(const char *path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) {
        return {};
    }
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size < WAV_HEADER_BYTES) {
        return {};
    }
    f.seekg(WAV_HEADER_BYTES, std::ios::beg); // Skip standard header
    std::vector<int16_t> samples((size - WAV_HEADER_BYTES) / 2);
    f.read(reinterpret_cast<char*>(samples.data()), samples.size() * sizeof(int16_t));
    return samples;
}

static bool ends_with(const std::vector<int16_t> &samples, const std::vector<int16_t> &expected) {
    if (samples.size() < expected.size()) {
        return false;
    }
    return std::equal(expected.rbegin(), expected.rend(), samples.rbegin());
}

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    write_junk_mono_8("junk_mono.wav");
    if (!decode_wav("junk_mono.wav", "out_mono.wav")) {
        return 1;
    }
    auto monoSamples = read_samples("out_mono.wav");
    // 0x00 -> -32768, 0xff -> 32512 after unsigned 8-bit to signed 16-bit conversion
    std::vector<int16_t> expected8 = {-32768, 32512};
    if (!ends_with(monoSamples, expected8)) {
        return 1;
    }

    write_pcm24_stereo("pcm24.wav");
    if (!decode_wav("pcm24.wav", "out_pcm24.wav")) {
        return 1;
    }
    auto stereoSamples = read_samples("out_pcm24.wav");
    std::vector<int16_t> expected24 = {0x7fff, -32768};
    if (!ends_with(stereoSamples, expected24)) {
        return 1;
    }

    return 0;
}
