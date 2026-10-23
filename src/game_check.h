/*
 * The game check: the program runs only on the player's own copy of the
 * supported Aggressive Inline release (North American, title 41430007, version 1).
 * The recompiled code is tied to that exact program, and the download holds
 * no game files, so a release build checks default.xbe before it starts.
 */
#ifndef AR_GAME_CHECK_H
#define AR_GAME_CHECK_H

#include <stddef.h>

/* The project's name: windows, messages, the saves folder. The ini and the
 * window title come from RECOMP_APP_NAME in CMakeLists.txt. */
#define AR_APP "Aggro Royale"

/* SHA-1 of the supported default.xbe. build.bat checks the same value. */
#define AR_XBE_SHA1 "7693c1c861392a42fdb63dc46a2d86e9adf16726"

/* 1 if `xbe` (the whole default.xbe, `size` bytes) is the supported release.
 * `found` receives its SHA-1 as 40 lower-case hex digits (empty if it could
 * not be computed); it must hold 41 characters. */
int ar_game_check(const void *xbe, size_t size, char found[41]);

#endif /* AR_GAME_CHECK_H */
