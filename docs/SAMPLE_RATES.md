# Sample rates: a limitation of the game, and what the plugin does about it

**Short version: give every track of a station the same sample rate (44100 Hz is a good choice). The plugin can
convert on the fly when a station mixes rates, but converting the files beforehand is the preferred way.**

## The limitation

GTA San Andreas plays the radio (and the User Track Player, which our stations use) through **one** DirectSound
streaming channel. The channel has one playback frequency, set per track. It plays one decoder and holds a second
decoder ready for the next track. When the first one runs out, the channel switches over without a gap, **but only if
both tracks have the same sample rate.**

If the rates differ (a 32 kHz track followed by a 44.1 kHz one), this is what the game does
(`CAEStreamingChannel::FillBuffer` / `Service`, from the disassembly):

1. The rest of the half of the channel's buffer in which the old track ends is filled with **silence**.
2. When the play cursor reaches the next half, the channel is stopped, prepared with the new decoder and started again.
   That is a pause of **0 to about 3 seconds**, depending on where in the buffer half the track happened to end.
3. The restart is broken: the channel ends up "started" while its DirectSound buffer is **not playing** (the fade-out
   started by the stop switches the buffer off after the channel was already started again - this is our reading of
   the logs, not confirmed in the code). The result is **silence for the rest of that track, and the radio never
   moves on to the next one**, until the player retunes.

This is not specific to this plugin. The original game has the same problem with a mixed collection of user tracks;
it is just rare there because most music files are 44.1 kHz. It is common for us, because station packs often mix
sources (one fan-made station pack had 34 songs at 32 kHz and 9 at 44.1 kHz).

How we found out: tracing the game's state while a stress station switched between files of different rates. Every
stall in the logs (more than twenty) followed a sample rate change, and no change between equal rates ever stalled.

The game cannot mix or overlap two tracks either (no crossfade): every item of the queue - song, ident, ad, DJ talk -
is simply played after the previous one.

## What the plugin does

At startup it reads the sample rate of every **Ogg Vorbis** file of every station (`PlanSampleRates`, logged):

| Situation | What happens |
|---|---|
| All tracks of a station have the same rate (32 kHz, 44.1 kHz, 22 kHz ... any) | **Nothing.** The game plays them natively. No conversion, no cost; the conversion code is not even installed. |
| A station mixes rates | Every track of that station is converted **up to the highest rate in the station**, while playing. The log says `Station 'X' mixes sample rates (...): converting to ...`. |
| Other formats (mp3, wav, wma, m4a ...) | Not converted (they use other decoders). A change of rate between those still pauses, and the repair below applies. |
| `ConvertSampleRates=0` in the ini | Conversion off. Mixed stations pause at every change of rate. |

Details:

- Each station is looked at on its own. Switching between stations restarts the channel, so rates may differ between
  stations. The player's own `User Tracks` folder is a station too and is treated the same way.
- Always converts **up** (to the highest rate present), never down: nothing is cut off. A 22 kHz and a 44.1 kHz file
  become 44.1 kHz; two 32 kHz files with a 48 kHz one become 48 kHz.
- The converter (`src/resample.h`) is a windowed sinc interpolator (48 taps, Kaiser window, 1024 sub-sample phases).
  The Ogg decoder of the game is made to report the target rate and to deliver the converted audio; the game's channel
  sees only one rate.
- **Safety net** (`KickChannel`): if the channel is ever found "started" with a stopped buffer for 300 ms - for example
  after a change between two mp3 files of different rates - the plugin starts the channel again, so the radio goes on
  instead of staying silent. The pause remains.
- **Watchdog**: a track that has run long past its end without the game moving on makes the plugin retune the station.
  It leaves the game alone while it is paused, switching rate, or out of focus.

## Quality of the conversion

Measured offline with the plugin's own converter (`tests/resample_test.cpp`, `tools/resample_tool.cpp`):

- Level of test tones after 32 kHz -> 44.1 kHz: 0.00 dB up to 14 kHz, -1.8 dB at 15 kHz (the 32 kHz source ends at 16 kHz).
  22.05 kHz -> 44.1 kHz: flat to 8 kHz, -0.3 dB at 10 kHz.
- Compared with ffmpeg's `soxr` resampler (very high quality) on two real songs: the difference is 46 and 50 dB below
  the music. On a sweep up to 15.5 kHz it is -27 dB, concentrated in the last kilohertz below the source's own limit.
- Listening files to compare: run the commands under "Making comparison files" below.

Music that was encoded at 32 kHz is lowpassed by its encoder at about 14-16 kHz anyway, so the part where the converter
is not perfect is the part where there is little to hear.

## Pre-converting the files (preferred) or converting while playing?

**Preferred: all files of a station at one rate.** Nothing runs at playback, nothing can go wrong, and the audio is
exactly what you made.

How to get there matters for quality:

- **Best:** convert from your lossless originals and encode **once** to the final rate and format.
- **Second best:** convert the Ogg files you have. Ogg -> resample -> Ogg is a second lossy encoding. With a good
  quality setting (`-q:a 6` or higher) the extra loss is small, but it is not zero. (Converting into wav avoids the
  second encoding, at 10x the size.)
- **Converting while playing** (the plugin) has no second encoding, because it works on the decoded audio. Its
  interpolator is a little less steep than soxr at the very top (see above). For music this is practically
  inaudible; whether pre-converting is "even better" is a matter of what the source is: from a lossless source, yes
  (slightly); from already compressed Ogg files, the plugin's live conversion is the cleaner path.

Choosing the rate: **44100 Hz** if you convert upward anyway; the rate of the majority if you want to touch few
files (downsampling the others cuts everything above half that rate: 32 kHz keeps up to 16 kHz, plenty for radio).

### Converting a folder

```powershell
# needs ffmpeg and ffprobe in the PATH; writes the result to "<folder>-44100", the originals stay untouched
.\tools\convert-sample-rate.ps1 -Folder "C:\...\scripts\ExtraStations.SA\MyStation"
.\tools\convert-sample-rate.ps1 -Folder .\MyStation -Rate 32000 -Quality 7
```

Or one file by hand:

```
ffmpeg -i in.ogg -af aresample=resampler=soxr:precision=28 -ar 44100 -c:a libvorbis -q:a 6 out.ogg
```

To see the rates in a folder: `ffprobe -v error -select_streams a:0 -show_entries stream=sample_rate -of csv=p=0 file.ogg`.

### Making comparison files

```
tools\resample_tool.exe                      (built from tools/resample_tool.cpp)
ffmpeg -i song.ogg -ac 2 -ar 32000 -c:a pcm_s16le original.wav
resample_tool original.wav plugin-44100.wav 44100 reference-soxr-44100.wav
ffmpeg -i original.wav -af aresample=resampler=soxr:precision=28 -ar 44100 -c:a pcm_s16le reference-soxr-44100.wav
```

(make the reference first, the tool then prints the difference to it and writes it to `plugin-44100.wav.diff.wav`,
ten times louder).

## Related facts about user tracks in the game

- **Tracks shorter than 7 seconds are rejected.** `CAEVorbisDecoder::Initialise` fails for user tracks under 7000 ms
  (a compare at `0x502538`), so they are never loaded and the radio stops at the end of the previous track. Keep idents
  and jingles at 7 seconds or longer. The plugin reports such Ogg files in the log (`WARNING: ... is only 5.3 s long`). (The limit could be patched; not done yet, see `FUTURE_FEATURES.md`.)
- The game accepts `.ogg .mp3 .wav .wma .wmv .aac .m4a`, not FLAC.
- Mono Ogg files work (the decoder doubles the channel); more than two channels are rejected.
