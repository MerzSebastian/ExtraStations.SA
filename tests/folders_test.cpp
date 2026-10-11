// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sebastian Merz
// Checks the folder-name contract.  Build:  cl /EHsc /std:c++17 tests\folders_test.cpp
#include "../src/folders.h"

#include <cstdio>

using namespace cr;

static int failures = 0;
static void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++failures;
}

int main() {
    Check(RoleForFolder("songs") == FolderRole::Songs, "songs");
    Check(RoleForFolder("Music") == FolderRole::Songs, "music is an alias of songs, case-insensitive");
    Check(RoleForFolder("IDENTS") == FolderRole::Idents, "idents, upper case");
    Check(RoleForFolder("jingles") == FolderRole::Idents, "jingles is an alias of idents");
    Check(RoleForFolder("ads") == FolderRole::Ads && RoleForFolder("Adverts") == FolderRole::Ads, "ads and adverts");
    Check(RoleForFolder("talk") == FolderRole::Talk, "talk");
    Check(RoleForFolder("dj") == FolderRole::Talk && RoleForFolder("banter") == FolderRole::Talk, "dj and banter are talk");
    Check(RoleForFolder("news") == FolderRole::Talk && RoleForFolder("weather") == FolderRole::Talk &&
              RoleForFolder("traffic") == FolderRole::Talk,
          "news, weather and traffic count as talk today");
    Check(RoleForFolder("intros") == FolderRole::Reserved && RoleForFolder("outros") == FolderRole::Reserved &&
              RoleForFolder("shows") == FolderRole::Reserved,
          "intros, outros and shows are reserved");
    Check(RoleForFolder("4") == FolderRole::Unknown, "a bare track type number is not a folder name");
    Check(RoleForFolder("sonngs") == FolderRole::Unknown, "a typo is unknown (reported in the log)");
    Check(RoleForFolder("") == FolderRole::Unknown, "empty name is unknown");
    std::printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
