// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sebastian Merz
// ExtraStations.SA - GTA San Andreas (v1.0 US) ASI plugin.
//
// Adds any number of custom radio stations to the radio scroll cycle. Each station is a folder of audio files
// (songs plus optional idents, ads and talk) defined in ExtraStations.SA.ini. They are played by the game's own User
// Track Player, so bass boost, EQ, volume and ducking all apply, and the station name is shown on screen.
//
// How it works, in short:
//   * the game's user track scan is pointed at each station folder and the results are merged into its one track
//     list (BuildUserTrackList),
//   * the radio scroll is taken over at the key press so the cycle can contain extra entries, be reordered, or have
//     built-in stations switched off (cycle.h, OnScrollPress),
//   * when the game asks for the next user track, the tuned-in station answers (PickerHook), with the original
//     game's probabilities for songs / idents / ads / talk (scheduler.h),
//   * the game's own adverts are kept or replaced according to the station's settings (ChooseTracksHook,
//     ServiceAdvertHook).
//
// Addresses are for the 1.0 US exe (checked against the exe). The notes in
// docs/FUTURE_FEATURES.md list the structures and functions in detail.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shlobj.h>
#pragma comment(lib, "shell32.lib")

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "cycle.h"
#include "folders.h"
#include "resample.h"
#include "scheduler.h"

namespace {

constexpr const char* kVersion = "1.0.0"; // keep in step with the release tag (the release workflow checks it)

constexpr uintptr_t kAddrRadioTrackManager = 0x8CB6F8;
constexpr uintptr_t kAddrActiveStationId = kAddrRadioTrackManager + 0xC4 + 0x25; // m_ActiveSettings.StationID
constexpr uintptr_t kAddrIsVehicleRadioActive = 0x4E9800;
constexpr uintptr_t kAddrMenuActive = 0xBA6748 + 0x5C;  // CMenuManager::m_bMenuActive
constexpr uintptr_t kAddrGetRadioStationName = 0x4E9E10;  // CAERadioTrackManager::GetRadioStationName(char), thiscall
constexpr uintptr_t kAddrVersionCheck = 0x82457C;         // 16-bit value 0x94BF on 1.0 US

std::mt19937 g_rng{std::random_device{}()};

// A station: played by the game's own User Track Player. Any number of them can exist; they all live in the
// User Track Player slot (12) and are told apart by g_virt, which is the 1-based index of the one that is
// currently tuned in. The scroll cycle is extended so the player can step through all of them.
struct GameStation {
    std::string name;
    std::string folder;      // absolute, without trailing slash
    std::string folderLower; // for matching against the paths in the game's track list
    bool userTracks = false; // the player's own "User Tracks" folder: the game's original User Track Player
    int position = 13;       // place in the radio cycle (see HostStation)
    cr::Rules rules;
    bool shuffle = true;
    std::vector<int> tracks[4];       // indices into the game's user track list, per cr::Kind
    std::vector<size_t> order[4];     // shuffled order of those indices
    size_t pos[4] = {0, 0, 0, 0};
    int last[4] = {-1, -1, -1, -1};
    std::unique_ptr<cr::Scheduler> sched;
    std::deque<cr::Kind> queue;       // kinds decided by the scheduler, not yet played
};
std::vector<GameStation> g_game;
volatile int g_virt = 1;
std::vector<int> g_disabled;      // the game's own stations (1-12) left out of the cycle
std::vector<cr::Elem> g_cycle;    // the scroll cycle, in order

volatile bool g_run = true;

inline int ReadStationId() { return *(volatile signed char*)kAddrActiveStationId; }
inline bool MenuActive() { return *(volatile unsigned char*)kAddrMenuActive != 0; }

bool VehicleRadioActive() {
    // trivial getter: CAERadioTrackManager::IsVehicleRadioActive (thiscall)
    using Fn = bool(__fastcall*)(void*, void*);
    return reinterpret_cast<Fn>(kAddrIsVehicleRadioActive)(reinterpret_cast<void*>(kAddrRadioTrackManager), nullptr);
}

std::string ModuleDir();

void LogV(const char* fmt, va_list ap) {
    static std::string path = ModuleDir() + "ExtraStations.SA.log";
    FILE* f = fopen(path.c_str(), "a");
    if (!f) { // e.g. no write access in Program Files
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        f = fopen((std::string(tmp) + "ExtraStations.SA.log").c_str(), "a");
    }
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    fclose(f);
}

// What goes into the log by default: start-up information, what was found, problems. Not what happens on every
// key press or every track (that is LogDebug, which writes only with [Debug] Trace=1, see docs/DEBUGGING.md).
void Log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    LogV(fmt, ap);
    va_end(ap);
}

bool g_trace = false; // [Debug] Trace=1

void LogDebug(const char* fmt, ...) {
    if (!g_trace) return;
    va_list ap;
    va_start(ap, fmt);
    LogV(fmt, ap);
    va_end(ap);
}

std::string ModuleDir() {
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&ModuleDir), &self);
    char path[MAX_PATH];
    GetModuleFileNameA(self, path, MAX_PATH);
    std::string s(path);
    return s.substr(0, s.find_last_of("\\/") + 1);
}

std::string IniString(const char* sec, const char* key, const char* def, const std::string& ini) {
    char buf[512];
    GetPrivateProfileStringA(sec, key, def, buf, sizeof buf, ini.c_str());
    return buf;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
    return s;
}

// Audio files directly inside `dir` (sorted); subfolders go to `subdirs` if given.
std::vector<std::string> ScanDir(const std::string& dir, std::vector<std::string>* subdirs = nullptr) {
    std::vector<std::string> files;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return files;
    do {
        std::string n = fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (subdirs && n != "." && n != "..") subdirs->push_back(n);
            continue;
        }
        size_t dot = n.find_last_of('.');
        std::string ext = Lower(dot == std::string::npos ? "" : n.substr(dot));
        // the formats the game's own user track scanner accepts (FLAC is not among them)
        if (ext == ".ogg" || ext == ".mp3" || ext == ".wav" || ext == ".wma" || ext == ".wmv" || ext == ".aac" ||
            ext == ".m4a")
            files.push_back(dir + "\\" + n);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(files.begin(), files.end());
    return files;
}

// What is in a station folder, following the contract in folders.h: songs, idents, ads and talk (with their
// aliases) are used, intros / outros / shows are reserved for later, anything else is reported. Files directly in
// the station folder are songs. The log lines show a pack author at once what the plugin found.
struct FolderSummary {
    int songs = 0, idents = 0, ads = 0, talk = 0;
};

FolderSummary SummarizeStationFolder(const std::string& folder) {
    FolderSummary s;
    std::vector<std::string> subdirs;
    s.songs = (int)ScanDir(folder, &subdirs).size();
    for (auto& d : subdirs) {
        const cr::FolderRole role = cr::RoleForFolder(d);
        if (role == cr::FolderRole::Reserved) {
            Log("  folder '%s' is reserved for a later version and is ignored for now", d.c_str());
            continue;
        }
        if (role == cr::FolderRole::Unknown) {
            Log("  WARNING: folder '%s' is not a known name (songs, idents, ads, talk) and is ignored", d.c_str());
            continue;
        }
        const int count = (int)ScanDir(folder + "\\" + d).size();
        switch (role) {
        case cr::FolderRole::Songs: s.songs += count; break;
        case cr::FolderRole::Idents: s.idents += count; break;
        case cr::FolderRole::Ads: s.ads += count; break;
        case cr::FolderRole::Talk: s.talk += count; break;
        default: break;
        }
    }
    return s;
}

// ---- game engine mode: point the User Track Player at the station's songs folder -------------------
// The game scans "<GTA San Andreas User Files>\User Tracks" when it builds its user track list. That path
// is assembled in CAEUserRadioTrackManager's scan thread (0x4F4A20) as  <docs dir> + "\User Tracks".
// We patch the three pointers it pushes so the root becomes the station folder instead, then run the
// game's own scan. The game then plays those files with its normal radio audio chain.
constexpr uintptr_t kAddrUserRadioTrackManager = 0xB6B970;
constexpr uintptr_t kAddrScanUserTracks = 0x4F4BA0; // CAEUserRadioTrackManager::ScanUserTracks(), thiscall
constexpr uintptr_t kAddrReadUserTracks = 0x4F2FD0; // CAEUserRadioTrackManager::ReadUserTracks(), thiscall
constexpr uintptr_t kAddrDocsDirBuffer = 0xB71A60;  // CFileMgr::ms_dirName
constexpr uintptr_t kAddrUserTracksSuffix = 0x860ACC; // "\User Tracks"

char g_userTracksRoot[MAX_PATH];
const char g_emptySuffix[] = "";

bool PatchPointer(uintptr_t where, uintptr_t expectedOld, const void* replacement) {
    uint32_t* p = reinterpret_cast<uint32_t*>(where);
    if (*p != expectedOld) return false;
    DWORD old;
    VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &old);
    *p = reinterpret_cast<uint32_t>(replacement);
    VirtualProtect(p, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 4);
    return true;
}

std::string UserFilesDir() {
    char docs[MAX_PATH];
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs))) return {};
    return std::string(docs) + "\\GTA San Andreas User Files\\";
}

std::string ReadWholeFile(const std::string& path) {
    std::string data;
    if (FILE* f = fopen(path.c_str(), "rb")) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        data.resize((size_t)size);
        if (size > 0 && fread(&data[0], 1, (size_t)size, f) != (size_t)size) data.clear();
        fclose(f);
    }
    return data;
}

bool WriteWholeFile(const std::string& path, const std::string& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

bool g_scanPatched = false;

// Makes the game's user track scan read `dir` (absolute path, no trailing slash) instead of "User Tracks".
// The code is patched once; later calls only change the folder it points at.
bool SetUserTracksScanRoot(const std::string& dir) {
    strncpy(g_userTracksRoot, dir.c_str(), MAX_PATH - 1);
    if (g_scanPatched) return true;
    bool ok = PatchPointer(0x4F4A8D, kAddrDocsDirBuffer, g_userTracksRoot);     // strlen(root)
    ok = ok && PatchPointer(0x4F4AA6, kAddrDocsDirBuffer, g_userTracksRoot);    // strcpy(buf, root)
    ok = ok && PatchPointer(0x4F4AB2, kAddrUserTracksSuffix, g_emptySuffix);    // strcat(buf, "")
    Log(ok ? "User track scan redirected" : "Could not redirect the user track scan (code differs)");
    g_scanPatched = ok;
    return ok;
}

struct TrackListPart {
    std::string records; // {offset, length, type} records, 12 bytes each
    std::string files;   // the full paths, back to back
};

// Runs the game's own scan on one folder and returns what the game wrote.
bool ScanFolder(const std::string& dir, TrackListPart& out) {
    volatile int& state = *(volatile int*)(kAddrUserRadioTrackManager + 0x18);
    void* mgr = reinterpret_cast<void*>(kAddrUserRadioTrackManager);
    using Fn = int(__fastcall*)(void*, void*);
    for (int i = 0; i < 300 && state == 1; ++i) Sleep(100); // a scan that is already running finishes first
    state = 0;                                              // the game only starts a scan from state 0
    if (!SetUserTracksScanRoot(dir)) return false;
    reinterpret_cast<Fn>(kAddrScanUserTracks)(mgr, nullptr);
    for (int i = 0; i < 600 && state == 1; ++i) Sleep(100);
    if (state == 1) {
        Log("The scan of %s did not finish", dir.c_str());
        return false;
    }
    const std::string files = UserFilesDir();
    out.records = ReadWholeFile(files + "sa-utrax.dat");
    out.files = ReadWholeFile(files + "sa-ufiles.dat");
    Log("  scanned %s: %d tracks", dir.c_str(), (int)(out.records.size() / 12));
    return true;
}

// Scans every folder with the game's own scanner, merges the results into one track list in the game's file
// format, and makes the game load it. The station folders can be anywhere.
bool BuildUserTrackList(const std::vector<std::string>& dirs) {
    std::string records, files;
    for (const std::string& dir : dirs) {
        TrackListPart part;
        if (!ScanFolder(dir, part)) continue;
        const uint32_t base = (uint32_t)files.size();
        for (size_t i = 0; i + 12 <= part.records.size(); i += 12) {
            uint32_t rec[3];
            memcpy(rec, &part.records[i], 12);
            rec[0] += base;
            records.append(reinterpret_cast<const char*>(rec), 12);
        }
        files += part.files;
    }
    const std::string dir = UserFilesDir();
    // Keep the game's own list: copy it aside once, before the first time we replace it.
    for (const char* name : {"sa-utrax.dat", "sa-ufiles.dat"}) {
        const std::string original = dir + name, backup = original + ".before-extrastations";
        if (GetFileAttributesA(backup.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesA(original.c_str()) != INVALID_FILE_ATTRIBUTES) {
            WriteWholeFile(backup, ReadWholeFile(original));
            Log("Saved a copy of your original %s", name);
        }
    }
    if (!WriteWholeFile(dir + "sa-utrax.dat", records) || !WriteWholeFile(dir + "sa-ufiles.dat", files)) {
        Log("Could not write the merged track list to %s", dir.c_str());
        return false;
    }
    using Fn = int(__fastcall*)(void*, void*);
    int r = reinterpret_cast<Fn>(kAddrReadUserTracks)(reinterpret_cast<void*>(kAddrUserRadioTrackManager), nullptr);
    Log("Merged track list: %d tracks, the game loaded it (returned %d)", (int)(records.size() / 12), r);
    return true;
}

void LoadConfig() {
    std::string dir = ModuleDir();
    std::string ini = dir + "ExtraStations.SA.ini";
    // Every section whose name starts with "Station" is a station: [Station1], [Station2], [StationWestside] ...
    // There is no limit on how many you write.
    std::vector<char> names(64 * 1024);
    DWORD len = GetPrivateProfileSectionNamesA(names.data(), (DWORD)names.size(), ini.c_str());
    std::vector<std::string> sections;
    for (const char* p = names.data(); len && *p; p += strlen(p) + 1)
        if (Lower(p).rfind("station", 0) == 0) sections.push_back(p);

    for (const std::string& sec : sections) {
        std::string folder = IniString(sec.c_str(), "Folder", "", ini);
        // HostStation is the station's place in the radio cycle. 1-12 are the game's own stations (12 = User
        // Track Player), so 13 or more puts a station after all of them. Stations with the same number keep
        // the order they have in the file.
        int host = (int)GetPrivateProfileIntA(sec.c_str(), "HostStation", 13, ini.c_str());
        if (folder.empty() || host < 0) {
            Log("[%s] skipped: needs Folder= and a HostStation of 0 or more", sec.c_str());
            continue;
        }
        const std::string engine = Lower(IniString(sec.c_str(), "Engine", "Game", ini));
        if (engine != "game")
            Log("[%s] Engine=%s is no longer supported: every station is played by the game's own player", sec.c_str(),
                engine.c_str());

        GameStation gs;
        gs.name = IniString(sec.c_str(), "Name", sec.c_str(), ini);
        gs.position = host;
        gs.shuffle = GetPrivateProfileIntA(sec.c_str(), "Shuffle", 1, ini.c_str()) != 0;
        // relative folders: try next to the .asi first, then the game directory
        bool absolute = folder.size() > 1 && (folder[1] == ':' || folder[0] == '\\');
        std::string resolved = absolute ? folder : dir + folder;
        if (!absolute && GetFileAttributesA(resolved.c_str()) == INVALID_FILE_ATTRIBUTES) resolved = folder;
        char full[MAX_PATH];
        GetFullPathNameA(resolved.c_str(), MAX_PATH, full, nullptr);
        gs.folder = full;
        while (gs.folder.size() > 3 && (gs.folder.back() == '\\' || gs.folder.back() == '/')) gs.folder.pop_back();
        gs.folderLower = Lower(gs.folder);

        const FolderSummary summary = SummarizeStationFolder(gs.folder);
        Log("Station '%s': position %d, folder %s: songs=%d idents=%d ads=%d talk=%d", gs.name.c_str(), host,
            gs.folder.c_str(), summary.songs, summary.idents, summary.ads, summary.talk);
        if (summary.songs == 0) {
            Log("[%s] skipped: no songs (put audio files in %s\\songs)", sec.c_str(), gs.folder.c_str());
            continue;
        }

        auto source = [&](const char* key, cr::Source def) {
            cr::Source s = def;
            if (!cr::ParseSource(IniString(sec.c_str(), key, "", ini), s)) s = def;
            return s;
        };
        gs.rules.ident = source("Idents", cr::Source::Auto);
        gs.rules.ad = source("Ads", cr::Source::Auto);
        gs.rules.talk = source("Talk", cr::Source::Auto);
        gs.rules.identFrom = (int)GetPrivateProfileIntA(sec.c_str(), "IdentsFrom", 0, ini.c_str());
        gs.rules.adFrom = (int)GetPrivateProfileIntA(sec.c_str(), "AdsFrom", 0, ini.c_str());
        gs.rules.talkFrom = (int)GetPrivateProfileIntA(sec.c_str(), "TalkFrom", 0, ini.c_str());
        g_game.push_back(std::move(gs));
    }
}

// ---- function hook ------------------------------------------------------------------------------
// Overwrites the first `len` bytes (whole instructions, no relative operands) of `addr` with a jump to
// `hook` and returns a trampoline that runs the original code.
void* HookFunction(uintptr_t addr, const uint8_t* expected, size_t len, void* hook) {
    uint8_t* target = reinterpret_cast<uint8_t*>(addr);
    if (memcmp(target, expected, len) != 0) return nullptr;
    uint8_t* tramp = static_cast<uint8_t*>(VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return nullptr;
    memcpy(tramp, target, len);
    tramp[len] = 0xE9; // jmp back to target+len
    *reinterpret_cast<int32_t*>(tramp + len + 1) =
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(target + len) - reinterpret_cast<uintptr_t>(tramp + len + 5));
    DWORD old;
    VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old);
    target[0] = 0xE9; // jmp hook
    *reinterpret_cast<int32_t*>(target + 1) =
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(hook) - reinterpret_cast<uintptr_t>(target + 5));
    for (size_t i = 5; i < len; ++i) target[i] = 0x90;
    VirtualProtect(target, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, len);
    return tramp;
}

// Replaces the station name shown by the game for our stations.
// GTA SA on PC uses 8-bit GXT strings, so the name is returned as a plain char string.
using NameFn = const char*(__stdcall*)(int);
NameFn g_origName = nullptr;

const char* __stdcall NameHook(int id) {
    const int station = (signed char)id;
    if (station == cr::kUserTrackSlot && !g_game.empty()) { // the name of the virtual station that is tuned in
        int v = g_virt;
        if (v < 1 || v > (int)g_game.size()) v = 1;
        if (g_game[v - 1].userTracks) return g_origName(id); // the game's own "User Track Player"
        return g_game[v - 1].name.c_str();
    }
    return g_origName(id);
}

bool InstallNameHook() {
    // mov eax,[esp+4] ; sub esp,8   (7 bytes)
    static const uint8_t expected[7] = {0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x08};
    void* tramp = HookFunction(kAddrGetRadioStationName, expected, sizeof expected, reinterpret_cast<void*>(&NameHook));
    if (!tramp) {
        Log("GetRadioStationName prologue does not match - custom name not shown");
        return false;
    }
    g_origName = reinterpret_cast<NameFn>(tramp);
    Log("Station name hook installed");
    return true;
}

// ---- game engine stations: track list, track picker, scroll cycle ------------------------------------
constexpr uintptr_t kAddrCheckForStationRetune = 0x4EB660; // CAERadioTrackManager::CheckForStationRetune(), per frame
constexpr uintptr_t kAddrSelectUserTrackIndex = 0x4F3250;  // CAEUserRadioTrackManager::SelectUserTrackIndex()
constexpr uintptr_t kAddrUserTrackIndex = 0xBA67F4;        // FrontEndMenuManager.m_UserTrackIndex

// The game's user track list: sa-utrax.dat holds {offset, length, type} records into sa-ufiles.dat, which is
// the plain concatenation of the full paths. Returns the path of every track, in the game's own order.
std::vector<std::string> ReadUserTrackList() {
    char docs[MAX_PATH];
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs))) return {};
    const std::string dir = std::string(docs) + "\\GTA San Andreas User Files\\";
    auto readAll = [](const std::string& path) {
        std::string data;
        if (FILE* f = fopen(path.c_str(), "rb")) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            data.resize((size_t)size);
            if (size > 0 && fread(&data[0], 1, (size_t)size, f) != (size_t)size) data.clear();
            fclose(f);
        }
        return data;
    };
    const std::string records = readAll(dir + "sa-utrax.dat"), files = readAll(dir + "sa-ufiles.dat");
    std::vector<std::string> paths;
    for (size_t i = 0; i + 12 <= records.size(); i += 12) {
        uint32_t offset, length;
        memcpy(&offset, &records[i], 4);
        memcpy(&length, &records[i + 4], 4);
        paths.push_back(offset + (size_t)length <= files.size() ? files.substr(offset, length) : std::string());
    }
    return paths;
}

// Sorts the game's user track list into the stations (by folder) and their track kinds (by sub folder).
void BuildGameStationLists() {
    const std::vector<std::string> paths = ReadUserTrackList();
    Log("The game's user track list has %d tracks", (int)paths.size());
    for (auto& gs : g_game)
        for (int k = 0; k < 4; ++k) {
            gs.tracks[k].clear();
            gs.order[k].clear();
            gs.pos[k] = 0;
        }
    for (size_t i = 0; i < paths.size(); ++i) {
        std::string p = Lower(paths[i]);
        for (char& c : p)
            if (c == '/') c = '\\';
        for (auto& gs : g_game) {
            const size_t n = gs.folderLower.size();
            if (p.size() <= n + 1 || p.compare(0, n, gs.folderLower) != 0 || p[n] != '\\') continue;
            const std::string rel = p.substr(n + 1);
            const size_t slash = rel.find('\\');
            cr::Kind kind = cr::Kind::Song;
            if (slash != std::string::npos && !gs.userTracks) { // the user's own folder: everything is music
                switch (cr::RoleForFolder(rel.substr(0, slash))) {
                case cr::FolderRole::Songs: kind = cr::Kind::Song; break;
                case cr::FolderRole::Idents: kind = cr::Kind::Ident; break;
                case cr::FolderRole::Ads: kind = cr::Kind::Ad; break;
                case cr::FolderRole::Talk: kind = cr::Kind::Talk; break;
                default: continue; // reserved or unknown folder (reported when the station is scanned)
                }
            }
            gs.tracks[(int)kind].push_back((int)i);
            break;
        }
    }
    for (auto& gs : g_game) {
        cr::Content content;
        content.customIdent = !gs.tracks[(int)cr::Kind::Ident].empty();
        content.customAd = !gs.tracks[(int)cr::Kind::Ad].empty();
        content.customTalk = !gs.tracks[(int)cr::Kind::Talk].empty();
        content.vanillaIdent = content.vanillaTalk = content.vanillaAd = false; // borrowing the game's clips: later
        gs.sched = std::make_unique<cr::Scheduler>(gs.rules, content);
        Log("  '%s': songs=%d idents=%d ads=%d talk=%d", gs.name.c_str(), (int)gs.tracks[0].size(),
            (int)gs.tracks[1].size(), (int)gs.tracks[2].size(), (int)gs.tracks[3].size());
    }
}

// Chooses the next track of a station: the scheduler decides the kind (song, ident, ad, talk), then the next
// file of that kind comes from a shuffled deck, so nothing repeats before everything has played.
int NextOfKind(GameStation& gs, cr::Kind kind) {
    if (gs.tracks[(int)kind].empty()) kind = cr::Kind::Song;
    const int k = (int)kind;
    const std::vector<int>& list = gs.tracks[k];
    if (list.empty()) return -1;
    if (gs.pos[k] >= gs.order[k].size()) {
        gs.order[k].resize(list.size());
        for (size_t i = 0; i < list.size(); ++i) gs.order[k][i] = i;
        if (gs.shuffle) {
            std::shuffle(gs.order[k].begin(), gs.order[k].end(), g_rng);
            if (list.size() > 1 && list[gs.order[k][0]] == gs.last[k]) std::swap(gs.order[k][0], gs.order[k][1]);
        }
        gs.pos[k] = 0;
    }
    const int index = list[gs.order[k][gs.pos[k]++]];
    gs.last[k] = index;
    return index;
}

// The scheduler decides the kind (song, ident, ad, talk) of the next track, then the next file of that kind comes
// from the deck.
int PickTrack(GameStation& gs) {
    for (int tries = 0; tries < 50 && gs.queue.empty() && gs.sched; ++tries)
        for (auto& item : gs.sched->ChooseTracks())
            if (item.where == cr::Where::Custom) gs.queue.push_back(item.kind);
    cr::Kind kind = cr::Kind::Song;
    if (!gs.queue.empty()) {
        kind = gs.queue.front();
        gs.queue.pop_front();
    }
    return NextOfKind(gs, kind);
}

// ---- the game's own clips in the queue ---------------------------------------------------------------
// On the User Track Player slot the game fills its queue with user tracks and, now and then, one of its own
// adverts (English voices). Right after it has filled the queue we look at it and, depending on the station's
// Ads= / Idents= / Talk= setting, keep the game's clip, swap it for one of the station's own, or drop it.
constexpr uintptr_t kAddrChooseTracksCallSite = 0x4EB4F6; // StartRadio calls ChooseTracksForStation here
constexpr uintptr_t kAddrChooseTracks = 0x4EB180;
constexpr int kQueueTypeIdent = 0, kQueueTypeAdvert = 1, kQueueTypeDj = 2, kQueueTypeUserTrack = 7, kQueueTypeNone = 6;

using ChooseTracksFn = void(__fastcall*)(void*, void*, int);
uintptr_t g_chooseTracksTarget = kAddrChooseTracks;

// What to do with one of the game's clips that is in the queue.
cr::Where ResolveQueued(cr::Source src, bool haveCustom) {
    switch (src) {
    case cr::Source::Off: return cr::Where::Skip;
    case cr::Source::Vanilla: return cr::Where::Vanilla;
    case cr::Source::Custom: return haveCustom ? cr::Where::Custom : cr::Where::Skip;
    case cr::Source::Mixed: return haveCustom && (g_rng() & 1) ? cr::Where::Custom : cr::Where::Vanilla;
    case cr::Source::Auto:
    default: return haveCustom ? cr::Where::Custom : cr::Where::Vanilla;
    }
}

void __fastcall ChooseTracksHook(void* self, void* edx, int station) {
    reinterpret_cast<ChooseTracksFn>(g_chooseTracksTarget)(self, edx, station);
    if ((signed char)station != cr::kUserTrackSlot || g_game.empty()) return;
    int v = g_virt;
    if (v < 1 || v > (int)g_game.size()) v = 1;
    GameStation& gs = g_game[v - 1];

    uint8_t* m = static_cast<uint8_t*>(self);
    int* ids = reinterpret_cast<int*>(m + 0x88);          // the queued tracks
    int8_t* types = reinterpret_cast<int8_t*>(m + 0xB4);  // what kind each one is
    int8_t* indices = reinterpret_cast<int8_t*>(m + 0xBB);
    bool changed = false;
    for (int i = 0; i < 5; ++i) {
        cr::Kind kind;
        cr::Source src;
        if (types[i] == kQueueTypeAdvert) { kind = cr::Kind::Ad; src = gs.rules.ad; }
        else if (types[i] == kQueueTypeIdent) { kind = cr::Kind::Ident; src = gs.rules.ident; }
        else if (types[i] == kQueueTypeDj) { kind = cr::Kind::Talk; src = gs.rules.talk; }
        else continue;
        const cr::Where w = ResolveQueued(src, !gs.tracks[(int)kind].empty());
        if (w == cr::Where::Vanilla) continue; // keep the game's own clip
        // the game's clip is replaced by a track of ours: one of the station's own clips, or a song when it is off
        const int index = NextOfKind(gs, w == cr::Where::Custom ? kind : cr::Kind::Song);
        if (index < 0) continue;
        LogDebug("Queue slot %d: the game's %s replaced by track #%d", i, kind == cr::Kind::Ad ? "advert" : kind == cr::Kind::Ident ? "ident" : "DJ talk", index);
        ids[i] = index;
        types[i] = kQueueTypeUserTrack;
        indices[i] = -1;
        changed = true;
    }
    if (changed)
        LogDebug("  queue is now [%d %d %d %d %d]", (int)types[0], (int)types[1], (int)types[2], (int)types[3], (int)types[4]);
}

// The game's per-frame update (CAERadioTrackManager::Service) also keeps a two-track lookahead for the User Track
// Player and, at one point, asks for one of its own adverts:  ChooseAdvertIndex(12) at 0x4EBBA5, and right after
// it marks slot 0 as an advert (mov byte ptr [ebp+0F0h],1 at 0x4EBBB5). We hook that call, decide with the
// station's Ads= setting, and set the slot's type ourselves (the game's own write is removed).
constexpr uintptr_t kAddrServiceAdvertCall = 0x4EBBA5;
constexpr uintptr_t kAddrServiceAdvertTypeWrite = 0x4EBBB5;
using AdvertFn = int(__fastcall*)(void*, void*, int);
uintptr_t g_advertIndexTarget = 0x4E9570; // CAERadioTrackManager::ChooseAdvertIndex

// The tuned-in station decides about an advert the game is about to queue: true = use `index` (a track of ours)
// instead of one of the game's own adverts.
bool PatchCallSite(uintptr_t site, uintptr_t* originalTarget, void* hook); // defined with the scroll code below

bool TakeCustomAdvert(int& index, const char* where) {
    if (g_game.empty()) return false;
    int v = g_virt;
    if (v < 1 || v > (int)g_game.size()) v = 1;
    GameStation& gs = g_game[v - 1];
    const cr::Where w = ResolveQueued(gs.rules.ad, !gs.tracks[(int)cr::Kind::Ad].empty());
    if (w == cr::Where::Vanilla) return false;
    index = NextOfKind(gs, w == cr::Where::Custom ? cr::Kind::Ad : cr::Kind::Song);
    if (index < 0) return false;
    LogDebug("%s: the game's advert replaced by track #%d on '%s'", where, index, gs.name.c_str());
    return true;
}

int __fastcall ServiceAdvertHook(void* self, void* edx, int station) {
    int8_t& type = *reinterpret_cast<int8_t*>(static_cast<uint8_t*>(self) + 0xF0);
    int index;
    if (TakeCustomAdvert(index, "Lookahead")) {
        type = kQueueTypeUserTrack;
        return index;
    }
    type = kQueueTypeAdvert; // what the removed instruction did
    return reinterpret_cast<AdvertFn>(g_advertIndexTarget)(self, edx, station);
}

// The third place: when a track ends, the game moves its queue along and appends the next items; for the User
// Track Player slot that can be one of its adverts (0x4EAAC6 asks for it, 0x4EAACE marks the new slot as an
// advert). The slot index lives in registers, so the type write is replaced by a small thunk that sets the type
// (and the index byte of a user track) from what the hook decided.
constexpr uintptr_t kAddrAppendAdvertCall = 0x4EAAC6;
constexpr uintptr_t kAddrAppendAdvertTypeWrite = 0x4EAACE;
uintptr_t g_appendAdvertTarget = 0x4E9570;
volatile uint8_t g_appendType = kQueueTypeAdvert, g_appendIndex = 0;

int __fastcall AppendAdvertHook(void* self, void* edx, int station) {
    int index;
    if (TakeCustomAdvert(index, "Queue append")) {
        g_appendType = kQueueTypeUserTrack;
        g_appendIndex = (uint8_t)index;
        return index;
    }
    g_appendType = kQueueTypeAdvert;
    return reinterpret_cast<AdvertFn>(g_appendAdvertTarget)(self, edx, station);
}

// mov [edi+ebp+2Ch], type  /  (user tracks) mov [edi+ebp+33h], index      -- registers as in the game's code
__declspec(naked) void AppendTypeThunk() {
    __asm {
        push eax
        mov al, byte ptr [g_appendType]
        mov byte ptr [edi+ebp+2Ch], al
        cmp al, 7
        jne done
        mov al, byte ptr [g_appendIndex]
        mov byte ptr [edi+ebp+33h], al
    done:
        pop eax
        ret
    }
}

bool InstallAppendAdvertHook() {
    static const uint8_t typeWrite[5] = {0xC6, 0x44, 0x2F, 0x2C, 0x01};
    uint8_t* write = reinterpret_cast<uint8_t*>(kAddrAppendAdvertTypeWrite);
    if (memcmp(write, typeWrite, sizeof typeWrite) != 0) {
        Log("The advert type write in the queue update does not match - those adverts not handled");
        return false;
    }
    if (!PatchCallSite(kAddrAppendAdvertCall, &g_appendAdvertTarget, reinterpret_cast<void*>(&AppendAdvertHook))) return false;
    DWORD old;
    VirtualProtect(write, sizeof typeWrite, PAGE_EXECUTE_READWRITE, &old);
    write[0] = 0xE8; // call AppendTypeThunk
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&AppendTypeThunk) - reinterpret_cast<uintptr_t>(write + 5));
    memcpy(write + 1, &rel, 4);
    VirtualProtect(write, sizeof typeWrite, old, &old);
    FlushInstructionCache(GetCurrentProcess(), write, sizeof typeWrite);
    return true;
}

bool PatchCallSite(uintptr_t site, uintptr_t* originalTarget, void* hook); // defined with the scroll code below

bool InstallServiceAdvertHook() {
    static const uint8_t typeWrite[7] = {0xC6, 0x85, 0xF0, 0x00, 0x00, 0x00, 0x01};
    uint8_t* write = reinterpret_cast<uint8_t*>(kAddrServiceAdvertTypeWrite);
    if (memcmp(write, typeWrite, sizeof typeWrite) != 0) {
        Log("The advert type write in Service does not match - lookahead adverts not handled");
        return false;
    }
    if (!PatchCallSite(kAddrServiceAdvertCall, &g_advertIndexTarget, reinterpret_cast<void*>(&ServiceAdvertHook))) return false;
    DWORD old;
    VirtualProtect(write, sizeof typeWrite, PAGE_EXECUTE_READWRITE, &old);
    memset(write, 0x90, sizeof typeWrite); // the hook sets the type itself
    VirtualProtect(write, sizeof typeWrite, old, &old);
    FlushInstructionCache(GetCurrentProcess(), write, sizeof typeWrite);
    return true;
}

using PickerFn = int(__fastcall*)(void*, void*);
PickerFn g_origPicker = nullptr;

// The game asks for the next user track: answer with one from the tuned-in station.
int __fastcall PickerHook(void* self, void* edx) {
    if (!g_game.empty()) {
        int v = g_virt;
        if (v < 1 || v > (int)g_game.size()) v = 1;
        const int index = PickTrack(g_game[v - 1]);
        if (index >= 0) {
            *reinterpret_cast<volatile int*>(kAddrUserTrackIndex) = index;
            LogDebug("Next track for '%s': #%d", g_game[v - 1].name.c_str(), index);
            return index;
        }
    }
    return g_origPicker(self, edx);
}

// ---- scroll cycle ---------------------------------------------------------------------------------
// The game scrolls like this: each key press adds +-1 to a "pending retunes" counter (manager + 0x6C), and about
// 1.5 s after the last press it tunes to  requested + pending  (wrapped over 1..13). It tunes to radio off (13)
// immediately, and it knows nothing about our extra entries. So we take over the key press itself: the game asks
// the pad twice per frame "next pressed?" / "previous pressed?" (call sites 0x4EB72A / 0x4EB753). We answer
// "no" and handle the press in our own cycle (see cycle.h), writing the counter so that the game's own logic
// and HUD then end up on the right game station.
using TickFn = void(__fastcall*)(void*, void*);
TickFn g_origTick = nullptr;
int g_cyclePos = -1; // where the player is in the extended cycle

constexpr uintptr_t kAddrManager = kAddrRadioTrackManager;
constexpr uintptr_t kAddrGameTimeMs = 0xB7CB84; // CTimer::m_snTimeInMilliseconds

// Watchdog: a track that has played well past its length without the game moving on means the station has stalled
// (silence until the player retunes). Log the game's state once, then retune the station the way a scroll away and
// back would.
constexpr uintptr_t kAddrStartRadio = 0x4EB3C0;
using StartRadioFn = void(__fastcall*)(void*, void*, int, int, int, int);

void DumpBytes(const char* label, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i += 32) {
        char line[128] = {0};
        for (size_t j = 0; j < 32 && i + j < n; ++j) sprintf_s(line + j * 3, sizeof line - j * 3, "%02X ", p[i + j]);
        Log("  %s+%03X: %s", label, (unsigned)i, line);
    }
}

// Trace mode ([Debug] Trace=1): logs every change of the game's playback state (queue, current track, stream) with
// the game time, so a normal track change can be compared with a stalled one. Words that change on every frame
// (counters) are muted after a while.
void Trace(uint8_t* m) {
    struct Region { const char* name; const uint8_t* base; };
    const Region regions[2] = {{"mgr+0C4", m + 0xC4}, {"stream", reinterpret_cast<const uint8_t*>(0xB5F8B8 + 0xE14)}};
    constexpr int kWords = 24; // 96 bytes each
    static uint32_t prev[2][kWords];
    static int changes[2][kWords];
    static bool muted[2][kWords], first = true;
    static unsigned windowStart = 0;
    const unsigned now = *reinterpret_cast<unsigned*>(kAddrGameTimeMs);
    if (now - windowStart > 2000) {
        windowStart = now;
        memset(changes, 0, sizeof changes);
    }
    for (int r = 0; r < 2; ++r) {
        const uint32_t* cur = reinterpret_cast<const uint32_t*>(regions[r].base);
        for (int i = 0; i < kWords; ++i) {
            if (first) { prev[r][i] = cur[i]; continue; }
            if (cur[i] == prev[r][i] || muted[r][i]) { prev[r][i] = cur[i]; continue; }
            if (++changes[r][i] > 30) {
                muted[r][i] = true;
                Log("T%u %s+%02X changes every frame, not traced any more", now, regions[r].name, i * 4);
            } else {
                Log("T%u %s+%02X: %08X -> %08X", now, regions[r].name, i * 4, (unsigned)prev[r][i], (unsigned)cur[i]);
            }
            prev[r][i] = cur[i];
        }
    }
    first = false;
}

// ---- one sample rate per station -------------------------------------------------------------------------------
// The game's audio channel cannot change sample rate between two tracks. It pads the rest of the buffer half with
// silence (0-3 s) and starts over, and that restart leaves the channel "started" with a stopped DirectSound buffer, so
// the radio stays silent (KickChannel repairs that). Every stall in our logs followed a sample rate change; the
// original game has the same problem with a mixed collection of tracks.
// So when the tracks of one station use more than one sample rate, the Ogg Vorbis decoder of the User Track Player is
// made to report the highest of them for every track, and the others are converted on the fly (cr::Resampler, cubic
// interpolation). A station whose tracks all share one rate is not touched at all. Only Ogg Vorbis is converted
// (mp3, wav and wma use other decoders); see docs/SAMPLE_RATES.md.
// Layout of CAEVorbisDecoder (checked against the exe): +4 data stream, +0x18 OggVorbis_File*,
// +0x1C initialised, +0x1D user track, +0x24 vorbis_info* (channels +4, rate +8); vtable[7] = GetStreamID (the
// track's number in the game's list).
std::vector<int> g_trackRate;   // per track of the game's list: Ogg Vorbis sample rate; 0 = unknown / not Ogg
std::vector<int> g_trackTarget; // per track: the rate the audio channel gets; 0 = leave the track alone
bool g_convertRates = true;

int RateOfFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return 0;
    uint8_t b[96] = {0};
    const size_t n = fread(b, 1, sizeof b, f);
    fclose(f);
    if (n >= 40 && memcmp(b, "OggS", 4) == 0) { // first packet: 01 "vorbis" version(4) channels(1) rate(4)
        const size_t at = 27 + b[26];
        if (at + 16 <= n && b[at] == 1 && memcmp(b + at + 1, "vorbis", 6) == 0)
            return (int)(b[at + 12] | (b[at + 13] << 8) | (b[at + 14] << 16) | (b[at + 15] << 24));
    }
    return 0;
}

// Length in seconds of an Ogg Vorbis file, from the position of its last page (0 when it cannot be told).
double OggSeconds(const std::string& path, int rate) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f || rate <= 0) { if (f) fclose(f); return 0; }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    const long tail = size < 65536 ? size : 65536;
    std::vector<uint8_t> b((size_t)tail);
    fseek(f, size - tail, SEEK_SET);
    const size_t got = fread(b.data(), 1, b.size(), f);
    fclose(f);
    for (long i = (long)got - 14; i >= 0; --i)
        if (memcmp(&b[(size_t)i], "OggS", 4) == 0) { // the last page: bytes 6-13 are the position in samples
            unsigned long long granule = 0;
            for (int k = 7; k >= 0; --k) granule = (granule << 8) | b[(size_t)i + 6 + k];
            return granule == ~0ull ? 0 : (double)granule / rate;
        }
    return 0;
}

// Looks at the sample rates of every station's tracks and decides which tracks have to be converted; also reports
// tracks the game will not play because they are shorter than 7 seconds.
void PlanSampleRates() {
    const std::vector<std::string> paths = ReadUserTrackList();
    g_trackRate.assign(paths.size(), 0);
    g_trackTarget.assign(paths.size(), 0);
    for (size_t i = 0; i < paths.size(); ++i) {
        if (paths[i].empty()) continue;
        g_trackRate[i] = RateOfFile(paths[i]);
        const double seconds = OggSeconds(paths[i], g_trackRate[i]);
        if (seconds > 0 && seconds < 7.0)
            Log("WARNING: %s is only %.1f s long - the game does not play user tracks shorter than 7 seconds", paths[i].c_str(), seconds);
    }
    for (const GameStation& gs : g_game) {
        std::map<int, int> count; // sample rate -> number of tracks
        std::vector<int> all;
        for (int kind = 0; kind < 4; ++kind)
            for (int index : gs.tracks[kind])
                if (index >= 0 && index < (int)g_trackRate.size() && g_trackRate[index] > 0) {
                    ++count[g_trackRate[index]];
                    all.push_back(index);
                }
        if (count.size() < 2) continue; // one rate (or none known): nothing to do
        const int target = count.rbegin()->first;
        std::string text;
        for (auto& c : count) text += (text.empty() ? "" : ", ") + std::to_string(c.first) + " Hz: " + std::to_string(c.second);
        if (g_convertRates) {
            Log("Station '%s' mixes sample rates (%s): converting to %d Hz while playing. Converting the files beforehand is better, see docs/SAMPLE_RATES.md", gs.name.c_str(), text.c_str(), target);
            for (int index : all) g_trackTarget[index] = target;
        } else {
            Log("Station '%s' mixes sample rates (%s) and ConvertSampleRates=0: the radio will pause at every change of rate", gs.name.c_str(), text.c_str());
        }
    }
}

constexpr uintptr_t kAddrVorbisFillBuffer = 0x502470;
constexpr uintptr_t kAddrVorbisGetSampleRate = 0x502560;

using VorbisFillFn = size_t(__fastcall*)(void*, void*, void*, size_t);
VorbisFillFn g_origVorbisFill = nullptr;

struct ResampleEntry {
    void *stream = nullptr, *handle = nullptr; // which decoder this belongs to (decoder addresses get reused)
    cr::Resampler resampler;
};
std::map<void*, ResampleEntry> g_resample;
CRITICAL_SECTION g_resampleLock;

inline int VorbisRate(const uint8_t* d) { return *reinterpret_cast<const int*>(*reinterpret_cast<uint8_t* const*>(d + 0x24) + 8); }

// The rate the audio channel gets for this decoder's track.
int ChannelRateFor(void* decoder, int sourceRate) {
    if (!g_convertRates) return sourceRate;
    using IntFn = int(__thiscall*)(void*);
    const int id = reinterpret_cast<IntFn>((*reinterpret_cast<void***>(decoder))[7])(decoder); // GetStreamID
    return id >= 0 && id < (int)g_trackTarget.size() && g_trackTarget[id] > 0 ? g_trackTarget[id] : sourceRate;
}

int __fastcall VorbisSampleRateHook(void* self, void*) { // replaces CAEVorbisDecoder::GetSampleRate
    const uint8_t* d = static_cast<const uint8_t*>(self);
    if (!d[0x1C]) return -1;
    const int rate = VorbisRate(d);
    return (d[0x1D] && rate > 0) ? ChannelRateFor(self, rate) : rate;
}

size_t __fastcall VorbisFillBufferHook(void* self, void* edx, void* dest, size_t size) {
    const uint8_t* d = static_cast<const uint8_t*>(self);
    if (!d[0x1C] || !d[0x1D]) return g_origVorbisFill(self, edx, dest, size);
    const int srcRate = VorbisRate(d);
    const int dstRate = srcRate > 0 ? ChannelRateFor(self, srcRate) : srcRate;
    if (srcRate <= 0 || dstRate == srcRate) return g_origVorbisFill(self, edx, dest, size);

    EnterCriticalSection(&g_resampleLock);
    ResampleEntry& e = g_resample[self];
    void* const stream = *reinterpret_cast<void* const*>(d + 4);
    void* const handle = *reinterpret_cast<void* const*>(d + 0x18);
    if (e.stream != stream || e.handle != handle) { // a new decoder on an old address
        e = ResampleEntry();
        e.stream = stream;
        e.handle = handle;
        if (g_resample.size() > 6) { // decoders that were thrown away mid-track leave their state behind
            for (auto it = g_resample.begin(); it != g_resample.end();) it = (it->first == self) ? std::next(it) : g_resample.erase(it);
        }
    }
    const size_t outFrames = size / 4;
    const size_t produced = e.resampler.Fill(srcRate, dstRate, static_cast<int16_t*>(dest), outFrames, [&](int16_t* out, size_t frames) {
        return g_origVorbisFill(self, edx, out, frames * 4) / 4; // the decoder delivers stereo 16-bit (mono is doubled)
    });
    if (e.resampler.Finished() && produced < outFrames) g_resample.erase(self); // the track is finished
    LeaveCriticalSection(&g_resampleLock);
    return produced * 4;
}

bool InstallResampler() {
    InitializeCriticalSection(&g_resampleLock);
    static const uint8_t fillBytes[5] = {0x51, 0x53, 0x55, 0x56, 0x57};            // push ecx,ebx,ebp,esi,edi
    static const uint8_t rateBytes[7] = {0x8A, 0x41, 0x1C, 0x84, 0xC0, 0x74, 0x07}; // mov al,[ecx+1Ch]; test al,al; je +7
    void* fill = HookFunction(kAddrVorbisFillBuffer, fillBytes, sizeof fillBytes, reinterpret_cast<void*>(&VorbisFillBufferHook));
    if (fill) g_origVorbisFill = reinterpret_cast<VorbisFillFn>(fill);
    // GetSampleRate is replaced completely (its first bytes hold a relative jump and cannot be copied)
    bool rate = false;
    if (memcmp(reinterpret_cast<void*>(kAddrVorbisGetSampleRate), rateBytes, sizeof rateBytes) == 0) {
        uint8_t* t = reinterpret_cast<uint8_t*>(kAddrVorbisGetSampleRate);
        DWORD old;
        VirtualProtect(t, 5, PAGE_EXECUTE_READWRITE, &old);
        t[0] = 0xE9;
        *reinterpret_cast<int32_t*>(t + 1) = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&VorbisSampleRateHook) - reinterpret_cast<uintptr_t>(t + 5));
        VirtualProtect(t, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), t, 5);
        rate = true;
    }
    Log("Sample rate conversion for mixed stations: decoder %s, reported rate %s", fill ? "ok" : "FAILED", rate ? "ok" : "FAILED");
    return fill && rate;
}

// What the game's audio channel is doing: its state, the decoder that is playing and the one prepared for next
// (their track id and sample rate). Layout of CAEStreamingChannel and CAEStreamingDecoder; the
// stream thread object sits at 0xB5F8B8 + 0xE14 and points to its channel at +8.
struct DecoderInfo { int id, rate, lengthMs; };

bool ReadDecoder(void* decoder, DecoderInfo& out) {
    out = {-99, -99, -99};
    if (!decoder) return false;
    __try {
        void** vtable = *reinterpret_cast<void***>(decoder);
        using IntFn = int(__thiscall*)(void*);
        out.lengthMs = reinterpret_cast<IntFn>(vtable[2])(decoder);
        out.rate = reinterpret_cast<IntFn>(vtable[5])(decoder);
        out.id = reinterpret_cast<IntFn>(vtable[7])(decoder);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void LogChannel(const char* why) {
    uint8_t* thread = reinterpret_cast<uint8_t*>(0xB5F8B8 + 0xE14);
    uint8_t* chan = *reinterpret_cast<uint8_t**>(thread + 8);
    if (!chan) return;
    const int state = *reinterpret_cast<int*>(chan + 0x60074);
    void* cur = *reinterpret_cast<void**>(chan + 0x6006C);
    void* next = *reinterpret_cast<void**>(chan + 0x60070);
    DecoderInfo c, n;
    const bool haveCur = ReadDecoder(cur, c), haveNext = ReadDecoder(next, n);
    Log("CHANNEL %s: state %d status %u loop %d silenced %d needSwitch %d needFinish %d shouldStop %d slot %d/%d | current %s id %d %d Hz %d ms | next %s id %d %d Hz %d ms | thread: track %d next %d needsService %d preparing %d",
        why, state, (unsigned)*reinterpret_cast<uint32_t*>(chan + 0x5C), chan[0x61], chan[0x63], chan[0x62], chan[0x64], chan[0x45], chan[0x66], chan[0x67], cur ? (haveCur ? "ok" : "bad") : "none", c.id, c.rate, c.lengthMs,
        next ? (haveNext ? "ok" : "bad") : "none", n.id, n.rate, n.lengthMs, *reinterpret_cast<int*>(thread + 0x18),
        *reinterpret_cast<int*>(thread + 0x20), thread[0x12], thread[0x15]);
}

// A change of sample rate between two tracks is something the game's channel is meant to handle (the rest of the
// buffer is filled with silence, the next decoder is taken over, the channel is stopped, re-prepared and started
// again: CAEStreamingChannel::Service at 0x4F2770). It does not work: afterwards the channel says "started" (-1)
// while the DirectSound buffer is not playing, so the radio is silent for the rest of that track and cannot change to
// the next one (a stopped buffer never finishes a track). Every stall in the logs followed a sample rate change. The
// likely cause is the fade-out that Stop(false) starts: it stops the buffer when it is done, which is after the
// channel has already been started again. We notice "started but not playing" and start the channel once more.
void KickChannel(uint8_t* m) {
    static DWORD badSince = 0;
    static unsigned kickLogAt = 0;
    uint8_t* thread = reinterpret_cast<uint8_t*>(0xB5F8B8 + 0xE14);
    uint8_t* chan = *reinterpret_cast<uint8_t**>(thread + 8);
    if (!chan) return;
    const unsigned now = *reinterpret_cast<unsigned*>(kAddrGameTimeMs);
    if (kickLogAt && now >= kickLogAt) {
        kickLogAt = 0;
        LogChannel("0.6 s after the kick");
    }
    const int state = *reinterpret_cast<int*>(chan + 0x60074);
    const uint32_t status = *reinterpret_cast<uint32_t*>(chan + 0x5C);
    const bool stuck = *reinterpret_cast<signed char*>(m + 0xE9) == cr::kUserTrackSlot && state == -1 && (status & 1) == 0;
    if (!stuck || MenuActive() || !VehicleRadioActive()) {
        badSince = 0;
        return;
    }
    if (!badSince) badSince = GetTickCount();
    if (GetTickCount() - badSince < 300) return;
    badSince = 0;
    LogChannel("KICK: started but not playing");
    using PlayFn = void(__fastcall*)(void*, void*, int, int, float);
    void** vtable = *reinterpret_cast<void***>(chan);
    reinterpret_cast<PlayFn>(vtable[5])(chan, nullptr, 0, 0, 1.0f); // CAEStreamingChannel::Play(0, 0, 1.0f)
    kickLogAt = now + 600;
}

int ChannelState() { // the audio channel's state, 0 when there is none
    uint8_t* chan = *reinterpret_cast<uint8_t**>(0xB5F8B8 + 0xE14 + 8);
    return chan ? *reinterpret_cast<int*>(chan + 0x60074) : 0;
}

void Watchdog(uint8_t* m) {
    static int lastTrack = -2, lastStation = -2, lastCounter = 0, samples = 0, changes = 0;
    static unsigned startMs = 0, changedMs = 0;
    static bool reported = false;
    static unsigned channelLogAt = 0, nextEndLog = 0;
    static int endLogs = 0;
    const int station = *reinterpret_cast<signed char*>(m + 0xE9);
    const int trackId = *reinterpret_cast<int*>(m + 0xD8);
    const int length = *reinterpret_cast<int*>(m + 0xE4);
    const int counter = *reinterpret_cast<int*>(m + 0xE0); // in all stall dumps: a small number that stopped moving
    const unsigned now = *reinterpret_cast<unsigned*>(kAddrGameTimeMs);
    if (trackId != lastTrack || station != lastStation) {
        lastTrack = trackId;
        lastStation = station;
        startMs = changedMs = now;
        lastCounter = counter;
        reported = false;
        samples = changes = 0;
        channelLogAt = g_trace ? now + 800 : 0;
        endLogs = 0;
        nextEndLog = 0;
        return;
    }
    if (channelLogAt && now >= channelLogAt) {
        channelLogAt = 0;
        LogChannel("after track start");
    }
    if (g_trace && station == cr::kUserTrackSlot && length > 0 && now - startMs + 500 >= (unsigned)length && endLogs < 14 && now >= nextEndLog) {
        LogChannel("end of track");
        ++endLogs;
        nextEndLog = now + 500;
    }
    if (counter != lastCounter) {
        if (samples < 3 && now - startMs > 5000) { ++samples; LogDebug("counter at +%u ms: %d (was %d)", now - startMs, counter, lastCounter); }
        lastCounter = counter;
        changedMs = now;
        ++changes;
    }
    if (station != cr::kUserTrackSlot || length <= 0 || reported || MenuActive() || !VehicleRadioActive()) return;
    const int chanState = ChannelState();
    // prepared (-2), stopping (-5), stopped (-6) or paused (-7) is what a pause or a lost window focus looks like:
    // the game restarts it by itself, nothing to repair
    if (chanState == -2 || chanState == -5 || chanState == -6 || chanState == -7) return;
    // -4 is the audio stream's "this track ran out and no next track was prepared": the game has no handler for it
    const bool frozen = (counter == -4 && now - changedMs > 1500) ||
                        (changes >= 5 && now - startMs > 6000 && now - changedMs > 4000);
    // A change of sample rate takes its time: the end of the track is padded with silence up to the next half of the
    // channel's buffer (2-3 s), and the change itself waits for the play cursor to cross into the following half.
    // Only when that is not going on and the track is long over, something is wrong.
    const uint8_t* chan = *reinterpret_cast<uint8_t**>(0xB5F8B8 + 0xE14 + 8);
    const bool switching = chan && (chan[0x62] || chan[0x63]);
    if (!frozen && now - startMs < (unsigned)length + (switching ? 25000u : 12000u)) return;
    reported = true;
    Log("STALL (%s): track %d (%d ms) after %u ms, counter %d stuck for %u ms", frozen ? "frozen" : "overrun", trackId, length, now - startMs, counter, now - changedMs);
    LogChannel("AT STALL");
    DumpBytes("manager", m, 0x200);
    DumpBytes("stream", reinterpret_cast<const uint8_t*>(0xB5F8B8 + 0xE14), 0x200);
    Log("STALL: retuning the station");
    *reinterpret_cast<int*>(m + 0x100 + cr::kUserTrackSlot * 0x2C + 0x10) = -1;
    *reinterpret_cast<signed char*>(m + 0xE9) = -1;
    *reinterpret_cast<int*>(m + 0x6C) = 0;
    *reinterpret_cast<int*>(m + 0x70) = 0;
    reinterpret_cast<StartRadioFn>(kAddrStartRadio)(m, nullptr, cr::kUserTrackSlot, *(m + 0xEA), *reinterpret_cast<int*>(m + 0xEC), 0);
}

void __fastcall TickHook(void* self, void* edx) {
    g_origTick(self, edx);
    if (g_cycle.empty()) return;
    uint8_t* m = static_cast<uint8_t*>(self);
    if (g_virt > 0 && !g_game.empty()) {
        if (g_trace) Trace(m);
        KickChannel(m);
        Watchdog(m);
    }
    const int pending = *reinterpret_cast<int*>(m + 0x6C);
    const int previous = *reinterpret_cast<int*>(m + 0x70);
    const int scriptRequest = *reinterpret_cast<int*>(m + 0x7C);
    // settled, or the game's own script changed the station: remember where we really are
    if ((pending == 0 && previous == 0) || scriptRequest >= 0 || g_cyclePos < 0)
        g_cyclePos = cr::IndexOf(g_cycle, *reinterpret_cast<signed char*>(m + 0xAD), g_virt);
}

// A key press: move one entry through the extended cycle and leave the game's counters as if it had done the
// move itself (same bookkeeping as the game's own press handling at 0x4EB733 / 0x4EB75C).
void OnScrollPress(int delta) {
    uint8_t* m = reinterpret_cast<uint8_t*>(kAddrManager);
    int& pending = *reinterpret_cast<int*>(m + 0x6C);
    int& previous = *reinterpret_cast<int*>(m + 0x70);
    const int requested = *reinterpret_cast<signed char*>(m + 0xAD);
    const int length = (int)g_cycle.size();
    if (pending == 0 && previous == 0) g_cyclePos = cr::IndexOf(g_cycle, requested, g_virt);
    if (g_cyclePos < 0) g_cyclePos = cr::IndexOf(g_cycle, requested, g_virt);
    if (g_cyclePos < 0) g_cyclePos = 0;

    const int from = g_cyclePos;
    g_cyclePos = cr::Step(g_cyclePos, delta, length);
    const cr::Elem e = g_cycle[g_cyclePos];
    if (e.game == cr::kUserTrackSlot && e.virt > 0) g_virt = e.virt;

    previous = pending;                        // the game does this on every press
    pending = cr::PendingFor(requested, e);    // requested + pending = the game station we want
    if (pending == 0 && previous == 0) previous = 1; // staying on slot 12: still make the game retune
    *reinterpret_cast<unsigned int*>(m + 0x50) = *reinterpret_cast<unsigned int*>(kAddrGameTimeMs); // last press time
    m[1] = 1; // show the station name
    m[6] = 1; // retune started
    LogDebug("Press %+d: position %d -> %d/%d (station %d%s%s), pending %d previous %d requested %d", delta, from, g_cyclePos,
        length, e.game, e.virt ? ", " : "", e.virt ? g_game[e.virt - 1].name.c_str() : "", pending, previous, requested);
}

constexpr uintptr_t kAddrPadNext = 0x5405B0; // CPad: "next station" pressed
constexpr uintptr_t kAddrPadPrev = 0x5405E0; // CPad: "previous station" pressed
using PadFn = bool(__fastcall*)(void*, void*);

uintptr_t g_padNextTarget = kAddrPadNext, g_padPrevTarget = kAddrPadPrev; // where the game's calls went before

bool __fastcall PadNextHook(void* pad, void* edx) {
    const bool pressed = reinterpret_cast<PadFn>(g_padNextTarget)(pad, edx);
    if (!pressed || g_cycle.empty()) return pressed;
    OnScrollPress(+1);
    return false; // handled: the game must not count it again
}

bool __fastcall PadPrevHook(void* pad, void* edx) {
    const bool pressed = reinterpret_cast<PadFn>(g_padPrevTarget)(pad, edx);
    if (!pressed || g_cycle.empty()) return pressed;
    OnScrollPress(-1);
    return false;
}

// Re-points a "call" instruction (E8 rel32) at `hook` and returns where it called before. Other mods (a gamepad
// plugin, for example) may already have redirected the call, so the target is not required to be the game's own
// function: whatever it called is what `hook` has to call in turn.
bool PatchCallSite(uintptr_t site, uintptr_t* originalTarget, void* hook) {
    uint8_t* p = reinterpret_cast<uint8_t*>(site);
    int32_t rel;
    memcpy(&rel, p + 1, 4);
    if (p[0] != 0xE8) {
        Log("Call site %08X is not a call (opcode %02X) - not patched", (unsigned)site, p[0]);
        return false;
    }
    *originalTarget = static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(p) + 5 + rel);
    Log("Call site %08X called %08X", (unsigned)site, (unsigned)*originalTarget);
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    const int32_t newRel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(hook) - reinterpret_cast<uintptr_t>(p + 5));
    memcpy(p + 1, &newRel, 4);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

bool IsDisabled(int slot) { return std::find(g_disabled.begin(), g_disabled.end(), slot) != g_disabled.end(); }

// Builds the scroll cycle: the game's own stations (unless disabled) at positions 1-11, then the virtual stations
// ordered by their HostStation number (the original User Track Player counts as 12), and radio off last.
void BuildCycle() {
    struct Item {
        int key, order;
        cr::Elem elem;
    };
    std::vector<Item> items;
    int order = 0;
    for (int r = 1; r <= 11; ++r)
        if (!IsDisabled(r)) items.push_back({r, order++, {r, 0}});
    for (size_t i = 0; i < g_game.size(); ++i) {
        const int key = g_game[i].userTracks ? 12 : g_game[i].position;
        if (g_game[i].userTracks && IsDisabled(12)) continue;
        items.push_back({key, order++, {cr::kUserTrackSlot, (int)i + 1}});
    }
    items.push_back({1 << 20, order++, {cr::kRadioOff, 0}});
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.key < b.key; });
    g_cycle.clear();
    std::string text;
    for (auto& it : items) {
        g_cycle.push_back(it.elem);
        text += (text.empty() ? "" : ", ") + (it.elem.virt ? g_game[it.elem.virt - 1].name : std::to_string(it.elem.game));
    }
    Log("Radio cycle: %s", text.c_str());
}

// ---- starting a station ------------------------------------------------------------------------
// CheckForStationRetune ends by calling StartRadio(station, ...) at 0x4EB876. StartRadio does nothing when the
// station is already the active one, and it resumes the station's saved position when it has one. Both are wrong
// for our stations: they all share game slot 12, so switching between them must be a real restart, and the saved
// position of one station must not be used for another. We wrap that one call to arrange this.
constexpr uintptr_t kAddrStartRadioCallSite = 0x4EB876;
int g_lastPlayedVirt = -1;

void __fastcall StartRadioHook(void* self, void* edx, int station, int bass, int arg, int zero) {
    uint8_t* m = static_cast<uint8_t*>(self);
    int& pending = *reinterpret_cast<int*>(m + 0x6C);
    int& previous = *reinterpret_cast<int*>(m + 0x70);
    signed char& active = *reinterpret_cast<signed char*>(m + 0xE9);
    const int st = (signed char)station;
    LogDebug("StartRadio(%d) active=%d requested=%d pending=%d previous=%d virt=%d lastPlayed=%d", st, (int)active,
        (int)*reinterpret_cast<signed char*>(m + 0xAD), pending, previous, (int)g_virt, g_lastPlayedVirt);

    if (st == cr::kUserTrackSlot && !g_game.empty()) {
        const bool switched = g_lastPlayedVirt != g_virt;
        if (switched) {
            // a different station than the one that played last: no resuming its saved position, and a real
            // restart even though the game's active station stays slot 12
            *reinterpret_cast<int*>(m + 0x100 + cr::kUserTrackSlot * 0x2C + 0x10) = -1;
            active = -1;
        }
        g_lastPlayedVirt = g_virt;
    }
    pending = 0;  // StartRadio ignores a restart while a retune is still pending
    previous = 0;
    reinterpret_cast<StartRadioFn>(kAddrStartRadio)(self, edx, station, bass, arg, zero);
}

bool InstallStartRadioHook() {
    uint8_t* site = reinterpret_cast<uint8_t*>(kAddrStartRadioCallSite);
    int32_t rel;
    memcpy(&rel, site + 1, 4);
    if (site[0] != 0xE9 && site[0] != 0xE8) {
        Log("StartRadio call site does not match (opcode %02X) - restarts not fixed", site[0]);
        return false;
    }
    if (static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(site) + 5 + rel) != kAddrStartRadio) {
        Log("StartRadio call site calls somewhere else - restarts not fixed");
        return false;
    }
    DWORD old;
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
    const int32_t newRel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&StartRadioHook) - reinterpret_cast<uintptr_t>(site + 5));
    memcpy(site + 1, &newRel, 4);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    Log("StartRadio hook installed");
    return true;
}

bool InstallScrollHook() {
    // push ecx ; push esi ; mov esi,ecx ; mov cl,[esi+0E9h]    (10 bytes)
    static const uint8_t tickBytes[10] = {0x51, 0x56, 0x8B, 0xF1, 0x8A, 0x8E, 0xE9, 0x00, 0x00, 0x00};
    void* tick = HookFunction(kAddrCheckForStationRetune, tickBytes, sizeof tickBytes, reinterpret_cast<void*>(&TickHook));
    if (tick) g_origTick = reinterpret_cast<TickFn>(tick);
    const bool pads = PatchCallSite(0x4EB72A, &g_padNextTarget, reinterpret_cast<void*>(&PadNextHook)) &&
                      PatchCallSite(0x4EB753, &g_padPrevTarget, reinterpret_cast<void*>(&PadPrevHook));
    Log("Scroll hook %s, key presses %s", tick ? "ok" : "FAILED", pads ? "taken over" : "FAILED");
    InstallStartRadioHook();
    return tick != nullptr && pads;
}

bool InstallGameStationHooks() {
    // [Debug] NoPicker=1 / NoAdHooks=1 leave out a group of hooks, to find out which one a problem comes from.
    const std::string ini = ModuleDir() + "ExtraStations.SA.ini";
    const bool noPicker = IniString("Debug", "NoPicker", "0", ini) == "1";
    const bool noAds = IniString("Debug", "NoAdHooks", "0", ini) == "1";
    void* picker = nullptr;
    if (noPicker) {
        Log("DEBUG: NoPicker=1 - the game picks the tracks itself, from the whole merged list");
        picker = reinterpret_cast<void*>(1);
    } else {
        // push esi ; mov esi,ecx ; mov cx,[esi+6]                  (7 bytes)
        static const uint8_t pickerBytes[7] = {0x56, 0x8B, 0xF1, 0x66, 0x8B, 0x4E, 0x06};
        picker = HookFunction(kAddrSelectUserTrackIndex, pickerBytes, sizeof pickerBytes, reinterpret_cast<void*>(&PickerHook));
        if (picker) g_origPicker = reinterpret_cast<PickerFn>(picker);
        Log("Track picker hook %s", picker ? "ok" : "FAILED");
    }
    if (noAds) {
        Log("DEBUG: NoAdHooks=1 - the game's own adverts are left alone");
    } else {
        const bool queue = PatchCallSite(kAddrChooseTracksCallSite, &g_chooseTracksTarget, reinterpret_cast<void*>(&ChooseTracksHook));
        Log("Queue hook (the game's own adverts) %s", queue ? "ok" : "FAILED");
        Log("Lookahead advert hook %s", InstallServiceAdvertHook() ? "ok" : "FAILED");
        Log("Queue-append advert hook %s", InstallAppendAdvertHook() ? "ok" : "FAILED");
    }
    return picker && InstallScrollHook();
}

// ---- probes on the game's track loader ([Debug] Probe=1, also on with Trace=1) ----------------------------------
// The game loads the next track on its own audio thread (CAEStreamThread::Service): LoadUserTrack(id) finds the
// file name in sa-ufiles.dat (GetTrackPath, relative to the current directory), opens it and creates a decoder; a
// decoder whose Initialise fails (for user tracks that includes any track shorter than 7 s) means "no next track"
// and the radio stops at the end of the current one. These probes log every one of those steps that fails.
using LoadTrackFn = void*(__fastcall*)(void*, void*, int);
using TrackPathFn = char*(__fastcall*)(void*, void*, int);
using DecoderInitFn = bool(__fastcall*)(void*, void*);
LoadTrackFn g_origLoadTrack = nullptr;
TrackPathFn g_origTrackPath = nullptr;
DecoderInitFn g_origVorbisInit = nullptr;

void* __fastcall LoadTrackProbe(void* self, void* edx, int id) {
    const DWORD t = GetTickCount();
    void* r = g_origLoadTrack(self, edx, id);
    int attempts = 1;
    // A failure can be momentary (the file name is read relative to the process's current directory, which other
    // threads change): try again a few times before the game gives up on the track.
    while (!r && attempts < 4) {
        Sleep(20);
        r = g_origLoadTrack(self, edx, id);
        ++attempts;
    }
    char cwd[MAX_PATH] = {0};
    GetCurrentDirectoryA(MAX_PATH, cwd);
    Log("LoadUserTrack(%d) -> %s after %d attempt(s), %u ms (thread %u, current dir %s)", id, r ? "ok" : "NULL", attempts,
        (unsigned)(GetTickCount() - t), (unsigned)GetCurrentThreadId(), cwd);
    return r;
}

char* __fastcall TrackPathProbe(void* self, void* edx, int id) {
    char* r = g_origTrackPath(self, edx, id);
    if (!r) {
        char cwd[MAX_PATH] = {0};
        GetCurrentDirectoryA(MAX_PATH, cwd);
        Log("GetTrackPath(%d) FAILED (current dir %s, count %d)", id, cwd, (int)*reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(self) + 6));
    }
    return r;
}

bool __fastcall VorbisInitProbe(void* self, void* edx) {
    const bool r = g_origVorbisInit(self, edx);
    if (!r) Log("Vorbis decoder Initialise FAILED (decoder %p)", self);
    return r;
}

bool InstallLoaderProbes() {
    static const uint8_t loadBytes[7] = {0x6A, 0xFF, 0x68, 0xC7, 0xC1, 0x83, 0x00};
    static const uint8_t pathBytes[8] = {0x56, 0x8B, 0x74, 0x24, 0x08, 0x57, 0x8B, 0xF9};
    static const uint8_t initBytes[8] = {0x53, 0x56, 0x57, 0x68, 0xD0, 0x02, 0x00, 0x00};
    void* a = HookFunction(0x4F35F0, loadBytes, sizeof loadBytes, reinterpret_cast<void*>(&LoadTrackProbe));
    void* b = HookFunction(0x4F3050, pathBytes, sizeof pathBytes, reinterpret_cast<void*>(&TrackPathProbe));
    void* c = HookFunction(0x5024D0, initBytes, sizeof initBytes, reinterpret_cast<void*>(&VorbisInitProbe));
    if (a) g_origLoadTrack = reinterpret_cast<LoadTrackFn>(a);
    if (b) g_origTrackPath = reinterpret_cast<TrackPathFn>(b);
    if (c) g_origVorbisInit = reinterpret_cast<DecoderInitFn>(c);
    Log("Loader probes: LoadUserTrack %s, GetTrackPath %s, Vorbis Initialise %s", a ? "ok" : "FAILED", b ? "ok" : "FAILED", c ? "ok" : "FAILED");
    return a && b && c;
}

DWORD WINAPI Worker(LPVOID) {
    // wait for the game to finish booting
    Sleep(5000);
    { // keep the log from growing for ever: a log over 1 MB is moved to ExtraStations.SA.log.old (one old copy is kept)
        const std::string log = ModuleDir() + "ExtraStations.SA.log";
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExA(log.c_str(), GetFileExInfoStandard, &info) && (info.nFileSizeHigh || info.nFileSizeLow > (1u << 20)))
            MoveFileExA(log.c_str(), (log + ".old").c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    Log("ExtraStations.SA %s loaded from %s", kVersion, ModuleDir().c_str());
    if (*(volatile unsigned short*)kAddrVersionCheck != 0x94BF) {
        Log("Unsupported game version (needs GTA SA 1.0 US) - plugin disabled");
        return 0;
    }

    LoadConfig();

    g_trace = IniString("Debug", "Trace", "0", ModuleDir() + "ExtraStations.SA.ini") == "1";
    if (g_trace) Log("Trace mode on: every change of the game's playback state is logged");
    // Built-in stations (1-12, 12 = User Track Player) that the user switched off:  [Radio] Disabled=2,5,12
    {
        std::string list = IniString("Radio", "Disabled", "", ModuleDir() + "ExtraStations.SA.ini");
        for (size_t i = 0; i < list.size();) {
            while (i < list.size() && !isdigit((unsigned char)list[i])) ++i;
            if (i >= list.size()) break;
            int n = atoi(list.c_str() + i);
            if (n >= 1 && n <= 12 && !IsDisabled(n)) g_disabled.push_back(n);
            while (i < list.size() && isdigit((unsigned char)list[i])) ++i;
        }
        if (!g_disabled.empty()) {
            std::string text;
            for (int n : g_disabled) text += (text.empty() ? "" : ", ") + std::to_string(n);
            Log("Disabled built-in stations: %s", text.c_str());
        }
    }
    if (g_game.empty() && g_disabled.empty()) {
        Log("Nothing to do: no usable station in ExtraStations.SA.ini and no built-in station disabled");
        return 0;
    }

    if (!g_game.empty()) {
        // The game has a single User Track Player. The player's own "User Tracks" folder is just another entry
        // of the cycle (the original User Track Player), then every station folder is scanned with the game's
        // own scanner and the results are merged into the game's one track list.
        bool addedUserTracks = false;
        if (!IsDisabled(12)) {
            const std::string userTracks = UserFilesDir() + "User Tracks";
            if (GetFileAttributesA(userTracks.c_str()) != INVALID_FILE_ATTRIBUTES) {
                GameStation utp;
                utp.name = "User Track Player";
                utp.folder = userTracks;
                utp.folderLower = Lower(userTracks);
                utp.userTracks = true;
                g_game.insert(g_game.begin(), std::move(utp));
                addedUserTracks = true;
            }
        }
        std::vector<std::string> folders;
        for (auto& gs : g_game) folders.push_back(gs.folder);
        if (BuildUserTrackList(folders)) {
            BuildGameStationLists();
            if (addedUserTracks && g_game.front().tracks[0].empty()) { // nothing in there: don't show an empty station
                Log("Your User Tracks folder has no tracks - the original User Track Player is not in the cycle");
                g_game.erase(g_game.begin());
            }
        } else {
            g_game.clear();
        }
    }
    // [Debug] Vanilla=1: no hooks at all. The merged track list is loaded, but the game scrolls and picks tracks by
    // itself (its own User Track Player plays everything). Used to find out whether a stall is the game's or ours.
    const bool vanillaTest = IniString("Debug", "Vanilla", "0", ModuleDir() + "ExtraStations.SA.ini") == "1";
    if (vanillaTest) {
        Log("VANILLA TEST MODE: no hooks installed, the game plays all %d tracks with its own User Track Player", (int)ReadUserTrackList().size());
    } else {
        if (!g_game.empty() || !g_disabled.empty()) {
            BuildCycle();
            if (!g_game.empty()) InstallGameStationHooks(); // picker + scroll
            else InstallScrollHook();                        // only the scroll (disabled stations)
        }
        InstallNameHook();
    }
    g_convertRates = IniString("Radio", "ConvertSampleRates", "1", ModuleDir() + "ExtraStations.SA.ini") != "0";
    if (!g_game.empty()) {
        PlanSampleRates();
        if (std::any_of(g_trackTarget.begin(), g_trackTarget.end(), [](int t) { return t > 0; })) InstallResampler();
    }
    if (IniString("Debug", "Probe", "0", ModuleDir() + "ExtraStations.SA.ini") == "1" || g_trace) InstallLoaderProbes();
    Log("Ready. Scroll the radio in a vehicle.");

    // Diagnostics: log station changes and what the game plays (kind of track), for bug reports.
    int lastId = -2;
    int lastTrackId = -1;
    while (g_run) {
        Sleep(50);
        const int id = ReadStationId();
        if (id != lastId) {
            LogDebug("Station id %d -> %d (vehicleRadio=%d, menu=%d)", lastId, id, (int)VehicleRadioActive(), (int)MenuActive());
            lastId = id;
        }
        const uintptr_t a = kAddrRadioTrackManager + 0xC4; // m_ActiveSettings
        if (vanillaTest) { // read-only stall detection (the hooks that do it are not installed)
            static int stallTrack = -2, lastCounter = 0;
            static DWORD changedAt = 0;
            const int counter = *(volatile int*)(kAddrRadioTrackManager + 0xE0);
            const int track = *(volatile int*)(a + 0x14);
            if (counter != lastCounter || track != stallTrack) { lastCounter = counter; changedAt = GetTickCount(); }
            if (id == cr::kUserTrackSlot && counter == -4 && GetTickCount() - changedAt > 1500 && stallTrack != track) {
                stallTrack = track;
                Log("STALL (vanilla game): track %d, counter -4 for %u ms", track, (unsigned)(GetTickCount() - changedAt));
            }
        }
        const int trackId = *(volatile int*)(a + 0x14);
        if (trackId != lastTrackId) {
            lastTrackId = trackId;
            LogDebug("Vanilla station %d: playing track id %d type %d, length %d ms, queued types [%d %d %d %d %d]", id, trackId,
                (int)*(volatile signed char*)(a + 0x31), *(volatile int*)(a + 0x20), (int)*(volatile signed char*)(a + 0x2C),
                (int)*(volatile signed char*)(a + 0x2D), (int)*(volatile signed char*)(a + 0x2E),
                (int)*(volatile signed char*)(a + 0x2F), (int)*(volatile signed char*)(a + 0x30));
        }
    }
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_run = false;
    }
    return TRUE;
}
