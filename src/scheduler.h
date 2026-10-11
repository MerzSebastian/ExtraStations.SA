// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sebastian Merz
// Decides what kind of track comes next on a station, following the structure of the game's own
// CAERadioTrackManager::ChooseTracksForStation, and resolves each
// kind to "the game's own clip", "one of the user's files" or "nothing" according to the station's rules.
//
// This file has no dependency on the game or on the audio code, so it can be unit tested on its own.
#pragma once

#include <random>
#include <string>
#include <vector>

namespace cr {

enum class Kind { Song, Ident, Ad, Talk };

// How a kind of track is sourced.
enum class Source {
    Auto,    // custom if the user supplied files for it, otherwise the game's own
    Vanilla, // always the game's own
    Custom,  // only the user's files
    Mixed,   // randomly either (only if both exist)
    Off      // never
};

enum class Where { Vanilla, Custom, Skip };

struct Rules {
    Source song = Source::Custom;
    Source ident = Source::Auto;
    Source ad = Source::Auto;
    Source talk = Source::Auto;
    // Borrow the game's own clips from another station (eRadioID 1..11, e.g. 6 = Radio Los Santos). 0 = the
    // station's own slot (which has none of its own on the User Track Player, except generic ads).
    int identFrom = 0, adFrom = 0, talkFrom = 0;
};

// What exists for a station: the user's files per kind, and whether the game has its own clip for it.
struct Content {
    bool customIdent = false, customAd = false, customTalk = false;
    // Vanilla ads/idents/DJ talk exist only on the game's real stations; the User Track Player slot has
    // generic ads only.
    bool vanillaIdent = false, vanillaAd = true, vanillaTalk = false;
};

inline bool ParseSource(const std::string& text, Source& out) {
    std::string s;
    for (char c : text) s += (char)tolower((unsigned char)c);
    if (s == "auto") out = Source::Auto;
    else if (s == "vanilla" || s == "default" || s == "original") out = Source::Vanilla;
    else if (s == "custom" || s == "own") out = Source::Custom;
    else if (s == "mixed" || s == "mix" || s == "both") out = Source::Mixed;
    else if (s == "off" || s == "none") out = Source::Off;
    else return false;
    return true;
}

class Scheduler {
public:
    // banterFailChance: how often the game's "queue DJ banter" step fails and an advert is queued instead. The
    // game's code does not show this rate (it depends on its internal state), so it is an adjustable estimate.
    Scheduler(const Rules& rules, const Content& content, unsigned seed = std::random_device{}(),
              double banterFailChance = 0.5)
        : rules_(rules), content_(content), rng_(seed), banterFail_(banterFailChance) {}

    // Resolves where a track of `kind` comes from.
    Where Resolve(Kind kind) {
        Source src;
        bool custom, vanilla;
        switch (kind) {
        case Kind::Ident: src = rules_.ident; custom = content_.customIdent; vanilla = content_.vanillaIdent || rules_.identFrom > 0; break;
        case Kind::Ad:    src = rules_.ad;    custom = content_.customAd;    vanilla = content_.vanillaAd || rules_.adFrom > 0;       break;
        case Kind::Talk:  src = rules_.talk;  custom = content_.customTalk;  vanilla = content_.vanillaTalk || rules_.talkFrom > 0;  break;
        default:          return Where::Custom; // songs always come from the user's files
        }
        switch (src) {
        case Source::Off:     return Where::Skip;
        case Source::Vanilla: return vanilla ? Where::Vanilla : Where::Skip;
        case Source::Custom:  return custom ? Where::Custom : Where::Skip;
        case Source::Mixed:
            if (custom && vanilla) return Prob(0.5) ? Where::Custom : Where::Vanilla;
            if (custom) return Where::Custom;
            return vanilla ? Where::Vanilla : Where::Skip;
        case Source::Auto:
        default:
            if (custom) return Where::Custom;
            return vanilla ? Where::Vanilla : Where::Skip;
        }
    }

    struct Item {
        Kind kind;
        Where where;
        int from = 0; // for Where::Vanilla: the station to take the game's clip from (0 = the host slot itself)
    };

    // The donor station configured for a kind (0 = none, i.e. the host slot's own).
    int DonorFor(Kind kind) const {
        switch (kind) {
        case Kind::Ident: return rules_.identFrom;
        case Kind::Ad: return rules_.adFrom;
        case Kind::Talk: return rules_.talkFrom;
        default: return 0;
        }
    }

    // One refill of the queue, like ChooseTracksForStation. May return nothing (all parts switched off or
    // an intro/outro-only outcome); the caller just asks again.
    std::vector<Item> ChooseTracks() {
        std::vector<Kind> kinds;

        if (!Prob(0.95)) {
            // rare path: station ident (50%), then DJ banter (falls back to an advert), then an intro
            if (Prob(0.5)) kinds.push_back(Kind::Ident);
            kinds.push_back(Kind::Talk); // fallback to Ad handled below
        } else if (Prob(0.9)) {
            kinds.push_back(Kind::Song);
        } else if (Prob(0.5)) {
            // ident (50%) and an intro, i.e. the DJ introduces the next song
            if (Prob(0.5)) kinds.push_back(Kind::Ident);
        } else {
            // an outro, then either [ident + intro] or [optional ident + DJ banter / advert]
            if (Prob(0.5)) {
                if (Prob(0.5)) kinds.push_back(Kind::Ident);
            } else {
                if (Prob(0.5)) kinds.push_back(Kind::Ident);
                kinds.push_back(Kind::Talk); // fallback to Ad handled below
            }
        }

        std::vector<Item> items;
        for (Kind k : kinds) {
            Where w = (k == Kind::Talk && Prob(banterFail_)) ? Where::Skip : Resolve(k);
            if (k == Kind::Talk && w == Where::Skip) { // "DJ banter failed -> advert"
                k = Kind::Ad;
                w = Resolve(Kind::Ad);
            }
            if (w != Where::Skip) items.push_back({k, w, w == Where::Vanilla ? DonorFor(k) : 0});
        }
        return items;
    }

private:
    bool Prob(double p) { return std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < p; }

    Rules rules_;
    Content content_;
    std::mt19937 rng_;
    double banterFail_;
};

} // namespace cr
