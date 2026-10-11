// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sebastian Merz
// What the sub folders of a station folder mean. This is a contract with everyone who builds a station pack:
// names never change meaning, aliases keep working, and names that are planned for later are reserved now.
//
//   songs   (music)                             the music; files directly in the station folder count as songs
//   idents  (ident, jingles, jingle)            station ids
//   ads     (ad, adverts, advert)               adverts
//   talk    (dj, banter, news, weather, traffic) speech: DJ talk, news, traffic, weather
//   intros, outros, shows                       reserved for later versions; ignored for now
//
// Names are case-insensitive. Anything else is "unknown" and is reported in the log.
#pragma once

#include <string>

namespace cr {

enum class FolderRole { Songs, Idents, Ads, Talk, Reserved, Unknown };

inline FolderRole RoleForFolder(const std::string& name) {
    std::string s;
    for (char c : name) s += (char)tolower((unsigned char)c);
    if (s == "songs" || s == "music") return FolderRole::Songs;
    if (s == "idents" || s == "ident" || s == "jingles" || s == "jingle") return FolderRole::Idents;
    if (s == "ads" || s == "ad" || s == "adverts" || s == "advert") return FolderRole::Ads;
    if (s == "talk" || s == "dj" || s == "banter" || s == "news" || s == "weather" || s == "traffic") return FolderRole::Talk;
    if (s == "intros" || s == "outros" || s == "shows") return FolderRole::Reserved;
    return FolderRole::Unknown;
}

// The canonical folder name of a role (what documentation and examples use).
inline const char* CanonicalFolderName(FolderRole role) {
    switch (role) {
    case FolderRole::Songs: return "songs";
    case FolderRole::Idents: return "idents";
    case FolderRole::Ads: return "ads";
    case FolderRole::Talk: return "talk";
    default: return "";
    }
}

} // namespace cr
