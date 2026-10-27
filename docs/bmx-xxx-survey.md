# BMX XXX — format survey for Aggro Royale

Read-only comparison of BMX XXX (Z-Axis, Xbox, 2002; the North American
release) with Aggressive Inline (AI, the recomp's `game\` folder) and Dave
Mirra Freestyle BMX 2 (DM2), made from the author's own copies. Done
2026-10-25. Nothing in any game folder was changed.

## Verdict

**Once its compression is removed, BMX XXX's level data is AI's format almost
record for record, and much closer to AI than DM2 is.** Its `.GEO`, `.TXR` and `.INS`
files wrap every top-level chunk (and every TGPT part inside a TGEO) in a
`ZLZO` chunk, which AI's executable does not know. Decompressed, the tree is
AI's: the same containers and nesting (including the TIVC/TIVN/TIVU/TIFN
indexed-vertex blocks DM2 lacks), MATI 160 bytes, TGIH 112, TGPH 20 with
bit 0 set, TXRH 52 + TXPR textures (DM2 uses TBMP/TDDS). Only the `.DAT`
data points (uncompressed) are DM2-like: the environment record is 64 bytes,
and the path node is 104 bytes, a size neither other game uses.

So a BMX XXX importer is mostly **decompress and copy**, plus the DM2-style
environment and path-node fixes, plus dropping a few XXX-only chunks. The
material, instance and part-flag conversions are not needed. Music needs
nothing new: 28 standard WMAs that drop into a soundtrack mod like
`mods\dm2-music`.

## 1. Layout

`media\` (755 MB) has `Scene.txt`, `assets\` and Z-Axis's usual root files.
`assets\` holds `audio`, `create`, `editor`, `levels`, `movies` (XMV, not
Bink), `screens`, a lot of `.zal` banks and per-character string tables
(`stringDM/LP/MO/MY/RA/SE/SH/SY/TF/TG/TV/UG`).

Extension counts under `media\`: 51 zsd, 50 zal, 35 XMV, 28 WMA, 15 TXT,
13 each of ZIX / ZIT / TXR / INS / GEO / DAT, 13 eus + 13 euk, 11 txt, 3 raw,
3 Map, plus odds and ends. (AI: 135 ZIT, 21 each of bin/bnf/bng/bni/bns,
19 GEO, 13 wma. DM2: 34 GEO, 10 wma.)

Levels (`assets\levels`, all with GEO/TXR/INS/DAT/TXT/ZIT/ZIX + `anims.zal`,
`models.zal`, `cutscene.zal`, `cutcomp.zal`, `objctv.txt`):

| Level | GEO | TXR (decompressed) |
|---|---|---|
| CHL_DAM  | 2.7 → 3.2 MB | 5.0 → 11.9 MB |
| CHL_MNCO | 4.1 → 4.7 MB | 4.5 → 10.9 MB |
| CHL_MULY | 3.2 → 3.8 MB | 5.6 → 12.0 MB |
| CHL_NASA | 3.3 → 3.8 MB | 5.1 → 11.5 MB |
| CHL_SHEP | 2.6 → 3.0 MB | 6.7 → 15.4 MB |
| CHL_SYRA | 3.0 → 3.4 MB | 6.0 → 14.9 MB |
| CMP_RMPG | 0.4 → 0.5 MB | 1.7 → 3.8 MB |
| CMP_ROOT | 0.9 → 1.1 MB | 2.8 → 7.0 MB |

Decompressed, these are within AI's own budgets (AI MUSEUM: 7.5 MB GEO, 17.9 MB
TXR). `assets\editor` also holds five small park-editor sets (Mullaly,
Objects, Rampage, UGP, ZAxis) in the same format. `Scene.txt` also names
CMP_SCRT, TST_FLAT, TST_GRND and TST_VERT, which are not on the disc.

## 2. Chunk tags per file kind

Counts across all level and editor files, with BMX XXX counted *after*
decompressing ZLZO (record size × count for leaves):

**.GEO**

| Tag | AI | DM2 | XXX |
|---|---|---|---|
| ZLZO (wrapper) | — | — | 3690 TGEO + 4137 TGPT |
| TGEO / TGHD | 8224 / 64 B | 9887 / 64 B | 3690 / 64 B |
| TGPT / TGPH | 8931 / 20 B | 9918 / 20 B (212 × 16 B) | 4137 / 20 B |
| TMTB / MATI | 21234 × **160** | 28760 × **156** (727 × 124) | 12356 × **160** |
| TIVC TIVN TIVU TIFN (TIDA, TII8, TII6) | yes | **no** | yes |
| TGVP TGFP TGFM TGFE TGFT TGVC TGVN TGVU TGFN TGCI TGLO TGFR TGTD | yes | yes | yes |
| **TGFV** (in TGPT) | — | — | 14 |
| ZDYN / DDEF / DBDY | 82 | 54 | 86 |
| **ZFPH / ZFPD / ZFPT** (top level) | — | — | 8 / 346 / 346 |
| **ZSCR / ZSCD** (`animScript_*`) | — | — | 128 / 6 |
| **WSMC** (`WSMO` records) / **ZPEC** (`ZPEO`/`ZPED`, particle emitters) | — | — | 8 / 8 |

**.TXR**: AI is TXRC{TXRH 52, TXPR}; DM2 is TXRC{TXRH 52 (26 × 44), TBMP/TDDS};
XXX is ZLZO{TXRC{TXRH 52, TXPR}}, i.e. AI's format.

**.INS**: TGIN{TGIH, TGIT} in all three; TGIH is AI 112, DM2 108, XXX 112.
XXX wraps each TGIN in its own ZLZO.

**.DAT**: SDPT, PVSO, ZLIT, ZPVS in all three; not compressed in XXX.

**.ZIT**: the same `FSCN{TERR{TEHD, ZFTX, ZFGE, ZFIS, ZFSD}}` in all three.
XXX pads the file names to 16 bytes, where AI and DM2 use 12.

**.ZIX** (XXX only) is a text index of asset hash, name and type, written by
the exporter. AI does not need it.

Tag strings in each `default.xbe` (a rough check of what each executable
knows): AI's has WSMC, ZPEC, TXPR, TIFN and TBMP/TDDS, but not ZLZO, TGFV,
ZFPH/ZFPD/ZFPT or ZSCR/ZSCD. Only XXX's has ZLZO.

### ZLZO

`ZLZO` body = u32 LE uncompressed size, then a standard **LZO1X** stream
(ends in the usual `11 00 00` marker, padded to 4 bytes). It decompresses
to exactly one complete chunk (tag + size + data). Each file's top-level
TGEO/TXRC/TGIN chunks are wrapped, and inside each decompressed TGEO every
TGPT is wrapped again. All 52 XXX files decompressed cleanly and re-parsed
with `zaxis_port.parse`.

Many XXX `.TXR` and `.INS` files (and 5 `.GEO`s) have bytes after the FORM's
declared size (old buffer contents: DXT-looking blocks, `0xAD` fill).
`read_form` already keeps that as `tail`, and it can be dropped.

## 3. Key records, word by word

Per-word statistics (share zero, share plausible-float, distinct values, top
value) across every record of each kind:

- **MATI**: XXX's 40 words follow AI's profile word for word: w1 = texture
  id (many distinct values; DM2 has `FFE5E5E5` there), colours at w2–w4,
  0.1 / 1.0 floats at w5–w6, `1C000101` at w15, texture id again at w16,
  1 at w17/w18. Nothing was inserted, so **no MATI conversion is needed**.
- **TGIH**: XXX matches AI: w19 is the extra word (93% zero in XXX, 69% in AI;
  in DM2 the name already starts there), and the instance name starts at
  w20. **No conversion is needed.** One difference: XXX w16 and w18 hold
  floats in 17–20% of records, where AI's are always small integers. The
  layout is the same, but those two fields deserve a look if instances
  misbehave.
- **TGPH**: flags w0 are 1 in 95% of XXX parts (`1`, `2001`, `3`, `2004`, `4`),
  the same mix as AI. DM2 has 0 everywhere. **No collision-flag fix is
  needed.** XXX w4 holds a float in 17% of parts (never in AI).
- **TXRH**: 52 bytes, same profile in all three (the name from w5 on is
  longer in XXX).
- **SDPT 0x0C136653 (environment)**: XXX is **64 bytes, like DM2** (AI 72): two
  distances after the colours (e.g. 50/600, 1/500). **The DM2 conversion
  applies as it is** (24, near, far, far clip).
- **SDPT 0x06E53984 (path node)**: XXX is **104 bytes** (AI 80, DM2 100). Words
  0–13 match everywhere (w2 path id, w3 node index), and w14–w24 are DM2's
  eleven link slots. XXX then adds one word, w25, an id-like value (equal
  to the node's own w2 in 350 of 952 nodes, and to some node's w2 in 486).
  XXX nodes use at most 2 links, so packing w14–w24 into AI's six slots
  w14–w19 and dropping w25 loses nothing.
- **Other SDPT types**: shared with AI at the same sizes are 005846BE (48),
  020F5896 (84), 0320F5B6 (124) and 0866E5A2 (80). XXX-only types are 01A62423
  (48), 0422B9CE (240 × 296), 0468D10E (240), 06E6932A (196), 0D9A3A22 (112)
  and 0E482A04 (620 × 389), plus 037B56C5 (52), which DM2 also has. The DM2
  port already copies types AI lacks unchanged.

## 4. Challenge scripts and Scene.txt

XXX levels have **no** `<level>.bin` and no `.bnf/.bng/.bni/.bns`, the same as
DM2. Objectives live in a text `objctv.txt` (`OBJECTIVE_BEGIN … TYPE
"TRANSFER" … FROM_FACE "gap_01a"`), and level props and animations live in
the level's `models.zal`/`anims.zal`/`cutscene.zal`. Free roam through an AI
scene therefore needs the same handling as DM2 levels (whatever the DM2
port does about AI's missing challenge `.bin`).

`Scene.txt` (header still says "BMX2") registers levels as bare
`SCENE { NAME "local.CHL_DAM"  FILE "assets\levels\CHL_DAM\CHL_DAM.zit" }`
blocks, with no per-scene MODEL list. It also has `A.*` / `B.*` copies pointing at
the developers' `S:\bmx3\Art_In_Progress\...` exports. `zaxis_port.point_scene`
(repointing an AI scene's FILE) works unchanged.

## 5. Music

`assets\audio` holds 28 `.WMA` files, every one a standard ASF (header GUID
`3026B275-8E66-CF11-A6D9-00AA0062CE6C`): WMA v2 (format tag `0x0161`),
stereo, 44.1 kHz, 96 kbps. That is the same encoding as AI's 13 songs and DM2's 10. Lengths from the
ASF File Properties object (play duration − preroll):

| File | Length | File | Length |
|---|---|---|---|
| 3IsTheMagicNumber | 3:18 | JustaGirl | 3:29 |
| BasketCase | 3:05 | KnowtheLedge | 3:58 |
| BoutThatTime | 3:30 | Lapdance | 3:31 |
| ClickClickBoom | 4:14 | LooksThatKill | 4:01 |
| Crackling | 4:17 | MagicNumber | 3:13 |
| CuteWithoutTheE | 3:33 | MotleyCrue | 4:08 |
| DateRape | 3:39 | MyFriendsOverYou | 3:42 |
| Down | 3:17 | NERD | 4:57 |
| Downfall | 3:13 | Plastic | 2:22 |
| EvilTwin | 4:31 | Quarashi | 3:24 |
| GirlsGirlsGirls | 4:30 | RapturesDelight | 4:52 |
| GivingGravityAHand | 2:35 | SPP | 3:16 |
| JudgmentDay | 3:37 | TheEnergy | 4:13 |
| Juice | 3:58 | WhiteStripes | 1:49 |

That is 102:26 in all. The ASF files carry no title or artist. The game's
`music.zsd` (a ZSNDXBOX bank of 84-byte entries, the same entry layout as AI's)
names **20** of them. The other 8 (3IsTheMagicNumber, JustaGirl,
KnowtheLedge, LooksThatKill, MotleyCrue, NERD, Quarashi, WhiteStripes) are
not in the bank, the exe or the credits, and are probably cut tracks. None is a
byte-for-byte duplicate. Titles and artists for the 20 are in
`assets\credits.txt` (e.g. "Basketcase" / Greenday, "Girls, Girls, Girls" /
Motley Crue, "Date Rape" / Sublime). A `bmx-xxx-music` mod is just a
`soundtrack.txt` and the files, made the same way as `mods\dm2-music`. The
recomp's `ar_soundtracks` / `ar_wma_ms` already read these lengths.

## 6. What `zaxis_port.py` needs for BMX XXX

1. **LZO1X decompression**: replace each `ZLZO` with the chunk it holds
   (recursively, because TGPTs inside a TGEO are wrapped again), then write
   uncompressed FORMs. The common LZO libraries (liblzo2, python-lzo) are
   GPL, so the project must not use them (MIT, no GPL code). A decoder written
   from the format is about 70 lines of Python (now `lzo1x_decompress` in
   `tools/zaxis_port.py`). Uncompressed output is about
   2.3× larger for TXR and 1.15× for GEO, within AI's sizes.
2. **SDPT environment 64 → 72**: the existing DM2 rule as it is.
3. **SDPT path node 104 → 80**: like the DM2 rule but reading the eleven links
   at offset 56 from a 104-byte body (drop w25).
4. **Drop XXX-only chunks** AI's exe has no tag for: TGFV inside TGPT, and the
   top-level ZFPH/ZFPD/ZFPT and ZSCR/ZSCD. AI knows WSMC and ZPEC, but AI's
   levels never contain them and XXX's layout of them is unverified, so drop
   them too until proven safe.
5. **Skip** the MATI, TGIH and TGPH conversions (XXX is already AI's
   layout). The tool should pick rules by game, for example from the presence
   of ZLZO or from MATI being 160 bytes.
6. Drop the stale tail after the FORM; skip copying `.ZIX`, the `.zal` banks
   and `objctv.txt`.

Open question: AI's DM2-level collision problem may come from data a game
keeps outside these records. BMX XXX's geometry is AI's own format (TIVC/TIFN
blocks, flagged parts), so a converted XXX level is a good test of whether
the collision problem is in the geometry conversion or somewhere else.
