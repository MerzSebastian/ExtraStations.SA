// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sebastian Merz
// Runs the plugin's own sample rate conversion (cr::Resampler) on a WAV file, so the result can be listened to and
// measured outside the game.
//   resample_tool in.wav out.wav dstRate [reference.wav]
// in.wav: 16-bit stereo PCM (for example made with:  ffmpeg -i song.ogg -ac 2 -c:a pcm_s16le song.wav)
// reference.wav (optional): the same audio converted by another resampler at dstRate; the tool prints how far the
// plugin's result is from it (signal to noise ratio) and writes the difference, amplified, to out.wav.diff.wav.
#include "../src/resample.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

struct Wav { int rate = 0; std::vector<int16_t> samples; }; // interleaved stereo

static bool ReadWav(const char* path, Wav& w) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return false; }
    int channels = 0, bits = 0;
    for (;;) {
        uint8_t c[8];
        if (fread(c, 1, 8, f) != 8) break;
        uint32_t size = c[4] | (c[5] << 8) | (c[6] << 16) | ((uint32_t)c[7] << 24);
        if (!memcmp(c, "fmt ", 4)) {
            std::vector<uint8_t> fmt(size);
            fread(fmt.data(), 1, size, f);
            channels = fmt[2] | (fmt[3] << 8);
            w.rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
            bits = fmt[14] | (fmt[15] << 8);
        } else if (!memcmp(c, "data", 4)) {
            if (channels != 2 || bits != 16) { fclose(f); return false; }
            w.samples.resize(size / 2);
            w.samples.resize(fread(w.samples.data(), 2, size / 2, f));
            fclose(f);
            return true;
        } else {
            fseek(f, (long)size + (size & 1), SEEK_CUR);
        }
    }
    fclose(f);
    return false;
}

static void WriteWav(const char* path, int rate, const std::vector<int16_t>& s) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    const uint32_t dataBytes = (uint32_t)(s.size() * 2), byteRate = rate * 4;
    const uint16_t fmtTag = 1, ch = 2, align = 4, bits = 16;
    const uint32_t fmtSize = 16, riffSize = 36 + dataBytes, r = rate;
    fwrite("RIFF", 1, 4, f); fwrite(&riffSize, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtSize, 4, 1, f);
    fwrite(&fmtTag, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&r, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    fwrite(s.data(), 2, s.size(), f);
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 4) { std::printf("usage: resample_tool in.wav out.wav dstRate [reference.wav]\n"); return 2; }
    Wav in;
    if (!ReadWav(argv[1], in)) { std::printf("cannot read %s (16-bit stereo PCM wav expected)\n", argv[1]); return 1; }
    const int dst = atoi(argv[3]);
    size_t pos = 0;
    auto read = [&](int16_t* dest, size_t frames) {
        const size_t n = (std::min)(frames, in.samples.size() / 2 - pos);
        std::copy(in.samples.begin() + pos * 2, in.samples.begin() + (pos + n) * 2, dest);
        pos += n;
        return n;
    };
    cr::Resampler rs;
    std::vector<int16_t> out;
    const size_t sizes[] = {98304, 4096, 4096, 3000, 12345}; // chunk sizes like the game's
    for (size_t call = 0;; ++call) {
        const size_t ask = sizes[call % 5];
        std::vector<int16_t> part(ask * 2);
        const size_t got = rs.Fill(in.rate, dst, part.data(), ask, read);
        out.insert(out.end(), part.begin(), part.begin() + got * 2);
        if (got < ask) break;
    }
    WriteWav(argv[2], dst, out);
    std::printf("%d Hz -> %d Hz: %zu frames in, %zu frames out (%.2f s)\n", in.rate, dst, in.samples.size() / 2, out.size() / 2, out.size() / 2.0 / dst);
    if (argc > 4) {
        Wav ref;
        if (!ReadWav(argv[4], ref) || ref.rate != dst) { std::printf("reference unusable\n"); return 1; }
        const size_t n = (std::min)(out.size(), ref.samples.size());
        double sig = 0, err = 0;
        std::vector<int16_t> diff(n);
        for (size_t i = 0; i < n; ++i) {
            const double d = (double)out[i] - ref.samples[i];
            sig += (double)ref.samples[i] * ref.samples[i];
            err += d * d;
            diff[i] = (int16_t)(std::max)(-32768.0, (std::min)(32767.0, d * 10)); // difference, 20 dB louder
        }
        std::printf("compared with the reference: signal to difference ratio %.1f dB (%zu vs %zu frames)\n", 10 * std::log10(sig / (err + 1e-9)), out.size() / 2, ref.samples.size() / 2);
        WriteWav((std::string(argv[2]) + ".diff.wav").c_str(), dst, diff);
    }
    return 0;
}
