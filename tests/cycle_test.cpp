// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sebastian Merz
// Checks the scroll cycle arithmetic.  Build:  cl /EHsc /std:c++17 tests\cycle_test.cpp
#include "../src/cycle.h"

#include <cstdio>

using namespace cr;

static int failures = 0;
static void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++failures;
}

// 1..11, original User Track Player (V1), two custom stations (V2, V3), radio off
static std::vector<Elem> Standard() {
    std::vector<Elem> c;
    for (int r = 1; r <= 11; ++r) c.push_back({r, 0});
    c.push_back({12, 1});
    c.push_back({12, 2});
    c.push_back({12, 3});
    c.push_back({13, 0});
    return c;
}

static Elem Walk(const std::vector<Elem>& c, int game, int virt, int delta) {
    return c[Step(IndexOf(c, game, virt), delta, (int)c.size())];
}

int main() {
    const auto c = Standard();
    Check(c.size() == 15, "11 real + 3 virtual + off = 15 entries");
    Check(Walk(c, 11, 0, +1) == Elem{12, 1}, "WCTR -> next = User Track Player");
    Check(Walk(c, 12, 1, +1) == Elem{12, 2}, "UTP -> next = first custom station");
    Check(Walk(c, 12, 3, +1) == Elem{13, 0}, "last custom -> next = radio off");
    Check(Walk(c, 13, 0, +1) == Elem{1, 0}, "off -> next wraps to Playback FM");
    Check(Walk(c, 1, 0, -1) == Elem{13, 0}, "Playback FM -> previous = radio off");
    Check(Walk(c, 13, 0, -1) == Elem{12, 3}, "off -> previous = last custom station");
    Check(Walk(c, 12, 1, -1) == Elem{11, 0}, "UTP -> previous = WCTR");
    Check(Walk(c, 10, 0, +3) == Elem{12, 2}, "10 -> +3 = second virtual entry");

    bool lap = true, roundTrip = true;
    for (int s = 0; s < (int)c.size(); ++s) {
        lap &= Step(s, (int)c.size(), (int)c.size()) == s;
        for (int d = -20; d <= 20; ++d) roundTrip &= Step(Step(s, d, (int)c.size()), -d, (int)c.size()) == s;
    }
    Check(lap, "a full lap ends where it started");
    Check(roundTrip, "forward then backward returns to the same position");

    // a station disabled: leave out K-Rose (2) and the original User Track Player (V1)
    std::vector<Elem> d;
    for (int r = 1; r <= 11; ++r)
        if (r != 2) d.push_back({r, 0});
    d.push_back({12, 2});
    d.push_back({12, 3});
    d.push_back({13, 0});
    Check(IndexOf(d, 2, 0) == -1, "a disabled station is not in the cycle");
    Check(Walk(d, 1, 0, +1) == Elem{3, 0}, "Playback FM -> next skips the disabled K-Rose");
    Check(Walk(d, 11, 0, +1) == Elem{12, 2}, "WCTR -> next skips the disabled User Track Player");

    // reordered: a custom station placed between Playback FM and K-Rose
    std::vector<Elem> r;
    r.push_back({1, 0});
    r.push_back({12, 2});
    for (int s = 2; s <= 11; ++s) r.push_back({s, 0});
    r.push_back({13, 0});
    Check(Walk(r, 1, 0, +1) == Elem{12, 2}, "a custom station can sit anywhere in the cycle");
    Check(Walk(r, 12, 2, +1) == Elem{2, 0}, "...and the cycle continues after it");

    // the game's pending counter must make  requested + pending  land on the right game station
    Check(PendingFor(11, Elem{12, 1}) == 1, "11 -> slot 12 needs pending +1");
    Check(PendingFor(12, Elem{13, 0}) == 1, "12 -> off needs pending +1");
    Check(PendingFor(12, Elem{12, 2}) == 0, "between two virtual stations pending is 0");
    Check(PendingFor(5, Elem{12, 1}) == 7, "5 -> slot 12 needs pending +7");

    // fall back to the first entry of a slot when the virtual id is unknown
    Check(IndexOf(c, 12, 99) == 11, "unknown virtual id falls back to the first slot-12 entry");

    std::printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
