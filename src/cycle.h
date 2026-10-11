// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sebastian Merz
// The radio scroll cycle as an ordered list of entries.
//
// The game cycles  1 .. 11, 12 (User Track Player), 13 (radio off)  and wraps. We replace that fixed order with
// a list we build ourselves, so entries can be reordered, disabled, or added: custom stations are "virtual"
// entries that all ride on game slot 12 (told apart by `virt`), and the game's own stations are plain entries.
//
// This file only does the arithmetic; it knows nothing about the game, so it can be unit tested on its own.
#pragma once

#include <vector>

namespace cr {

constexpr int kUserTrackSlot = 12;
constexpr int kRadioOff = 13;

struct Elem {
    int game = 0; // the game's station id that is tuned for this entry
    int virt = 0; // 1-based id of the virtual station when game == 12, otherwise 0
    bool operator==(const Elem& o) const { return game == o.game && virt == o.virt; }
};

// Position of a station in the cycle, or -1 when it is not in it (e.g. a disabled station).
// If `virt` does not match, the first entry of that game slot is used.
inline int IndexOf(const std::vector<Elem>& cycle, int game, int virt) {
    int firstOfSlot = -1;
    for (int i = 0; i < (int)cycle.size(); ++i) {
        if (cycle[i].game != game) continue;
        if (cycle[i].virt == virt) return i;
        if (firstOfSlot < 0) firstOfSlot = i;
    }
    return firstOfSlot;
}

inline int Step(int index, int delta, int length) {
    return ((index + delta) % length + length) % length;
}

// The game's "pending retunes" value that makes it end up on `target`: the game computes
// target = requested + pending.
inline int PendingFor(int requestedGame, const Elem& target) { return target.game - requestedGame; }

} // namespace cr
