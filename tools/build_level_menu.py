"""build_level_menu.py -- give every added level a Free Skate page.

    py -3 tools/build_level_menu.py

Reads levels.txt in each folder of mods/ (written by zaxis_port.py --add:
"Free Skate name | scene | folder below assets/levels"; a folder whose name
starts with _ is off) and writes mods/!levels-menu/ -- sorted first, so the
mod overlay takes its files over any other mod's:

  media/Scene.txt          the game's, with a SCENE for each added level
  media/assets/uilib.zal   the game's menus, with a Free Skate page for each
                           added level after the park editor
  levels.txt               every added level, which the game reads at start
                           (recomp_manual.c, sub_00050050): a level picked by
                           one of these names plays in slot 9 ("LOCAL", which
                           no menu offers), pointed at the level's scene.

Run it again after adding or removing a level mod. Built from the player's
own game files; nothing of the game's is changed.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
GAME = os.path.join(ROOT, 'game', 'media')
MODS = os.path.join(ROOT, 'mods')
OUT = os.path.join(MODS, '!levels-menu')

# The Free Skate list's icon for an added level: the tutorial's (in the
# level-icon font; Free Skate has no tutorial), until they have their own.
ICON = os.environ.get('AR_MENU_ICON', 'a')


def read_levels():
    found = []
    for mod in sorted(os.listdir(MODS)):
        path = os.path.join(MODS, mod, 'levels.txt')
        if mod.startswith(('_', '!')) or not os.path.exists(path):
            continue
        for line in open(path, encoding='utf-8').read().splitlines():
            if not line.strip() or line.startswith('#'):
                continue
            name, scene, folder = [x.strip() for x in line.split('|')]
            level_dir = os.path.join(MODS, mod, 'media', 'assets', 'levels', folder)
            zits = [f for f in os.listdir(level_dir) if f.lower().endswith('.zit')]
            if not zits:
                print('  %s: no .zit in %s, skipped' % (name, level_dir))
                continue
            found.append((name, scene, folder, zits[0]))
    return found


def scene_txt(levels):
    text = open(os.path.join(GAME, 'Scene.txt'), 'rb').read().decode('latin1')
    add = ''.join('SCENE\r\n{\r\n\tNAME\t"%s"\r\n\tFILE\t"assets\\levels\\%s\\%s"\r\n}\r\n'
                  % (scene, folder, zit) for _, scene, folder, zit in levels)
    # The game stops reading at the file's last line, END: the added scenes
    # go before it.
    end = text.rstrip().rfind('\nEND')
    if end < 0:
        raise SystemExit('Scene.txt has no END')
    return (text[:end + 1] + add + text[end + 1:]).encode('latin1')


# ---- uilib.zal: ZDIR {DIDF count, GRID name hashes, DLEN flags<<28 | length,
# OFST offsets}, then the entries, each at a 16-byte boundary.

def read_zdir(data):
    assert data[:4] == b'ZDIR'
    count = struct.unpack_from('<I', data, 16)[0]
    base = 24 + 8                        # past DIDF, at GRID's data
    grid = list(struct.unpack_from('<%dI' % count, data, base))
    dlen = list(struct.unpack_from('<%dI' % count, data, base + 4 * count + 8))
    ofst = list(struct.unpack_from('<%dI' % count, data, base + 8 * count + 16))
    head = data[:struct.unpack_from('<I', data, 4)[0] + 8]
    entries = [data[o:o + (l & 0x0FFFFFFF)] for o, l in zip(ofst, dlen)]
    return head, grid, dlen, ofst, entries


def write_zdir(head, dlen, entries):
    count = len(entries)
    head = bytearray(head)
    base = 24 + 8
    out = bytearray()
    pos = (len(head) + 15) & ~15
    offsets, lengths = [], []
    order = sorted(range(count), key=lambda k: OFST_ORDER[k])
    placed = {}
    for k in order:
        placed[k] = pos
        pos = (pos + len(entries[k]) + 15) & ~15
    for k in range(count):
        lengths.append((dlen[k] & 0xF0000000) | len(entries[k]))
        offsets.append(placed[k])
    struct.pack_into('<%dI' % count, head, base + 4 * count + 8, *lengths)
    struct.pack_into('<%dI' % count, head, base + 8 * count + 16, *offsets)
    out = bytearray(head)
    for k in order:
        out += b'\0' * (placed[k] - len(out))
        out += entries[k]
    out += b'\0' * (((len(out) + 15) & ~15) - len(out))
    return bytes(out)


OFST_ORDER = []


def patch_freeskate(text, levels):
    """The Free Skate page script with a page for each added level."""
    start = text.index(b'ACTION "Script" "FreeskateOnChange"')
    end = text.index(b'ACTION "Script" "MultiplayerOnChange"', start)
    block = text[start:end]
    icons = b''.join(b'\\n' + ICON.encode() for _ in levels)

    # Every Xbox icon row (the ones with Zoo's "s") gains the added levels.
    out, at = bytearray(), 0
    while True:
        i = block.find(b'SET Items {"', at)
        if i < 0:
            break
        j = block.index(b'"}', i)
        items = block[i + 12:j]
        out += block[at:j]
        if b'\\ns' in items or b'\\nS' in items:
            out += icons
        at = j
    out += block[at:]
    block = bytes(out)

    # The list shows -- and lets the cursor reach -- VisibleCount icons:
    # nine on the Xbox, the levels, Zoo and the park editor.
    visible = int(os.environ.get('AR_MENU_VISIBLE', '0')) or min(10, 9 + len(levels))
    block = block.replace(b'SET VisibleCount {9}', b'SET VisibleCount {%d}' % visible)

    # The added levels' pages, after the park editor's (page 8), go in
    # actions of their own, PAGES_PER_ACTION each: the game crashes reading
    # a script of much more than 9 KB (Free Skate's own is 3.7), which 17
    # added pages in Free Skate's script made. Free Skate's sends the cursor
    # there from page 9 on; each sets its page and the level data and shows
    # the park editor's still background -- no flythrough movie for them.
    t6 = b'\t' * 6
    groups = [levels[i:i + PAGES_PER_ACTION] for i in range(0, len(levels), PAGES_PER_ACTION)]
    jump = t6 + b'IF SelectedIndex > {8} GOTO AddedLevels\r\n'
    k = block.index(t6 + b'GOTO SelectedIndex')
    block = block[:k] + jump + block[k:]
    dispatch = t6 + b':AddedLevels\r\n'
    for g in range(len(groups)):
        last = 8 + (g + 1) * PAGES_PER_ACTION
        dispatch += (t6 + b'IF SelectedIndex > {%d} GOTO AddedLevels%d\r\n' % (last, g + 2)
                     if g + 1 < len(groups) else b'')
        dispatch += t6 + b'PLAY AddedLevels%d\r\n' % (g + 1) + t6 + b'GOTO Complete\r\n'
        if g + 1 < len(groups):
            dispatch += t6 + b':AddedLevels%d\r\n' % (g + 2)
    done = block.index(b':Done')
    line = block.rfind(b'\n', 0, done) + 1
    block = block[:line] + dispatch + b'\r\n' + block[line:]

    base_items = b'o\\nr\\nu\\nw\\ne\\nq\\np\\ns\\ni'
    actions = b''
    for g, group in enumerate(groups):
        pages = t6 + b'GOTO SelectedIndex\r\n'
        for m, (name, scene, _, _) in enumerate(group):
            n = g * PAGES_PER_ACTION + m
            row = b''.join(b'\\n' + (ICON.upper() if k == n else ICON).encode()
                           for k in range(len(levels)))
            pages += (t6 + b':%d\r\n' % (9 + n) +
                      t6 + b'SET .SelectedLevel {"%s"}\r\n' % scene.encode() +
                      t6 + b'SET .LevelData.LevelName.TextLabel.Text {"%s"}\r\n' % name.upper().encode() +
                      t6 + b'SET Items {"%s%s"}\r\n' % (base_items, row) +
                      t6 + b'GOTO Done\r\n')
        pages += (t6 + b':Done\r\n' +
                  t6 + b'CALL inlineMenuPopulateLevelData\r\n' +
                  t6 + b'ABORT StartFlythruMovie\r\n' +
                  t6 + b'CALL inlineMenuStopMovie\r\n' +
                  t6 + b'SET Root.FrontEnd.Movie.Material {"FrontEnd.still_bkg_01"}\r\n')
        actions += (b'ACTION "Script" "AddedLevels%d"\r\n' % (g + 1) +
                    b'\t\t\t\t{\r\n'
                    b'\t\t\t\t\t//////////////////////////////\r\n'
                    b'\t\t\t\t\t// Property\r\n'
                    b'\t\t\t\t\tStartDelay\t0\r\n'
                    b'\t\t\t\t\tScript\t{\r\n' + pages +
                    b'\t\t\t\t\t\t}\r\n'
                    b'\t\t\t\t}\r\n'
                    b'\t\t\t\t')
    return text[:start] + block + actions + text[end:]


# Added pages per script action (see patch_freeskate); 8 keeps each well
# under the size the game reads.
PAGES_PER_ACTION = 8


def uilib(levels):
    data = open(os.path.join(GAME, 'assets', 'uilib.zal'), 'rb').read()
    head, grid, dlen, ofst, entries = read_zdir(data)
    OFST_ORDER[:] = ofst
    # Two entries carry the front end's script (23 and 26); the game reads
    # the second, so every copy is patched.
    for k, e in enumerate(entries):
        if b'ACTION "Script" "FreeskateOnChange"' in e:
            entries[k] = patch_freeskate(e, levels)
    return write_zdir(head, dlen, entries)


def main():
    levels = read_levels()
    if os.environ.get('AR_MENU_LIMIT'):
        levels = levels[:int(os.environ['AR_MENU_LIMIT'])]
    if not levels:
        print('no added levels (no levels.txt in mods/)')
        return
    os.makedirs(os.path.join(OUT, 'media', 'assets'), exist_ok=True)
    with open(os.path.join(OUT, 'media', 'Scene.txt'), 'wb') as f:
        f.write(scene_txt(levels))
    with open(os.path.join(OUT, 'media', 'assets', 'uilib.zal'), 'wb') as f:
        f.write(uilib(levels))
    with open(os.path.join(OUT, 'levels.txt'), 'w', encoding='utf-8') as f:
        f.write('# Built by tools/build_level_menu.py: Free Skate name | scene | folder\n')
        for name, scene, folder, _ in levels:
            f.write('%s | %s | %s\n' % (name, scene, folder))
    for n, (name, scene, folder, zit) in enumerate(levels):
        print('  Free Skate page %d: %-20s scene %-14s %s\\%s' % (9 + n, name, scene, folder, zit))


if __name__ == '__main__':
    main()
