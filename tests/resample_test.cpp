// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sebastian Merz
// Tests cr::Resampler with sine waves: the result must be the same sine at the new rate, with no jumps at the
// boundaries between the chunks the game asks for, and about the right length. Also prints how loud tones of
// different pitch come out (the frequency response of the interpolation).
#include "../src/resample.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static std::vector<int16_t> Run(int srcRate, int dstRate, double hz, size_t srcFrames, const std::vector<size_t>& chunkSizes) {
    std::vector<int16_t> src(srcFrames * 2);
    for (size_t i = 0; i < srcFrames; ++i) {
        const int16_t v = (int16_t)(20000.0 * std::sin(2 * 3.14159265358979 * hz * i / srcRate));
        src[i * 2] = v;
        src[i * 2 + 1] = (int16_t)-v; // different channels, to catch mixed-up channels
    }
    size_t srcPos = 0;
    auto read = [&](int16_t* dest, size_t frames) {
        const size_t n = (std::min)(frames, srcFrames - srcPos);
        std::copy(src.begin() + srcPos * 2, src.begin() + (srcPos + n) * 2, dest);
        srcPos += n;
        return n;
    };
    cr::Resampler rs;
    std::vector<int16_t> out;
    size_t call = 0;
    for (;;) {
        const size_t ask = chunkSizes[call++ % chunkSizes.size()];
        std::vector<int16_t> part(ask * 2);
        const size_t got = rs.Fill(srcRate, dstRate, part.data(), ask, read);
        out.insert(out.end(), part.begin(), part.begin() + got * 2);
        if (got < ask) break;
    }
    return out;
}

static void RunCase(int srcRate, int dstRate, double hz, size_t srcFrames, const std::vector<size_t>& chunkSizes) {
    const std::vector<int16_t> out = Run(srcRate, dstRate, hz, srcFrames, chunkSizes);
    const size_t outFrames = out.size() / 2;
    const double expected = (double)srcFrames * dstRate / srcRate;
    CHECK(std::fabs((double)outFrames - expected) < 8);
    double maxErr = 0;
    for (size_t i = 8; i + 8 < outFrames; ++i) {
        const double ideal = 20000.0 * std::sin(2 * 3.14159265358979 * hz * i / dstRate);
        maxErr = (std::max)(maxErr, std::fabs(out[i * 2] - ideal));
        CHECK(std::abs(out[i * 2 + 1] + out[i * 2]) <= 1); // the channels stay separate
    }
    std::printf("%5d -> %5d Hz, %5.0f Hz tone, chunks %zu..: %zu frames (expected %.1f), max error %.1f of 20000\n", srcRate,
                dstRate, hz, chunkSizes[0], outFrames, expected, maxErr);
    CHECK(maxErr < 120); // 0.6 % of full scale for a 1 kHz tone
}

static void ResponseTable(int srcRate, int dstRate) {
    std::printf("\nLoudness of tones after %d -> %d Hz (0 dB = unchanged):\n", srcRate, dstRate);
    for (double hz : {500.0, 1000.0, 4000.0, 8000.0, 10000.0, 12000.0, 14000.0, 15000.0}) {
        if (hz >= srcRate / 2.0) continue;
        const std::vector<int16_t> out = Run(srcRate, dstRate, hz, (size_t)srcRate * 2, {4096});
        double sum = 0;
        size_t n = 0;
        for (size_t i = 2000; i + 2000 < out.size() / 2; ++i, ++n) sum += (double)out[i * 2] * out[i * 2];
        const double rms = std::sqrt(sum / n), ideal = 20000.0 / std::sqrt(2.0);
        std::printf("  %6.0f Hz: %+5.2f dB\n", hz, 20 * std::log10(rms / ideal));
    }
}

int main() {
    const std::vector<size_t> game = {98304, 4096, 4096, 3000, 12345, 1}; // sizes like the game asks
    RunCase(32000, 44100, 1000, 32000 * 3, game);
    RunCase(48000, 44100, 1000, 48000 * 3, game);
    RunCase(22050, 44100, 500, 22050 * 3, {1000});
    RunCase(32000, 44100, 1000, 32000 * 3, {1});    // one frame at a time
    RunCase(32000, 44100, 1000, 100, game);         // a track shorter than one chunk
    ResponseTable(32000, 44100);
    ResponseTable(22050, 44100);
    if (failures) { std::printf("%d failures\n", failures); return 1; }
    std::printf("\nall resampler tests passed\n");
    return 0;
}
