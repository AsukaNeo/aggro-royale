AGGRO ROYALE {version}
A recompilation of Aggressive Inline for the original Xbox, running natively
on Windows.
https://github.com/AsukaNeo/aggro-royale

This is a beta. The game plays from start to finish; expect rough edges.
Report problems at https://github.com/AsukaNeo/aggro-royale/issues
(describe what happened; never attach game files).

WHAT YOU NEED
  - Your own copy of Aggressive Inline for the original Xbox, North American
    release (version 1): its disc image (.iso), or its files (default.xbe and
    the media folder). Nothing from any game is included in this download.
  - Optional, for the bonus features: your own Dave Mirra Freestyle BMX 2
    and/or BMX XXX for the original Xbox (North American), the same way.
  - Windows 10 or 11, 64-bit, and a graphics card with Direct3D 11.

FIRST START
  1. Unzip this folder anywhere you like.
  2. Double-click aggro_setup.exe. It finds game copies next to it; otherwise
     choose each one's folder or disc image:
       - Aggressive Inline (needed). Aggro Royale checks it is the supported
         release, then unpacks a disc image into a "game" folder here (about
         1.2 GB), or uses a folder where it is.
       - Dave Mirra Freestyle BMX 2, BMX XXX (optional, see below).
  3. Press Build, then Play.
  Only playing Aggressive Inline? You can also just double-click
  aggro_royale.exe and choose your copy when it asks.

NEW LEVELS: THE BONUS FEATURES (BETA)
  For the first time since 2002, you can play new levels in Aggressive
  Inline. Give the setup your own BMX XXX and Dave Mirra Freestyle BMX 2,
  and it builds a plus version of the game from them: 18 of their levels join
  Free Skate (after the park editor in the level list) and their 38 songs
  join the music menu as two more soundtracks. Either game on its own works
  too. Untick a game and press Build again to take its extras out.

  These levels were never designed for Aggressive Inline. They work because
  all three games share the same engine, and they are fun to skate, but
  expect rough spots: grinds feel a little different, a few places let you
  fall out of the level, and there are no challenges or characters on them.

SETTINGS
  Settings on the title card, or F10 (Back+Start on the pad) while you play.
  Four tabs: Display, Graphics, Controls and System. Switch them with Q/E,
  Tab or the bumpers. The line under the list explains each setting.
  - Display mode: Windowed, Borderless or Fullscreen, on the monitor you
    choose. Fullscreen uses the resolution you pick. After switching, the
    game asks whether to keep it, and goes back by itself after 15 seconds.
    These wait for "Apply display changes".
  - The game draws on your graphics card at twice the Xbox's resolution, in
    widescreen. Frame generation adds frames up to your display's refresh
    rate. If the picture is wrong on your graphics card, choose Renderer:
    Software.
  - Pause in background (on by default): the game and its sound stop while
    the window is in the background or minimised.

CONTROLS
  Any Xbox-style controller works as player 1. Keyboard: Enter = Start,
  Z X C V = A B X Y, WASD to move, arrow keys for the d-pad.
  F10 (or Back+Start on the pad): settings, with Exit to Windows
  Alt+Enter or F11: windowed / full screen          F12: screenshot

IF SOMETHING GOES WRONG
  - "not the supported version": only the North American release (version 1)
    works, because Aggro Royale is built from that exact program.
  - Settings live in aggroroyale.ini next to aggro_royale.exe; delete it to
    go back to the defaults. Every level is unlocked from a fresh save; set
    unlock_all=0 under [game] to play the career the original way.
  - Saved games are kept in the "saves" folder here, so this whole folder is
    portable: copy it to another PC or drive and your game, extras, settings
    and saves come along. For a new version, unzip it over this folder.
  - To use a different copy of the game, delete the "game" folder (or the
    [game] dir line in aggroroyale.ini) and run the setup again.

LEGAL
  Aggro Royale is MIT-licensed open source (see the licenses folder). It is
  an independent fan project, not affiliated with the rights holders of
  Aggressive Inline, Dave Mirra Freestyle BMX 2 or BMX XXX, or with
  Microsoft. It contains no game data or music and works only with your own
  copies. Parts of the xboxrecomp toolkit compiled into it are LGPL-2.1 (from
  xemu); their source is available at https://github.com/sp00nznet/xboxrecomp
  and this project's patch at https://github.com/AsukaNeo/aggro-royale.
