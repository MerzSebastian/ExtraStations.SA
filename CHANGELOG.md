# Changelog

## 1.0.0

First release.

- Any number of extra radio stations as folders (`songs`, `idents`, `ads`, `talk`), listed in `ExtraStations.SA.ini` and
  placed anywhere in the radio scroll cycle; built-in stations can be left out.
- Played by the game's own User Track Player (bass boost, EQ, volume and ducking apply).
- The original game's radio probabilities for songs, idents, ads and talk; per kind: your files, the game's own,
  a mix, or none.
- The original User Track Player and "radio off" stay in the cycle.
- Stations that mix sample rates work (the game cannot do that on its own): Ogg files are converted while playing,
  stations with a single rate are left alone. A repair for the game's silent channel after a change of rate.
- Log at `scripts\ExtraStations.SA.log` with warnings for unknown folders and tracks under 7 seconds (which the game
  does not play).

Known limits: only GTA San Andreas 1.0 US; only Ogg files are converted between sample rates; no intros/outros,
no time-of-day dependent talk, stations do not keep playing while you listen to another one. See
`docs/FUTURE_FEATURES.md`.
