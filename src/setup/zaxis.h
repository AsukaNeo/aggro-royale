/*
 * Z-Axis level conversion for the setup (aggro_setup.exe): a level from the
 * player's own Dave Mirra Freestyle BMX 2 or BMX XXX, rewritten as an extra
 * Aggressive Inline level in a mod, and the Free Skate menu that offers the
 * added levels. The C form of tools/zaxis_port.py and
 * tools/build_level_menu.py, which document the formats; both write the
 * same bytes.
 */
#ifndef AR_ZAXIS_H
#define AR_ZAXIS_H

#include <stddef.h>
#include "setup_src.h"

/* Progress lines for the player (setup.c). */
void setup_log(const char *fmt, ...);

/* Level `level` (a folder in media/assets/levels of `src`) into mod folder
 * `mod_dir` as an added level called `display`: assets/levels/<tag>_<level>,
 * listed in the mod's levels.txt. `tag` is "dm2" or "xxx"; `ai_dir` is the
 * player's Aggressive Inline (the start marker, motions.zal and the empty
 * challenge script come from it). 1 on success; 0 with a reason in err. */
int zx_port_level(ArSrc *src, const char *tag, const char *level, const char *mod_dir,
                  const char *display, const char *ai_dir, char *err, size_t errn);

/* mods\!levels-menu from every mod's levels.txt in `mods_dir`: the game's
 * Scene.txt and front-end menus with a Free Skate page per added level.
 * Removed when no level is added. The number of levels, or -1 with a
 * reason in err. */
int zx_build_level_menu(const char *mods_dir, const char *ai_dir, char *err, size_t errn);

/* Folder helpers the setup shares. */
int zx_make_dirs(const char *path);
int zx_write_file(const char *path, const void *data, size_t size);
void *zx_read_file(const char *path, size_t *size);
int zx_remove_tree(const char *path);

#endif /* AR_ZAXIS_H */
