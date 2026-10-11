---
name: Bug report
about: The radio does something wrong (silence, wrong track, crash, a station is missing ...)
labels: bug
---

**What happened, and what did you expect?**

**How to reproduce it** (which station, what you did, how long into the session)

**Setup**
- Plugin version (first line of `ExtraStations.SA.log`, "ExtraStations.SA x.y.z loaded"):
- Game: 1.0 US exe? Other mods that change the radio or the scripts folder (GInput, SilentPatch, ...):
- ASI loader:
- Audio files of the station (format, sample rate if you know it):

**The log**

Attach `scripts\ExtraStations.SA.log` (Windows may have put it in
`%LocalAppData%\VirtualStore\Program Files (x86)\Rockstar Games\Grand Theft Auto San Andreas\scripts\`).
If the problem is hard to describe, add `[Debug]` / `Trace=1` at the end of `ExtraStations.SA.ini`, play until it happens,
and attach the log (it gets large). See `docs/DEBUGGING.md`.

**Your `ExtraStations.SA.ini`** (without the comment block at the top)
