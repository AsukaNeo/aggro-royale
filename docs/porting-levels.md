# Porting levels from the BMX games

Notes on how the bonus levels from *Dave Mirra Freestyle BMX 2* and *BMX XXX* get into *Aggressive Inline*, and what tripped us up on the way. The code is `src/setup/zaxis.c` (what `aggro_setup.exe` runs) and `tools/zaxis_port.py` (the same thing in Python, easier to experiment with).

## Why this works at all

All three games are Z-Axis's engine from 2001–2002, and they keep levels the same way: a scene file (`.zit`) pointing at geometry (`.GEO`), textures (`.TXR`), placed objects (`.INS`) and data points like spawns and paths (`.DAT`). Each of those is a `FORM` file of chunks, a four-letter tag, a size and the data, with some chunks holding other chunks.

Aggressive Inline's exe still knows nearly every chunk BMX 2 writes. What changed between the games is mostly the size of a few records, which is why a straight copy gets surprisingly far before falling over.

## BMX 2 → Aggressive Inline

What had to change, roughly in the order we hit it:

- **Materials (`MATI`)**: 156 bytes in BMX 2, 160 in AI. AI keeps the first texture's ID up front at word 1, so it's inserted there.
- **Texture stages**: AI only sets a stage up if its first word's low byte is 1 and its last word is 1. BMX 2 leaves those zero, so every texture was skipped and the ground came out smeared. The stage words are rewritten AI's way.
- **Instances (`TGIH`)**: 108 → 112 bytes, a word inserted before the name.
- **Collision trees (`TGCI`)**: this was the "falling through the floor" bug. BMX 2 stores each leaf's face list inline; AI uses 8-byte leaves that point at face lists stored after the nodes. Read the BMX 2 way, nearly every collision query missed. The trees are rebuilt in AI's layout.
- **Grind and vert tables (`TGFE`, `TGFT`)**: BMX writes 8- and 12-byte records, AI 24 and 28. Read wrong, there were no grind edges and no ramp counted as vert.
- **Surface types**: the numbers mean different things. BMX 2's grass is 5, which is AI's water, so the skater drowned on Woodward's lawns. There's a mapping table in the code; BMX 2's 7 (bog, pond bottoms, fountains) becomes a wet spot rather than water.
- **Water**: BMX 2 makes its water from objects named `watersurfaceDef_*` with an ordinary surface type, so Swamp Trail's water was solid ground. Those objects' materials are marked as water.
- **Environment (`SDPT` env)**: AI expects four distances where BMX 2 has two, and the level came out grey. Fog uses BMX 2's range, and the far clip sits just past it.
- **Path nodes**: 100 bytes with eleven link slots in BMX 2, 80 with six in AI.
- **Visibility sets (`ZPVS`)**: BMX 2's don't parse as AI's, and Train Yard and Airport crashed. They're dropped; the game draws fine without them.

## BMX XXX

BMX XXX turned out to be closer to AI than BMX 2 is: under the compression it's AI's layout almost record for record. The catch is the compression. Every top-level chunk (and every part inside a model) is wrapped in a `ZLZO` chunk holding an LZO1X stream, and AI has no reader for that.

The common LZO libraries are GPL, which this project can't use, so the decoder is written from the format. It's about 70 lines of Python, or a page of C.

After unpacking, BMX XXX only needs the BMX 2 fixes for the environment and path nodes, plus dropping a handful of chunk types AI doesn't know: `TGFV`, `ZFPH`, `ZFPD`, `ZFPT`, `ZSCR`, `ZSCD`, `WSMC` and `ZPEC`. Its snow uses the same surface number as BMX 2's bog, so it's mapped to grass separately.

Its music needed nothing at all: 28 ordinary WMA files, the same format as Aggressive Inline's own.

## Making them extra levels, not replacements

Early on, a ported level replaced Movie Lot. Getting them added alongside the real levels took a few more pieces:

- **A start point.** BMX keeps the spawn as a data point called `PLAYERONE`; AI wants an object called `scriptObj_playerOne`. Without one, the game drops you at the world's origin, under the level. The object and its marker are copied from the player's own Movie Lot and placed at the BMX spawn, then lowered onto the floor beneath it.
- **Spawn and reset points on the ground.** BMX puts them at a rider's height, and in BMX XXX's Mnco some sit under the ground, so they're all moved onto the nearest floor.
- **Files every AI level has.** `motions.zal`, and `<scene>.bin` and `<scene>T.bin` (challenge scripts). Without them the game reports a "dirty disc". The tutorial's motions and the Zoo's empty script stand in.
- **Collectibles.** BMX's pickups and objective items are solid objects in AI, so they're left out.
- **The menu.** The Free Skate list is a script in `uilib.zal`. The added pages go in separate script actions of eight pages each, because one action much bigger than 9 KB crashes the game. There are two copies of the script in that file, and the game reads the second one.
- **The level slot.** Picking an added level loads it into slot 9, which no menu uses (`recomp_manual.c`, the override of `sub_00050050`).

## Still open

- Grinds attach a bit differently. The rails feel thinner, maybe a collision width the BMX games store somewhere else.
- There's no reset at the edge of a ported map, so you can skate off it, and a couple of corners let you fall through.
- No challenges or characters. BMX keeps its objectives as text (`objctv.txt`), not AI's scripts, and that would be a project of its own.
