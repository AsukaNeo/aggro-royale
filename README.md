# Aggro Royale

A static recompilation of *Aggressive Inline* (2002) for the original Xbox, to run natively on Windows. Built on [xboxrecomp](https://github.com/sp00nznet/xboxrecomp), following [Exonerated](https://github.com/AsukaNeo/Exonerated) (Deathrow).

> [!WARNING]
> **Early bring-up.** It does not play yet.

Bring your own copy: nothing from the game is included here.

## Supported release

| | |
|---|---|
| Title | Aggressive Inline, North America, version 1 |
| Title ID | 41430007 |
| `default.xbe` SHA-1 | `7693c1c861392a42fdb63dc46a2d86e9adf16726` |
| XDK | 1.0.4627, D3D linked into the game (`D3D8LTCG`) |

## Layout

The toolkit lives next to this folder as `../xboxrecomp` (branch `ar-v014`): xboxrecomp `v0.14.0` with `patches/xboxrecomp-local.patch` applied (`git apply` in a fresh clone of the tag). Put the game's files (`default.xbe` and its `media` folder) in `game/`.

| Script | What it does |
|---|---|
| `lift.sh [run.log]` | Lifts `game/default.xbe` to C in `src/recomp/gen/` (seeding entry points found in a run's log first, if given) |
| `run.sh <name> [watchdog s] [kill s]` | Runs the build with its log in `<name>.log`, then summarises crashes, stubs and unresolved calls |

Build: `cmake -S . -B build -A x64`, then `cmake --build build --config Release`.

## Legal

MIT licence (see [LICENSE](LICENSE) and [NOTICE](NOTICE)), covering this project's own code only. An independent fan project, not affiliated with the rights holders of *Aggressive Inline* or with Microsoft. No game code, data or assets are included; the generated code is derived from your own copy and never committed.
