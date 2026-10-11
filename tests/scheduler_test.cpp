// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sebastian Merz
// Standalone check of the scheduler's statistics. Build:  cl /EHsc /std:c++17 tests\scheduler_test.cpp
#include "../src/scheduler.h"

#include <cstdio>
#include <cstdlib>

using namespace cr;

struct Counts {
    long song = 0, ident = 0, adVanilla = 0, adCustom = 0, talkVanilla = 0, talkCustom = 0, identVanilla = 0, identCustom = 0,
         empty = 0;
    long refills = 0;
};

static Counts Run(const Rules& r, const Content& c, long refills, double banterFail = 0.5) {
    Scheduler s(r, c, 12345, banterFail);
    Counts n;
    for (long i = 0; i < refills; ++i) {
        ++n.refills;
        auto items = s.ChooseTracks();
        if (items.empty()) ++n.empty;
        for (auto& it : items) {
            switch (it.kind) {
            case Kind::Song: ++n.song; break;
            case Kind::Ident: (it.where == Where::Vanilla ? n.identVanilla : n.identCustom)++; break;
            case Kind::Ad: (it.where == Where::Vanilla ? n.adVanilla : n.adCustom)++; break;
            case Kind::Talk: (it.where == Where::Vanilla ? n.talkVanilla : n.talkCustom)++; break;
            }
        }
    }
    return n;
}

static void Print(const char* title, const Counts& n) {
    double t = (double)n.refills;
    std::printf("%s\n  per refill: song %.3f  ident(c/v) %.3f/%.3f  ad(c/v) %.3f/%.3f  talk(c/v) %.3f/%.3f  nothing %.3f\n", title,
                n.song / t, n.identCustom / t, n.identVanilla / t, n.adCustom / t, n.adVanilla / t, n.talkCustom / t,
                n.talkVanilla / t, n.empty / t);
}

static int failures = 0;
static void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++failures;
}

static bool Near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

int main() {
    const long N = 2000000;

    // 1. full set of custom files, no vanilla clips: the tree's own probabilities
    {
        Content c;
        c.customIdent = c.customAd = c.customTalk = true;
        c.vanillaIdent = c.vanillaAd = c.vanillaTalk = false;
        Counts n = Run(Rules{}, c, N, 0.0); // banter never fails: the tree's own probabilities
        Print("custom everything", n);
        // song-only refill: 0.95 * 0.9 = 0.855
        Check(Near(n.song / (double)N, 0.855, 0.003), "songs come up in 85.5% of refills");
        // talk (DJ banter): rare path 0.05 -> always; main 10% x 50% (non-intro) x 50% -> 0.95*0.1*0.5*0.5 = 0.02375
        Check(Near(n.talkCustom / (double)N, 0.05 + 0.02375, 0.003), "talk in about 7.4% of refills");
    }

    // 2. no custom talk/ads/idents and no vanilla ones: only songs ever play
    {
        Content c;
        c.vanillaAd = false;
        Counts n = Run(Rules{}, c, N);
        Print("songs only (nothing else available)", n);
        Check(n.identCustom + n.identVanilla + n.adCustom + n.adVanilla + n.talkCustom + n.talkVanilla == 0,
              "no ident/ad/talk when none exist");
        Check(Near(n.song / (double)N, 0.855, 0.003), "songs still 85.5% of refills (rest are empty refills)");
    }

    // 3. slot-12 defaults: only the game's generic ads exist -> talk falls back to vanilla ads
    {
        Content c; // vanillaAd = true, others off
        Counts n = Run(Rules{}, c, N);
        Print("default station without custom clips", n);
        Check(n.adVanilla > 0 && n.adCustom == 0, "falls back to the game's ads");
        Check(n.talkCustom + n.talkVanilla == 0, "no talk without sources");
    }

    // 4. Off really means off, Mixed splits roughly in half
    {
        Content c;
        c.customAd = c.customTalk = true;
        c.vanillaAd = true;
        Rules r;
        r.ad = Source::Off;
        r.talk = Source::Off;
        Counts n = Run(r, c, N);
        Print("ads and talk off", n);
        Check(n.adCustom + n.adVanilla + n.talkCustom + n.talkVanilla == 0, "ads and talk are never produced");

        r = Rules{};
        r.ad = Source::Mixed;
        Counts m = Run(r, c, N);
        Print("ads mixed", m);
        double frac = (double)m.adCustom / (double)(m.adCustom + m.adVanilla);
        Check(Near(frac, 0.5, 0.02), "mixed ads: about half custom, half vanilla");
    }

    // 5. Auto prefers custom, Vanilla forces vanilla
    {
        Content c;
        c.customAd = true;
        c.vanillaAd = true;
        Counts a = Run(Rules{}, c, N);
        Check(a.adVanilla == 0 && a.adCustom > 0, "Auto uses the user's ads when present");
        Rules r;
        r.ad = Source::Vanilla;
        Counts v = Run(r, c, N);
        Check(v.adCustom == 0 && v.adVanilla > 0, "Vanilla ignores the user's ads");
    }

    // 6. borrowing the game's clips from another station (e.g. 6 = Radio Los Santos)
    {
        Content c; // no custom clips, no vanilla clips on the host slot except generic ads
        c.vanillaAd = false;
        Rules r;
        r.talkFrom = 6;
        r.identFrom = 6;
        r.adFrom = 6;
        Scheduler s(r, c, 777, 0.0);
        long talkFrom6 = 0, identFrom6 = 0, wrong = 0, talk = 0, ident = 0;
        for (long i = 0; i < N; ++i)
            for (auto& it : s.ChooseTracks()) {
                if (it.kind == Kind::Song) continue;
                if (it.where != Where::Vanilla || it.from != 6) ++wrong;
                if (it.kind == Kind::Talk) { ++talk; talkFrom6 += (it.from == 6); }
                if (it.kind == Kind::Ident) { ++ident; identFrom6 += (it.from == 6); }
            }
        std::printf("borrowed from station 6: talk %ld ident %ld\n", talk, ident);
        Check(talk > 0 && talk == talkFrom6, "DJ talk is taken from the chosen station");
        Check(ident > 0 && ident == identFrom6, "idents are taken from the chosen station");
        Check(wrong == 0, "nothing comes from anywhere else");

        // a custom file set wins over the donor under Auto
        c.customTalk = true;
        Scheduler s2(r, c, 778, 0.0);
        long custom = 0, vanilla = 0;
        for (long i = 0; i < N; ++i)
            for (auto& it : s2.ChooseTracks())
                if (it.kind == Kind::Talk) (it.where == Where::Custom ? custom : vanilla)++;
        Check(custom > 0 && vanilla == 0, "Auto prefers the user's own talk over a donor station");
    }

    std::printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
