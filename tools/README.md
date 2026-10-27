# tools

Scripts that aren't part of the game itself. Run them from the `aggro-royale` folder.

**Levels from the BMX games.** These are the Python versions of what `aggro_setup.exe` does, kept around because they're much quicker to poke at when figuring out a format. The setup writes the same files, byte for byte.

- `zaxis_port.py`: converts one BMX 2 or BMX XXX level into a mod. `py -3 tools/zaxis_port.py "<game folder>" WOODWARD mods/dm2-levels --add "Woodward"`
- `build_level_menu.py`: rebuilds the Free Skate list after you add or remove level mods.

**Releases.**

- `make_release.py`: builds the download zip and checks nothing from the games ended up in it. See the comment at the top for how to set up the clean build folders.
- `release_readme.txt`: the README that goes in the zip.

**debug/**: odds and ends from bring-up. They read the linker map (`build/Release/aggro_royale.map`) or the generated C.

- `sym.py`, `sym_chain.py`: turn exe addresses from a log into function names.
- `fnx.py`: show the generated C around a guest address.
- `prof_report.py`: summarise a `RECOMP_PROF=1` run.
- `stub_trace.py`: make unresolved stubs print when they're hit.
