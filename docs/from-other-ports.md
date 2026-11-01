# Ideas from other ports

What other Xbox recomps have done that Aggro Royale can use, picked from the knowledge base's surveys (xbox-recomp-kb: `guides/survey-2026-11.md` and `guides/thps2x-adoption.md`). Last updated 2026-11-01.

Aggro Royale is MIT, so code may come only from MIT (or compatible) projects, with a line in `NOTICE`. GPL projects and projects with no licence are read for ideas only. Each item says which.

## Do next

1. **Lifter fix: jump tables indexed downward.** MSVC's `memcpy`/`memmove` jump through a table indexed *below* its address. Our disassembler counts upward and decodes the rest of the table as instructions, which on another title cut `memcpy` short.
   - Source: xenophobentx/xboxrecomp (Takedawn's toolkit) commit `0a7d117`, about 40 lines in `tools/disasm/engine.py` plus a test. MIT.
   - Our toolkit copy doesn't have it. Needs a re-lift.
2. **Players' own music in any format.** `ar_soundtracks` builds `ST.DB` from WMA files only. Burnout 2 converts MP3, FLAC and the rest to WMA with Windows' built-in Media Foundation (`src/custom_music.cpp`, MIT).
   - Adding that would let mods and players bring music without converting it themselves.
   - The trap: choose the 128 kbps layout with the *shortest encoded block* (Media Foundation reports 16002 or 16005 bytes/s), or the game's decoder stops early.
   - Burnout 2 also streams the conversion in bounded packets on a cancellable worker.
3. **RetroArch shaders on the D3D11 renderer.**
   - apexJCL's toolkit runs a slang preset with librashader's D3D11 runtime in place of the final present (`src/kernel/nv2a_present_chain.c` plus a hook in `src/d3d/nv2a_pb_d3d11.c`). MIT.
   - The usable DLL is apexJCL's D3D11-only build (`librashader-v0.12.0-xbr1`). The official one needs `D3DX9_43.dll`, which Windows 11 lacks. Ship it with its MPL-2.0 licence and a source link, or build our own.

## For the 120 Hz pacing

The user felt 120 Hz pacing was off, and both of these tackle that kind of problem (ideas only: DefJam FFNY and DOA3 have no clear licence):

- **Frame-clock steering.** When the game's simulation clock and the display-locked vblank run at nearly the same rate, their phase drifts and frames alternate 0 and 2 game steps for up to a minute, which reads as judder at an even frame rate. DefJam nudges each clock tick by at most 1 % of a period towards the middle between two step checks, only while the loop keeps up (their fix #140; switch `RECOMP_FRAME_CLOCK_LOCK`, trace `RECOMP_FRAME_CLOCK_TRACE`).
- **Guest vblank locked to the frame deadline.** DOA3's free-running 59.94 Hz vblank drifted through a 60 Hz frame every ~17 s. They fire it at each frame's deadline, and never fire the same boundary twice (that was a follow-up bug: movies ran fast, then flickered).
- How to measure, from THPS 2X: count game steps per presented frame over 10-minute runs, and look for presents arriving in pairs.

## Worth doing

- **Texture packs** with a resident budget (apexJCL `tex_pack`, MIT): replacement textures by hash, an LRU under 512 MB, re-read checks on size and time. Fits the mods folder.
- **RAM cache partition** (apexJCL `kernel_ramfs.c`, MIT): the game copies level files into `Z:\CacheData`. Keeping Z: in RAM (750 MB cap) saves disk writes and avoids stale cached copies of modded files outliving a session.
- **Atomic save writes** (THPS 2X's toolkit, MIT): write to `saves\.pending\` and move over the original on close.
- **Version and system on the first log lines** (THPS 2X's toolkit `host_info.c`, MIT).
- **Button remapping:** THPS 2X's toolkit `xinput_bind.c` (pure logic, menus always reachable) and Burnout 2's `input.cpp` (four players). Both MIT.

## Related projects

- **bmx-xxx-recomp** (abilliontokens) is a separate BMX XXX port on TGP482's XboxXBERecomp SDK. It isn't booting yet and has no licence, so there's nothing to take. Aggro Royale already plays XXX levels.
- **Frame capture and replay** (antbox `src/nvr/nvr_capture.c`, MIT) records pushbuffer kicks and replays them headless through any renderer. Useful if the level-up glow bug turns out to be a drawing problem.
