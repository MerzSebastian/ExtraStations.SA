# ExtraStations.SA – ideas and future features

Nothing in this file is implemented unless it says so. It is a parking lot for ideas, plus the game internals we
have already worked out, so they don't have to be rediscovered. Addresses are for GTA San Andreas **1.0 US**.

## Where we are today

- Any number of extra stations, inserted into the scroll cycle, played by the game's own
  User Track Player (so EQ, bass boost, volume and ducking apply).
- Cycle is one ordered list: built-in stations can be disabled, custom ones sit at any `HostStation` position.
- Station name on screen, own `songs / idents / ads / talk` folders, shuffle decks (no repeats), original game
  probabilities for what comes next (`src/scheduler.h`).
- The game's own adverts on the User Track Player slot are replaced according to `Ads=` (two hook points).
- Every station is played by the game's own player. The earlier `Engine=Plugin` built-in player (miniaudio) was removed before release.

---

## The folder-name contract (do not change)

Station packs depend on these names, so they are fixed. Code: `src/folders.h`, test: `tests/folders_test.cpp`.

| Canonical | Aliases (all case-insensitive) | Game track type | Used today |
|---|---|---|---|
| `songs` | `music` | 4 music | yes (files directly in the station folder count as songs) |
| `idents` | `ident`, `jingles`, `jingle` | 0 ident | yes |
| `ads` | `ad`, `adverts`, `advert` | 1 advert | yes |
| `talk` | `dj`, `banter`, `news`, `weather`, `traffic` | 2 DJ banter | yes |
| `intros` | | 3 intro | reserved |
| `outros` | | 5 outro | reserved |
| `shows` | | – | reserved |

Rules:
- **Names never change meaning and aliases keep working forever.** New behaviour gets new names.
- **Never numbers as folder names:** the game's type numbers are internal and a pack author can't read them.
- **`talk` stays the general speech bucket.** If news / weather / traffic ever get their own behaviour (idea 4),
  they remain valid folder names; until then they count as `talk`.
- **Reserved names are ignored with a log line**, so packs made today keep working when the feature arrives.
- **Unknown folders are ignored with a warning** in the log, so typos get noticed.
- Also reserved for the future, not used yet: `station.ini` inside a station folder (a pack's own settings), and a
  `<song>.ini` next to a song (per-song intro / outro / weight).

## Ideas

### 1. Announcements: intros, outros, "up next"

The real game queues an **intro** (type 3) before a song and an **outro** (type 5) after it. They are recorded per
song ("…and now, Ice Cube"). Some fan-made station packs have none (their files are songs, jingles, ads and spots only).

- `intros\` and `outros\` folders. A clip whose file name matches a song (`songs\Cueball - Sorry.ogg` →
  `intros\Cueball - Sorry.ogg`) plays right before / after exactly that song.
- Generic lines without a song name ("…und weiter geht's") for songs that have no matching clip.
- Chance per song following the original rate (about 7 % of songs have an intro), adjustable per station.
- Optional "talk over the start of the song": play the voice while the song is ducked, instead of before it.
- A small per-song side file for the same thing: `songs\Cueball - Sorry.ini` with `Intro=`, `Outro=`, `Weight=`,
  `Ads=Off` …
- Needs clips someone has to make; ship an example pack to test with.

### 2. Borrow the game's own clips from another station

Make a "modern Radio Los Santos": your songs, but Julio G's voice lines and the station's idents.

- Keys: `IdentsFrom=6`, `TalkFrom=6`, `AdsFrom=6`, or one `Voices=6` for all three.
  (`scheduler.h` already understands these; the picker ignores them for now.)
- Idea: call the game's `ChooseTracksForStation` for the *donor* station (6) and keep its idents, DJ banter and
  ads; swap only the music tracks for ours. Gives the exact original probabilities for free.
- Drop the donor's intros/outros: they are recorded for the donor's songs and would sound wrong.
- Open question: does the game play another station's clips correctly while it is tuned to slot 12? The queue
  holds global track ids, so it should; needs a test.
- Not every station has talk or idents (Playback FM has very little).

### 3. Extra songs on the game's own stations

"More Snoop on Radio Los Santos" without replacing the station.

- Per station: `[Extra...]` with `Station=6`, `Folder=…`.
- Hook right after the game fills its queue (same hook as the advert swap) and, for each queued **music** track
  (type 4), roll a chance to play one of ours instead (as a user track, type 7). DJ, ads and idents stay untouched.
- Chance options: fair share (`yours / (yours + the station's songs)`) or a fixed `Share=30`; never two of ours in
  a row (`MinTracksBetween`); shuffle deck so nothing repeats before everything has played.
- Table of vanilla song counts per station (Radio Los Santos has 16 according to the wiki; verify the rest).
- Drop the intro/outro that belonged to the replaced song.

### 4. Time- and situation-aware content

- Shows by time of day: a morning show, a night show; different `songs` sets per time window.
- Weather and traffic spots tied to the real in-game weather / area / time.
- News tied to events: wanted level, missions done, gang wars, the player's area (LS / SF / LV).
- Ads by region or time; seasonal packs.
- Per-vehicle default station (police car, low-rider, boat …).

### 4b. Which station do other cars use?

Today we don't control this at all and haven't looked at it, so it needs to be checked first (see the open
questions). Ideas once we know how the game assigns a radio to a car:

- **Random traffic cars use the custom stations or not**, per station: `AmbientCars=1` / `0`, with a weight
  (`AmbientWeight=10` means "10 % as likely as the others"), so a station can be only for the player.
- Which cars: by vehicle model or class (low-riders, taxis, trucks, boats, police), by area (Los Santos, San
  Fierro, Las Venturas, countryside) or by time of day. Example: `AmbientModels=lowrider,tahoma`.
- **A stolen car's station:** the player's choice, the car's own default, or a fixed station per model.
- **Station remembered per vehicle** (and per saved car in a garage), instead of only "the last one".
- Special vehicles that have a fixed radio in the real game (police, ambulance, fire truck …) should stay as they
  are; make that an option, not an accident.
- If an ambient car were assigned the User Track Player slot (12), every one of our virtual stations shares that
  slot, so such a car would play whichever custom station is tuned in right now. That needs a rule (probably: pick
  a virtual station per car, or don't assign slot 12 to ambient cars).
- Hearing other cars' radios from outside (distance fade, doors open) is listed under "Bigger ideas" and would
  build on this.

### 5. Scroll and on-screen UX

- Show the name of the station you are scrolling **to** while you press (today the HUD follows the game's own
  list, which is correct at rest but can lag during fast scrolling).
- "Now playing: Artist – Title" on screen, from file tags (Vorbis comments / ID3) or the file name.
- Hotkeys: jump to a favourite station, next / previous *track*, pause.
- Custom entries in the Options → Audio → Radio station list of the menu.
- Station logos for the pause-menu list.

### 6. Station packs (sharing)

- A self-contained folder per station with its own `station.ini` (name, position, rules), so a pack can be
  dropped in and works without touching the main INI. Auto-discovered on start.
- Pack metadata: author, license, version, credits shown in the log.
- Station packs have their own licenses; ship stations without copyrighted music and let users bring their own.

### 7. Playback quality

- Loudness normalisation (per-track gain from ReplayGain tags or measured at load) so tracks don't jump in volume.
- Per-station `Volume=` trim.
- Crossfade between tracks. The game has a single streaming channel with one current and one next decoder and cannot
  mix or overlap two tracks (see `SAMPLE_RATES.md`). Because the plugin already sits between the Ogg decoder and the
  channel (`VorbisFillBufferHook`), it could do the mixing itself: open the next file, blend the end of one with the
  start of the other, hand the channel one stream. The same mechanism would carry "up next" announcements spoken over
  the start of a song (idea 1).
- Allow tracks shorter than 7 s: the game refuses user tracks under 7000 ms (a compare at `0x502538` in
  `CAEVorbisDecoder::Initialise`). Short idents and jingles are common, so patch the limit (and check that switching
  into very short tracks is stable).
- Faster, still pausing change between sample rates for formats we cannot convert (mp3, wma): switch at the exact end of
  the audio instead of at the next buffer half. The Ogg case is solved by conversion, see `SAMPLE_RATES.md`.
- Make the game's "continues while you're away" tracking meaningful for custom stations (today a switch to a
  different virtual station is a deliberate fresh start).

### 8. Content control

- `.m3u` / `.m3u8` playlists instead of folders.
- Per-track weights, banned tracks, "play this track every hour".
- Several folders per station; sub folders as "shows".
- INI validator: report unknown keys and missing folders at start, with line numbers.

### 9. Compatibility and robustness

- Other game versions (Steam / 1.01 / DE is different code): detect the exe and use a table of addresses;
  fall back to doing nothing if unknown.
- Coexist with other mods that hook the same code (already done for the pad call sites: we chain to whatever
  they point at; GInputSA is one).
- Reload the INI in game with a hotkey; no restart for adding a song.
- Settings for log level; keep the log size bounded.
- Remember the last station per save game; restore it on load.

### 10. Bigger ideas

- Radio from other vehicles: nearby cars playing their own music (distance fade).
- Radio in interiors / shops / bars.
- A tiny plugin API so other mods can register stations or react to "track started".
- "Replace a real station" (K-DST becomes Snoop FM) comes back through idea 3 below, played by the game's own player.

---

## Game internals we found (1.0 US)

### Radio manager (`CAERadioTrackManager`, global at `0x8CB6F8`)

| Offset | Meaning |
|---|---|
| `+0x50` | time of the last scroll key press |
| `+0x68` | radio status (7 is the state `StartRadio` needs for a same-station restart) |
| `+0x6C` | **pending retunes**: scroll steps not yet applied; target = `requested + pending` |
| `+0x70` | previous pending value (the game copies the old one here on each press) |
| `+0x78` / `+0x7C` | menu / script retune request (`-1` when none) |
| `+0x88` | requested settings: `+0x88` track ids ×5, `+0xAD` station, `+0xB4` types ×5, `+0xBB` music indices ×5 |
| `+0xC4` | active settings: `+0xC4` ids ×5, `+0xD8` playing id, `+0xE9` station, `+0xF0` types ×5, `+0xF5` playing type |
| `+0x100` | station tracking list, `0x2C` bytes per station (`+0x10` = time stopped; negative = don't resume) |

### Track types

`0` ident, `1` advert, `2` DJ banter, `3` intro, `4` music, `5` outro, `6` none, `7` user track.

### Functions

| Address | What | Notes |
|---|---|---|
| `0x4EB660` | `CheckForStationRetune` | per frame; reads the pad, applies the scroll after ~1.5 s |
| `0x4EB72A` / `0x4EB753` | call sites: pad "next" / "previous" | other mods (GInputSA) redirect these; chain to the current target |
| `0x4EB3C0` | `StartRadio` | call site `0x4EB876`; ignores a restart of the active station; resumes saved position if any |
| `0x4EB180` | `ChooseTracksForStation` | single caller at `0x4EB4F6`; fills the queue |
| `0x4EAC30` | `TrackRadioStation` | the "resume" path used by `StartRadio` |
| `0x4EB9A0` | `Service` | per frame; has its own User-Track-Player lookahead and asks `ChooseAdvertIndex(12)` at `0x4EBBA5` (type write at `0x4EBBB5`) |
| `0x4E9570` | `ChooseAdvertIndex` | adverts come from one shared pool (`audio\streams\ADVERTS`) |
| `0x4E9E10` | `GetRadioStationName` | returns an 8-bit GXT string (`char*`), hooked for our names |
| `0x4E9E50` | `DisplayRadioStationName` | draws the name for ~2.5 s |
| `0x4F3250` | `SelectUserTrackIndex` | hooked: answers with a track of the tuned-in virtual station |
| `0x4F4BA0` / `0x4F2FD0` | `ScanUserTracks` / `ReadUserTracks` | scan only starts from state 0 (`+0x18` of `0xB6B970`) |
| `0x4F4A8D`, `0x4F4AA6`, `0x4F4AB2` | operands in the scan thread | patched to change the folder that is scanned |

### Behaviours worth remembering

- The audio channel (`CAEStreamingChannel`, via the stream thread object at `0xB5F8B8 + 0xE14`, channel pointer at +8)
  has one playback frequency. A change of sample rate between two tracks makes it pad the buffer half with silence and
  restart (`Service` at `0x4F2770`), and the restart leaves it "started" (state -1) with a stopped DirectSound buffer.
  Layout and the repair: see `SAMPLE_RATES.md`, `KickChannel` and `CHANNEL` lines in the trace.
- Channel state values: -1 started, -2 prepared, -3 playing, -4 finished (no next decoder), -5 stopping, -6 stopped,
  -7 paused. Manager playback position at `manager + 0xE0` (negative values are the channel's state), length at `+0xE4`.

- The scroll cycle is `1..12` and `13` (radio off), wrapped with the constant 13. The user track slot (12) is
  skipped when the track count (`word` at `0xB6B976`) is 0.
- Whenever `requested + pending` is **13** the game commits **immediately**; for everything else it waits ~1.5 s
  after the last press. That is why the scroll is handled at the key press, not after the game's arithmetic.
- `StartRadio` returns without doing anything if the station is the one already active, and resumes the station's
  saved queue if its stop time is not negative. Both are worked around in `StartRadioHook`.
- For slot 12 the game queues two user tracks and about 17 % of the time one of its own adverts, both in
  `ChooseTracksForStation` and again in `Service`'s lookahead.
- User track list files (in `Documents\GTA San Andreas User Files`): `sa-utrax.dat` = `{offset, length, type}`
  records of 12 bytes, `sa-ufiles.dat` = the full paths back to back. We merge several scans into one list.
- Scanning a folder with a single file lists that file twice (scanner quirk; harmless).
- Game files under `Program Files` are written to `AppData\Local\VirtualStore` when the game isn't elevated
  (this is where `ExtraStations.SA.log` ends up).

### Open questions

- **How does the game pick a radio station for a car** it spawns in traffic, and for a car the player steals?
  Where is it stored (per vehicle? in the vehicle audio entity?), is it chosen from stations 1..11 only, and can
  it ever be 12? We haven't looked at this yet. (needed for idea 4b)
- Does the game play another station's clips while on slot 12? (needed for idea 2)
- Exact rate at which the game's "DJ banter" step fails and an advert is queued instead (the scheduler uses an
  adjustable estimate, `banterFailChance = 0.5`).
- Minimum length of a user track: Media Foundation rejects MP3-type files under 7 s; Ogg files seem fine.
- Whether queued items can be reordered safely after the game has started playing them.
