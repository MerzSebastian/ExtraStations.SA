# Debugging

For bug reports and for finding out what the game does. Nothing here is needed to use the plugin.

## The log

`scripts\ExtraStations.SA.log` (a log over 1 MB is moved to `ExtraStations.SA.log.old` at the next start). By default it holds
what was found at start-up and anything that goes wrong:

- the stations and what each folder contains, warnings about unknown folders and tracks shorter than 7 seconds,
- which hooks were installed (`ok` / `FAILED`),
- stations that mix sample rates and whether they are converted,
- `KICK` and `STALL` events (see below), which should be rare.

If the game is not run as administrator, Windows may redirect the file to
`%LocalAppData%\VirtualStore\Program Files (x86)\Rockstar Games\Grand Theft Auto San Andreas\scripts\`.

## `[Debug]` section of the ini

Not in the default ini, add it at the end of the file. All keys are optional.

| Key | Meaning |
|---|---|
| `Trace=1` | Logs everything that happens: every key press, every track the game asks for, every change of the game's playback state (word by word) and the state of the audio channel around the end of each track. The log gets very large (a few MB per hour); only for tracking down a bug. Also installs the loader probes. |
| `Probe=1` | Only the probes on the game's track loader (`LoadUserTrack`, `GetTrackPath`, the Vorbis decoder): logs every load and every failure. `LoadUserTrack` is also tried again up to 3 times when it fails. |
| `Vanilla=1` | Installs no hooks at all. The track list is merged as usual, but the game scrolls and picks tracks by itself (its User Track Player plays everything). Tells game bugs from plugin bugs. |
| `NoPicker=1` | The game picks the tracks itself instead of the station's deck (from the whole merged list). |
| `NoAdHooks=1` | Leaves the game's own adverts alone (no replacing of adverts by the station's own). |

## What the watchdogs do

Both only act while the radio is on in a vehicle, outside the menu and with the game in the foreground.

- **`KICK`** - the audio channel says "started" but the DirectSound buffer is not playing (what the game leaves behind
  after a change of sample rate, see [SAMPLE_RATES.md](SAMPLE_RATES.md)). After 300 ms the plugin starts the channel again.
  The log line shows the channel before; `0.6 s after the kick` shows the result (state -3, status 5 = playing).
- **`STALL`** - a track has been over for a long time (12 s, or 25 s during a change of sample rate) or the channel
  reports "finished" (-4) for 1.5 s and the game has not moved on. The plugin writes the game's state to the log and
  retunes the station (the same as scrolling away and back).

## Reading a `CHANNEL` line

```
CHANNEL KICK: state -1 status 0 loop 0 silenced 0 needSwitch 0 needFinish 0 shouldStop 0 slot 0/0 |
  current ok id 74 44100 Hz 11000 ms | next ok id 80 44100 Hz 8000 ms | thread: track 74 next 80 needsService 0 preparing 0
```

- `state`: -1 started, -2 prepared, -3 playing, -4 finished (no next decoder), -5 stopping, -6 stopped, -7 paused.
- `status`: bit 0 = the DirectSound buffer is playing (5 = playing and looping, 0 = not playing).
- `silenced` / `needSwitch`: the channel is in the middle of a change of sample rate.
- `current` / `next`: the decoders - track number, sample rate, length. `none` = no decoder.
- `thread`: the game's stream thread - the track it plays and the one it has prepared.

Offsets and states come from the disassembly of the game; they are listed in
[FUTURE_FEATURES.md](FUTURE_FEATURES.md) (game internals).
