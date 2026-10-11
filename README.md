# ExtraStations.SA

Extra radio stations for **GTA San Andreas (v1.0 US)**. A station is a folder of audio files; you list it in an ini
file and it shows up in the radio scroll cycle next to the game's own stations.

- Any number of stations; each is its own entry in the scroll cycle (`HostStation` is its place in the cycle).
- The game's stations, the original User Track Player and "radio off" stay as they are; built-in stations can be left
  out (`Disabled=` in the ini).
- Songs, idents, ads and talk in separate sub folders. Per kind you can use your files, the game's own, a mix, or none.
- Stations whose files use different sample rates work (the game cannot do that on its own, see
  [docs/SAMPLE_RATES.md](docs/SAMPLE_RATES.md)).

The plugin ships **no music**: you bring your own files.

## It uses the game's own radio logic

The plugin does not replace the radio, it feeds it. The parts that matter are the game's own:

| What | How it works with this plugin |
|---|---|
| **Playing the music** | Your stations are played by the game's own **User Track Player**, so bass boost, EQ, volume and ducking behave like for every other station. |
| **What plays next** | The choice between a song, an ident, an ad or DJ talk follows the structure and the probabilities of the **game's own track-choice code**: mostly songs, now and then an ident, an ad or DJ talk. Your files take the place of the game's clips. Two limits: if a kind has no clip for a station (for example no idents of yours and none in the game for that slot), that step is skipped and the choice is made again, so songs come up a little more often; and the game's code does not show how often its DJ talk falls back to an advert, so that one rate (50 %) is an estimate. The game's intros and outros are not played yet. |
| **Selecting a station** | Your stations are normal entries in the radio scroll cycle. The game's stations keep their numbers: nothing is replaced, a station of yours between two built-in ones just makes the following ones come one step later when you scroll. A car's preselected radio is still the game's own station (K-DST stays K-DST). |
| **Your choice per kind** | For idents, ads and talk each station decides where the clips come from: your files, the game's own, both, or none (`Auto` uses your files if the folder has any, otherwise the game's). |

## Requirements

- GTA San Andreas **1.0 US** (the plugin checks the executable and does nothing on another version).
- An **ASI loader** that loads `.asi` files from `scripts\` (for example the Ultimate ASI Loader). It is not included.

## Install

Take the zip of the newest version from the **Releases** page. `SHA256SUMS.txt` next to it lets you check the download.
ASI plugins are sometimes flagged by antivirus programs (they patch another program's memory); the whole source is in
this repository, and `build.bat` builds the same file yourself.

1. **Unpack the zip into your GTA San Andreas folder.** It puts `ExtraStations.SA.asi` and `ExtraStations.SA.ini` into `scripts\`
   and creates a ready-made, still empty station folder `scripts\ExtraStations.SA\MyStation\` with the sub folders `songs`,
   `idents`, `ads` and `talk`.
2. **Copy your music into `scripts\ExtraStations.SA\MyStation\songs\`** (`.ogg .mp3 .wav .wma .wmv .aac .m4a`; convert FLAC to
   `.ogg` first).
3. Start the game and scroll the radio in a vehicle. Folders are read when the game starts, so restart after adding files.

The station "My Station" is already set up in `ExtraStations.SA.ini`, so nothing has to be edited for the first station. For another
station, copy the `MyStation` folder, give it a new name and add a `[Station2]` section to the ini (see "Examples").

To uninstall, delete the `.asi`, `.ini` and `.log`. The plugin also changes the game's user track list, see "Good to know".

## Settings

Everything is in `ExtraStations.SA.ini`, which explains every setting. The station settings:

| Key | Default | |
|---|---|---|
| `Name` | | Shown on screen when you tune in |
| `Folder` | | Station folder, relative to the ini or the GTA SA folder; it can be anywhere |
| `HostStation` | 13 | Its place in the radio cycle: 13 = after all the game's stations, 3 = right after K-DST, 0 = first |
| `Shuffle` | 1 | 1 = random order (each track once before any repeats), 0 = alphabetical |
| `Idents` / `Ads` / `Talk` | `Auto` | Where those clips come from: `Auto`, `Custom` (only yours), `Vanilla` (the game's), `Mixed`, `Off` |

And two general ones in `[Radio]`: `Disabled=` (built-in stations to leave out of the cycle) and `ConvertSampleRates=`.

The radio cycle in the game's order: 1 Playback FM, 2 K-Rose, 3 K-DST, 4 Bounce FM, 5 SF-UR, 6 Radio Los Santos, 7 Radio X,
8 CSR 103.9, 9 K-Jah West, 10 Master Sounds 98.3, 11 WCTR, 12 User Track Player (only there if your `User Tracks` folder has
music in it), then your own stations, then "radio off".

## Station folders: what goes where

```
scripts\
  ExtraStations.SA.asi
  ExtraStations.SA.ini
  ExtraStations.SA\            all your station folders live here (any other place works too, see `Folder=`)
    MyStation\                 one station
      songs\                   the music
      idents\                  station ids, jingles
      ads\                     adverts
      talk\                    DJ talk, news, traffic, weather
```

| Folder | Needed? | Can it be empty or missing? |
|---|---|---|
| `songs\` | **Yes**, the only one a station needs | **No.** A station without any song is skipped and the log says so. Files directly in the station folder count as songs too. |
| `idents\`, `ads\`, `talk\` | Optional | **Yes**, missing or empty is fine. With the default `Auto`, the game's own clips are used where it has some for this slot (only generic adverts, no idents or DJ talk), otherwise that kind is left out. |

Folder names are not case-sensitive. A folder with any other name is ignored and the log warns about it. The names `intros`, `outros`
and `shows` are reserved for later versions. Songs shorter than 7 seconds are not played by the game.

If a station has only songs, the game's generic adverts are used (`Ads=Off` switches them off).

## Examples

**The simplest station: just music**

```
scripts\ExtraStations.SA\MyStation\songs\track01.ogg
scripts\ExtraStations.SA\MyStation\songs\track02.ogg
```

```ini
[Station1]
Name=My Station
Folder=ExtraStations.SA\MyStation
```

It is the last station before "radio off" (`HostStation` 13) and the game's own adverts play between songs.

**A station with its own idents, ads and talk**

```
scripts\ExtraStations.SA\Westside\songs\...
scripts\ExtraStations.SA\Westside\idents\...
scripts\ExtraStations.SA\Westside\ads\...
scripts\ExtraStations.SA\Westside\talk\...
```

```ini
[StationWestside]
Name=Westside Mix
HostStation=14
Folder=ExtraStations.SA\Westside
```

Which kind comes next is still decided by the game's own probabilities; the clips come from your folders. Add `Ads=Off`
to never play adverts on this station.

**A station between two of the game's**

```ini
[StationRetro]
Name=Retro Hits
HostStation=3
Folder=ExtraStations.SA\Retro
```

It sits right after K-DST in the scroll cycle; Bounce FM and the following stations come one step later.

**Leave built-in stations out**

```ini
[Radio]
Disabled=2,5,12
```

K-Rose (2), SF-UR (5) and the User Track Player (12) no longer appear in the cycle.

## Good to know

- **Log:** `scripts\ExtraStations.SA.log`. It lists what was found in each station folder and warns about problems. If the
  game is not run as administrator, Windows may put the file in
  `%LocalAppData%\VirtualStore\Program Files (x86)\...\scripts\`.
- **Sample rates:** give all files of a station the same rate (44100 Hz is a good choice). Mixed Ogg files are converted
  while playing, which works, but converting beforehand is better: `tools\convert-sample-rate.ps1`
  (needs ffmpeg). Details in [docs/SAMPLE_RATES.md](docs/SAMPLE_RATES.md).
- **Tracks shorter than 7 seconds** are not played by the game.
- The original User Track Player entry only appears in the cycle if your `User Tracks` folder has music in it.
- The plugin writes the game's user track list (`sa-utrax.dat`, `sa-ufiles.dat` in
  `Documents\GTA San Andreas User Files`) with the tracks of all stations merged. The first time, it keeps a copy of
  the existing files as `*.before-extrastations`.
- Ideas for later versions are collected in [docs/FUTURE_FEATURES.md](docs/FUTURE_FEATURES.md). If something goes wrong in the
  game, [docs/DEBUGGING.md](docs/DEBUGGING.md) explains the log and the debug switches.

## Build

The source of every release is this repository at the tag of that release (`v1.0.0` ...). Pushes to `main` and every tag are
built by GitHub Actions (`.github/workflows/build.yml`): unit tests, the 32-bit plugin, and for a tag a release with the zip.

Needs Visual Studio with the C++ workload (the game is 32-bit, the plugin is built as 32-bit):

```
build.bat                  builds out\ExtraStations.SA.asi and copies the ini next to it
powershell tools\test.ps1  runs the unit tests
```

The unit tests (`tests\*.cpp`) are plain console programs without dependencies; compile one with
`cl /EHsc /std:c++17 tests\resample_test.cpp`.

## License

Copyright (C) 2026 Sebastian Merz. Free software under the **GNU General Public License, version 3 or (at your
option) any later version** - see [LICENSE](LICENSE). The plugin comes with no warranty. It is a fan project and not
affiliated with Rockstar Games or Take-Two Interactive. It contains no game assets or game code.
