// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sebastian Merz
// Sample rate conversion for the User Track Player (see ExtraStations.cpp, "one sample rate per station").
// Stereo 16-bit frames, windowed sinc interpolation (48 taps, Kaiser window): flat to about 14 kHz when going from
// 32 kHz to 44.1 kHz. It reads its source in chunks through a callback, so it can sit between a decoder and the
// game's audio channel, which asks for any number of frames at a time.
// Meant for converting UP (the plugin always converts to the highest rate of a station); converting down with it
// would let frequencies above the new Nyquist rate fold back.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cr {

class Resampler {
public:
    // Fills up to outFrames frames at dstRate from a source at srcRate. read(dest, frames) delivers up to `frames`
    // stereo frames of the source and returns how many it could (fewer than asked = the source has ended).
    // Returns the number of frames produced (fewer than outFrames = the track is finished).
    template <class Read>
    size_t Fill(int srcRate, int dstRate, int16_t* out, size_t outFrames, Read read) {
        const Kernel& k = GetKernel();
        const double ratio = (double)srcRate / dstRate;
        size_t produced = 0;
        std::vector<int16_t> chunk;
        for (; produced < outFrames; ++produced) {
            const int n = (int)pos_; // real frame n is the one just before the output position
            const double t = pos_ - n;
            // buffer index i holds real frame i - kHistory; the filter needs real frames n-23 .. n+24 = buffer n .. n+47
            while (!eof_ && (int)(buf_.size() / 2) < n + kTaps) {
                const size_t want = (std::max<size_t>)(2048, (size_t)((double)outFrames * (ratio + 1.0)));
                chunk.resize(want * 2);
                const size_t got = read(chunk.data(), want);
                if (!started_ && got > 0) { // before the first frame: the first frame, held
                    started_ = true;
                    for (int i = 0; i < kHistory; ++i) { buf_.push_back(chunk[0]); buf_.push_back(chunk[1]); }
                }
                buf_.insert(buf_.end(), chunk.begin(), chunk.begin() + got * 2);
                if (got < want) eof_ = true;
            }
            const int have = (int)(buf_.size() / 2);
            if (!started_ || n + 1 >= have - kHistory) break; // end of the track
            float w[kTaps];
            const double ph = t * kPhases;
            const int p0 = (int)ph;
            const float f = (float)(ph - p0);
            for (int i = 0; i < kTaps; ++i) w[i] = k.w[p0][i] + (k.w[p0 + 1][i] - k.w[p0][i]) * f;
            for (int ch = 0; ch < 2; ++ch) {
                float v = 0;
                for (int i = 0; i < kTaps; ++i) {
                    const int idx = n + i < have ? n + i : have - 1; // after the last frame: the last frame, held
                    v += w[i] * buf_[(size_t)idx * 2 + ch];
                }
                v = v > 32767.f ? 32767.f : (v < -32768.f ? -32768.f : v);
                out[produced * 2 + ch] = (int16_t)(v >= 0 ? v + 0.5f : v - 0.5f);
            }
            pos_ += ratio;
        }
        const int consumed = (int)pos_; // drop the frames behind the filter
        if (consumed > 0 && (size_t)consumed * 2 <= buf_.size()) {
            buf_.erase(buf_.begin(), buf_.begin() + (size_t)consumed * 2);
            pos_ -= consumed;
        }
        return produced;
    }
    bool Finished() const { return eof_; }

private:
    static constexpr int kTaps = 48, kHistory = 23, kPhases = 1024;
    struct Kernel {
        float w[kPhases + 1][kTaps]; // weights for a fractional position of phase/kPhases, each set adds up to 1
    };
    static double Bessel0(double x) { // modified Bessel function I0, for the Kaiser window
        double sum = 1, term = 1;
        for (int k = 1; k < 40; ++k) {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }
    static const Kernel& GetKernel() {
        static const Kernel kernel = [] {
            Kernel k;
            const double pi = 3.14159265358979323846, cutoff = 0.97, beta = 7.5, half = kTaps / 2.0;
            for (int p = 0; p <= kPhases; ++p) {
                const double t = (double)p / kPhases;
                double sum = 0;
                for (int i = 0; i < kTaps; ++i) {
                    const double x = i - kHistory - t; // distance of the tap from the output position, in source frames
                    const double a = pi * cutoff * x;
                    const double sinc = std::fabs(x) < 1e-9 ? cutoff : std::sin(a) / (pi * x);
                    const double r = x / half;
                    const double window = std::fabs(r) < 1 ? Bessel0(beta * std::sqrt(1 - r * r)) / Bessel0(beta) : 0;
                    k.w[p][i] = (float)(sinc * window);
                    sum += k.w[p][i];
                }
                for (int i = 0; i < kTaps; ++i) k.w[p][i] = (float)(k.w[p][i] / sum);
            }
            return k;
        }();
        return kernel;
    }
    std::vector<int16_t> buf_;
    double pos_ = 0;
    bool eof_ = false, started_ = false;
};

} // namespace cr
