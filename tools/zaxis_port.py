"""zaxis_port.py -- bring a Dave Mirra Freestyle BMX 2 or BMX XXX level into
Aggro Royale.

    py -3 tools/zaxis_port.py <game folder> <level> <mod folder> --add "Name"
    py -3 tools/zaxis_port.py <game folder> <level> <mod folder> --as SCENE

Reads <game folder>/media/assets/levels/<level>/ from the player's own copy
and writes it, converted, into the mod. With --add the level is an extra
one: it goes to media/assets/levels/<game>_<level>/ and a line in the mod's
levels.txt names it (tools/build_level_menu.py then gives it a Free Skate
page). With --as it replaces the Aggressive Inline scene SCENE instead, by a
Scene.txt in the mod (for testing). Nothing from either game is changed;
removing the mod folder undoes it.

Both games are Z-Axis's engine, files of chunks: a 4-character tag, a 32-bit
little-endian size, the data. Aggressive Inline reads every chunk BMX 2 writes;
what changed between them is the size of two records, each by one word:

  MATI (material, in .GEO)   BMX 2 156 bytes, Aggressive Inline 160: a texture
                             id inserted at word 1 (the first stage's texture,
                             which BMX 2 keeps only in the stage), the rest
                             moved up a word. The reader is sub_000B23A0.
  TGIH (instance, in .INS)   108 bytes, 112: a word inserted at word 19, before
                             the instance's name.
"""
import os
import re
import shutil
import struct
import sys

# Chunks that hold chunks. Everything else is a leaf, copied as it is.
CONTAINERS = {b'TGEO', b'TGPT', b'TMTB', b'TGIN', b'TIVC', b'TIVU', b'TIFN',
              b'TIVN', b'ZDYN', b'ZPVS', b'TXRC'}


def parse(data, off, end):
    """[(tag, payload or [children])] for the chunks in data[off:end]."""
    out = []
    while off + 8 <= end:
        tag = data[off:off + 4]
        size = struct.unpack_from('<I', data, off + 4)[0]
        body = data[off + 8:off + 8 + size]
        if len(body) != size:
            raise ValueError('chunk %r at %d runs past its parent' % (tag, off))
        out.append((tag, parse(body, 0, size) if tag in CONTAINERS else body))
        off += 8 + size
    if off != end:
        raise ValueError('%d stray bytes at %d' % (end - off, off))
    return out


def build(chunks):
    out = bytearray()
    for tag, body in chunks:
        payload = build(body) if isinstance(body, list) else body
        out += tag + struct.pack('<I', len(payload)) + payload
    return bytes(out)


def read_form(path):
    data = open(path, 'rb').read()
    if data[:4] != b'FORM':
        raise ValueError('%s: not a FORM file' % path)
    size = struct.unpack_from('<I', data, 4)[0]
    return parse(data, 8, 8 + size), data[8 + size:]


def write_form(path, chunks, tail=b''):
    body = build(chunks)
    with open(path, 'wb') as f:
        f.write(b'FORM' + struct.pack('<I', len(body)) + body + tail)


def lzo1x_decompress(src, out_len):
    """LZO1X, written from the format (the LZO libraries are GPL)."""
    out = bytearray()
    ip = 0

    def length(t, base):
        nonlocal ip
        if t:
            return t
        while src[ip] == 0:
            t += 255
            ip += 1
        t += base + src[ip]
        ip += 1
        return t

    def copy_match(pos, n):
        for _ in range(n):
            out.append(out[pos])
            pos += 1

    state = 'loop'
    t = 0
    if src[0] > 17:
        t = src[0] - 17
        ip = 1
        out += src[ip:ip + t]
        ip += t
        state = 'first' if t >= 4 else 'match_next'
        if state == 'match_next':
            t = src[ip]
            ip += 1
            state = 'match'
    while True:
        if state == 'loop':
            t = src[ip]
            ip += 1
            if t >= 16:
                state = 'match'
                continue
            t = length(t, 15) + 3
            out += src[ip:ip + t]
            ip += t
            state = 'first'
        if state == 'first':
            t = src[ip]
            ip += 1
            if t >= 16:
                state = 'match'
                continue
            pos = len(out) - (1 + 0x0800) - (t >> 2) - (src[ip] << 2)
            ip += 1
            copy_match(pos, 3)
            state = 'done'
        if state == 'match':
            if t >= 64:
                pos = len(out) - 1 - ((t >> 2) & 7) - (src[ip] << 3)
                ip += 1
                copy_match(pos, (t >> 5) + 1)
            elif t >= 32:
                t = length(t & 31, 31)
                pos = len(out) - 1 - ((src[ip] | src[ip + 1] << 8) >> 2)
                ip += 2
                copy_match(pos, t + 2)
            elif t >= 16:
                pos = len(out) - ((t & 8) << 11)
                t = length(t & 7, 7)
                pos -= (src[ip] | src[ip + 1] << 8) >> 2
                ip += 2
                if pos == len(out):
                    break                       # end of stream
                copy_match(pos - 0x4000, t + 2)
            else:
                pos = len(out) - 1 - (t >> 2) - (src[ip] << 2)
                ip += 1
                copy_match(pos, 2)
            state = 'done'
        if state == 'done':
            t = src[ip - 2] & 3
            if t == 0:
                state = 'loop'
                continue
            out += src[ip:ip + t]
            ip += t
            t = src[ip]
            ip += 1
            state = 'match'
    if len(out) != out_len:
        raise ValueError('LZO: %d bytes out, %d expected' % (len(out), out_len))
    return bytes(out)


def unwrap_lzo(chunks):
    """BMX XXX wraps chunks in ZLZO (u32 size, then an LZO1X stream holding
    one whole chunk); Aggressive Inline has no reader for it. Each is replaced
    by the chunk it holds, recursively (a TGEO's TGPTs are wrapped again)."""
    out = []
    for tag, body in chunks:
        if tag == b'ZLZO':
            size = struct.unpack_from('<I', body, 0)[0]
            inner = lzo1x_decompress(body[4:], size)
            tag, body = parse(inner, 0, len(inner))[0]
        if isinstance(body, list):
            body = unwrap_lzo(body)
        out.append((tag, body))
    return out


def walk(chunks, fn):
    """fn(tag, body) -> new body or None, for every leaf; containers recursed."""
    count = 0
    for i, (tag, body) in enumerate(chunks):
        if isinstance(body, list):
            count += walk(body, fn)
        else:
            new = fn(tag, body)
            if new is not None:
                chunks[i] = (tag, new)
                count += 1
    return count


# Surface types (BMX 2 MATI word 6, Aggressive Inline word 7), named from
# the textures that carry each across every level of both games. They
# differ: BMX 2's grass is 5, which is Aggressive Inline's water -- the
# skater drowned on Woodward's lawns. BMX 2: 0 general, 1 metal, 2 wood,
# 3 sheet metal, 4 brick, 5 grass, 6 water, 7 pond bottom, 8 trees, 9 tyres
# and signs, A ground, B rock and dirt, C jump tops, D-E fences, F fabric.
# Aggressive Inline: 0 general, 1 wood (trees too), 2 metal, 3 pipe, 4 grass,
# 5 water, A brick and rock, D wet spot (the Museum's puddles).
SURFACE_DM2 = {0: 0, 1: 2, 2: 1, 3: 3, 4: 0xA, 5: 4, 6: 5, 7: 0xD, 8: 1, 9: 0,
               0xA: 0, 0xB: 0xA, 0xC: 0, 0xD: 2, 0xE: 2, 0xF: 0}
# BMX 2's 8 is trees, foliage and grassy banks -- Swamp Trail's WATEREDGE
# textures are its grass banks, which as water drowned the skater on them --
# so wood, as Aggressive Inline's trees are. BMX XXX numbers its surfaces as
# BMX 2 does (its grass was water too). 7 is shallow: BMX 2's bog, pond
# bottoms and fountains, ridden through -- a wet spot, not water, which drowned
# the skater on Swamp Trail's bog -- and BMX XXX's snow (Syracuse), grass.
SURFACE_XXX = {**SURFACE_DM2, 7: 4}
TEXTURE_NAMES = {}                          # name hash -> name, per level


def surface_bmx(value, texture, xxx=False):
    return (SURFACE_XXX if xxx else SURFACE_DM2).get(value, 0)


def load_texture_names(level_dir):
    """The level's texture names by hash: each texture's own header in the
    .TXR (TXRH: word 0 its name's hash, the name from byte 20), and BMX 2's
    exporter log (.TXT) besides."""
    TEXTURE_NAMES.clear()
    for name in os.listdir(level_dir):
        path = os.path.join(level_dir, name)
        ext = os.path.splitext(name)[1].upper()
        if ext == '.TXR':
            for tag, body in read_level_form(path)[0]:
                if tag != b'TXRC':
                    continue
                for t, h in body:
                    if t == b'TXRH' and len(h) >= 24:
                        TEXTURE_NAMES[struct.unpack_from('<I', h, 0)[0]] = \
                            h[20:].split(b'\0')[0].decode('latin1').upper()
        elif ext == '.TXT':
            text = open(path, 'rb').read().decode('latin1')
            for n in re.findall(r'[\x5c/]([A-Za-z0-9_\- ]+)\.(?:dds|xpr|bmp|tga)', text, re.I):
                for v in (n, n + '.DDS', n + '.XPR', n + '.BMP', n + '.TGA'):
                    TEXTURE_NAMES[name_hash(v)] = n.upper()
        elif ext == '.ZIX':
            for line in open(path, 'rb').read().decode('latin1').splitlines():
                cols = line.split('\t')
                # BMX XXX's asset index: "<texture>_t_TXRC" for a texture,
                # which its materials name by the hash of "<texture>".
                if len(cols) > 2 and cols[2].strip() == 'TXR':
                    base = cols[1].split('_t_')[0]
                    for v in (base, base + '.TGA', base + '.DDS', base + '.BMP', base + '.XPR'):
                        TEXTURE_NAMES.setdefault(name_hash(v), base.upper())


def mark_water(chunks, xxx):
    """BMX 2 makes its water by name: the surfaces you ride over are objects
    called watersurfaceDef_*, of a reflection texture and surface 0 -- solid
    ground to Aggressive Inline, so Swamp Trail's water could not drown you.
    Their materials take the water surface (Aggressive Inline's 5)."""
    count = 0
    word = 7 if xxx else 6
    for tag, body in chunks:
        if tag != b'TGEO':
            continue
        head = [b for t, b in body if t == b'TGHD']
        name = head[0][28:60].split(b'\0')[0].decode('latin1').lower() if head else ''
        if 'watersurface' not in name.replace('_', ''):
            continue
        for t, table in body:
            if t != b'TMTB':
                continue
            for i, (k, m) in enumerate(table):
                if k == b'MATI' and len(m) > 4 * word:
                    m = bytearray(m)
                    struct.pack_into('<I', m, 4 * word, 6)       # BMX water
                    table[i] = (k, bytes(m))
                    count += 1
    return count


def mati_xxx(tag, body):
    """BMX XXX's material: Aggressive Inline's layout, BMX's surfaces."""
    if tag != b'MATI' or len(body) != 160:
        return None
    w = list(struct.unpack('<40I', body))
    w[7] = surface_bmx(w[7], w[1], xxx=True)
    return struct.pack('<40I', *w)


# Texture stages: four of seven words after the stage count (BMX 2 word 10,
# Aggressive Inline word 11). The game sets a stage up (sub_000BE1C0) only
# when its first word's low byte is 1 and its last word's low bits are 1;
# it then takes the address and filter flags from the second word's low
# byte and the blend mode from the first word's third byte. BMX 2 keeps the
# flags in the first word's low byte, the blend in its second byte, and
# leaves the last two words 0 -- so every stage was skipped and textures
# were clamped, smeared across the ground. Aggressive Inline's stage:
# [1 | blend << 16, flags | word 2, word 3, word 4, texture, 1, 1].
def stage_dm2(s):
    if not s[4]:
        return s                            # no texture: as it was
    flags, blend = s[0] & 0xFF, (s[0] >> 8) & 0xF
    return [1 | (blend << 16), s[1] | flags, s[2] & 0xFFFF0000, s[3], s[4], 1, 1]


def mati(tag, body):
    if tag != b'MATI' or len(body) != 156:
        return None
    w = list(struct.unpack('<39I', body))
    w[6] = surface_bmx(w[6], w[15])
    for k in range(min(w[10], 4)):
        w[11 + 7 * k: 18 + 7 * k] = stage_dm2(w[11 + 7 * k: 18 + 7 * k])
    return struct.pack('<40I', w[0], w[15], *w[1:])


# TGFE (a part's edges: grinds) and TGFT (its faces: vert, ramps, ground):
# u32 header -- bits 0-3 the index type (1: u16), 4-13 the record size,
# 14-31 the record count -- u32 entries, a u16 per edge or face giving its
# record (1-based; 0 none), padded to 16 bytes, then the records (the
# reader, sub_00089450; the lookup, sub_00089540). Both BMX games write
# 8-byte edge records [hash, flags] and 12-byte face records [hash, word,
# type byte, 0x32, 0, 0]; Aggressive Inline 24-byte [hash, 0, flags, 0, 0, 0]
# and 28-byte [hash, 0, 0, type, 0, 0, 0] (the commonest of each, as
# exported) -- read the BMX way, no edge grinds and no ramp is vert.
# Face types, by the objects that carry them in every level of the three
# games: 0 none, 1 vert (quarter pipes, bowls, pools), 2, 3 ramps and
# kickers alike; BMX's ground, road, wall and stair kinds 7-B are
# Aggressive Inline's one 0xD.
FACE_TYPE_BMX = {0: 0, 1: 1, 2: 2, 3: 3, 7: 0xD, 8: 0xD, 9: 0xD, 0xA: 0xD, 0xB: 0xD}


def surface_attrs(tag, body):
    if tag not in (b'TGFE', b'TGFT') or len(body) < 8:
        return None
    head, entries = struct.unpack_from('<II', body, 0)
    kind, width, count = head & 0xF, (head >> 4) & 0x3FF, head >> 14
    old_w, new_w = (8, 24) if tag == b'TGFE' else (12, 28)
    if width != old_w:
        return None
    start = 8 + ((2 * entries + 15) & ~15)
    out = bytearray(body[:start])
    struct.pack_into('<I', out, 0, kind | (new_w << 4) | (count << 14))
    for r in range(count):
        rec = body[start + r * old_w: start + (r + 1) * old_w]
        if tag == b'TGFE':
            h, flags = struct.unpack('<II', rec)
            out += struct.pack('<6I', h, 0, flags, 0, 0, 0)
        else:
            h = struct.unpack_from('<I', rec, 0)[0]
            out += struct.pack('<7I', h, 0, 0, FACE_TYPE_BMX.get(rec[8], 0), 0, 0, 0)
    return bytes(out)


# TGCI: an object's collision tree, u32 id (its part's TGPH id), u32 size,
# then the tree, positions in dwords from its start. A node is 8 bytes:
# axis (1-3; 0 a leaf), a byte, s16 split (cm), u16 first child, u16 second.
# Leaves differ: BMX 2 writes the count and its u16 face numbers inline
# after the leaf's first word (so nodes are of any size); Aggressive Inline
# and BMX XXX write an 8-byte leaf -- 0, 0, u16 count, u32 offset (in u16s
# from the tree's start) into face lists stored after the last node. Read
# BMX 2's way, nearly every query missed (the face numbers taken as an
# offset), and the skater fell through the level.
def ci_parse_inline(data, i):
    kind = data[4 * i]
    if kind == 0:
        n = struct.unpack_from('<H', data, 4 * i + 2)[0]
        return list(struct.unpack_from('<%dH' % n, data, 4 * i + 4))
    split, a, b = struct.unpack_from('<hHH', data, 4 * i + 2)
    return (kind, split, ci_parse_inline(data, a), ci_parse_inline(data, b))


def ci_parse_listed(data, i):
    kind = data[4 * i]
    if kind == 0:
        n, off = struct.unpack_from('<HI', data, 4 * i + 2)
        return list(struct.unpack_from('<%dH' % n, data, 2 * off))
    split, a, b = struct.unpack_from('<hHH', data, 4 * i + 2)
    return (kind, split, ci_parse_listed(data, a), ci_parse_listed(data, b))


def ci_build_listed(tree):
    nodes, leaves = [], []

    def emit(t):
        at = len(nodes)
        nodes.append(None)
        if isinstance(t, list):
            nodes[at] = ('leaf', len(leaves))
            leaves.append(t)
        else:
            kind, split, a, b = t
            ia = emit(a)
            ib = emit(b)
            nodes[at] = ('node', kind, split, ia, ib)
        return at

    emit(tree)
    out = bytearray()
    pos = len(nodes) * 4            # the face lists' start, in u16s
    starts = []
    for faces in leaves:
        starts.append(pos)
        pos += len(faces)
    for n in nodes:
        if n[0] == 'leaf':
            out += struct.pack('<BBHI', 0, 0, len(leaves[n[1]]), starts[n[1]])
        else:
            _, kind, split, ia, ib = n
            out += struct.pack('<BBhHH', kind, 0, split, 2 * ia, 2 * ib)
    for faces in leaves:
        out += struct.pack('<%dH' % len(faces), *faces)
    return bytes(out)


def tgci(tag, body):
    if tag != b'TGCI' or len(body) < 8:
        return None
    ident, size = struct.unpack_from('<II', body, 0)
    tree = ci_build_listed(ci_parse_inline(body[8:8 + size], 0))
    return struct.pack('<II', ident, len(tree)) + tree


def tgih(tag, body):
    if tag != b'TGIH' or len(body) != 108:
        return None
    return body[:76] + b'\0\0\0\0' + body[76:]


# SDPT (data point, in .DAT): a 0x2C-byte header -- size, type, id, position
# -- then a body by type. Two types changed size:
SDPT_ENV = 0x0C136653    # the level's environment, one per level: three
                         # colours (fog first), then distances -- BMX 2 two
                         # (64 bytes), Aggressive Inline four (72): 24, fog
                         # start, fog end, far clip (Movie Lot 24/250/600/1000)
SDPT_PATH = 0x06E53984   # path node: BMX 2 eleven link slots (100 bytes),
                         # Aggressive Inline six (80)


def sdpt(tag, body):
    if tag != b'SDPT' or len(body) < 0x2C:
        return None
    kind = struct.unpack_from('<I', body, 4)[0]
    if kind == SDPT_ENV and len(body) == 64:
        a, b = struct.unpack_from('<2f', body, 56)
        near, far = min(a, b), max(a, b)
        # Fog over BMX 2's range, the far clip just past its end: all beyond
        # is in the fog anyway, and drawing it out to Aggressive Inline's
        # usual 1000 m cost frames on levels with no visibility data.
        return body[:56] + struct.pack('<4f', 24.0, near, far, far * 1.05)
    if kind == SDPT_PATH and len(body) in (100, 104):
        # BMX XXX adds one more word (an id) after the eleven slots.
        links = [x for x in struct.unpack_from('<11I', body, 56) if x][:6]
        return body[:56] + struct.pack('<6I', *(links + [0] * (6 - len(links))))
    return None


# Names are found by an ELF (PJW) hash of the upper-cased name: object
# SCRIPTOBJ_PLAYERONE's geometry is 0x0230D2C5, BMX 2's start point PLAYERONE
# is 0x06DA4725.
def name_hash(name):
    h = 0
    for c in name.upper().encode('latin1'):
        h = (h << 4) + c
        g = h & 0xF0000000
        if g:
            h ^= g >> 24
        h &= ~g & 0xFFFFFFFF
    return h


# The player's start. BMX 2 keeps it as a data point named PLAYERONE (SDPT
# type 0x005846BE: position at 0x10, rotation x y z w at 0x1C); Aggressive
# Inline as an object, scriptObj_playerOne, whose geometry is a small marker
# in the level's own .GEO -- without one the game logs "Unable to find
# ScriptObj_PlayerOne" and drops the player at the world's origin, under the
# level. Both the object and its marker are taken from the player's own
# Aggressive Inline (Movie Lot) and placed at BMX 2's point.
SDPT_SPAWN = 0x005846BE
START_GEO = name_hash('SCRIPTOBJ_PLAYERONE')
AI_TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'game',
                           'media', 'assets', 'levels', 'MOVIE', 'Movie')


def find_start(dat_chunks):
    want = name_hash('PLAYERONE')
    for tag, body in dat_chunks:
        if tag == b'SDPT' and len(body) >= 0x2C:
            kind, ident = struct.unpack_from('<II', body, 4)
            if kind == SDPT_SPAWN and ident == want:
                return struct.unpack_from('<3f', body, 0x10), struct.unpack_from('<4f', body, 0x1C)
    return None


def start_template():
    """(TGEO chunk of the start marker, TGIN chunk of the start object)."""
    geo, _ = read_form(AI_TEMPLATE + '.GEO')
    marker = next(c for c in geo if c[0] == b'TGEO' and any(
        t == b'TGHD' and struct.unpack_from('<I', b, 60)[0] == START_GEO for t, b in c[1]))
    ins, _ = read_form(AI_TEMPLATE + '.INS')
    obj = next(c for c in ins if c[0] == b'TGIN' and any(
        t == b'TGIH' and struct.unpack_from('<I', b, 60)[0] == START_GEO for t, b in c[1]))
    return marker, obj


def place_start(obj, marker, pos, rot):
    """The template start object moved to pos, turned to rot."""
    lo = struct.unpack_from('<3f', [b for t, b in marker[1] if t == b'TGHD'][0], 0)
    hi = struct.unpack_from('<3f', [b for t, b in marker[1] if t == b'TGHD'][0], 12)
    out = []
    for tag, body in obj[1]:
        if tag == b'TGIH':
            body = bytearray(body)
            x, y, z, w = rot
            # Instance rotation is stored y, x, z, w (Movie Lot's starts).
            struct.pack_into('<4f', body, 8, y, x, z, w)
            struct.pack_into('<3f', body, 24, *pos)
            struct.pack_into('<6f', body, 36, *[p + o for p, o in zip(pos * 2, lo + hi)])
            body = bytes(body)
        out.append((tag, body))
    return (obj[0], out)


# BMX XXX's chunks that Aggressive Inline has no reader for (TGFV in a part;
# the rest at the top of a .GEO), or whose layout there is unverified.
XXX_DROP = {b'TGFV', b'ZFPH', b'ZFPD', b'ZFPT', b'ZSCR', b'ZSCD', b'WSMC', b'ZPEC'}


def drop(chunks, tags):
    return [(t, drop(b, tags) if isinstance(b, list) else b) for t, b in chunks if t not in tags]


def read_level_form(path):
    """A level file's chunks in Aggressive Inline's terms, and whether it came
    from BMX XXX (whose chunks are LZO-wrapped and otherwise AI's own
    layout, so the BMX 2 record conversions do not apply)."""
    chunks, tail = read_form(path)
    xxx = any(t == b'ZLZO' for t, _ in chunks)
    if xxx:
        chunks = drop(unwrap_lzo(chunks), XXX_DROP)
        tail = b''                      # stale buffer bytes past the FORM
    return chunks, tail, xxx


# Collectibles -- BMX XXX's pickups and coins, BMX 2's letters -- are solid
# objects in Aggressive Inline, which has no such thing: in a free-roam level
# they are only in the way, and are left out.
COLLECTIBLE = ('pickup', 'coin', 'animobj_pickup', 'dynamics_letter_',
               # BMX XXX's mission things: collectibles of every kind, with
               # their spins, glows and notes, and the checkpoint markers.
               'chip', 'banana', 'doughnut', 'fourty', 'lunchbox', 'dollarsign',
               'bikepart', 'boombox', 'boom_box', 'musicnote', 'soundwave',
               'rotatebox', 'innerglow', 'outerglow', 'checkpoint_', 'ball_0')


def is_collectible(chunk):
    tag, body = chunk
    if tag != b'TGIN':
        return False
    head = [b for t, b in body if t == b'TGIH']
    if not head:
        return False
    name = head[0][80 if len(head[0]) == 112 else 76:].split(b'\0')[0].decode('latin1').lower()
    return name.startswith(COLLECTIBLE)


# ---- The ground under a point ---------------------------------------------
# The BMX games put their spawn and reset points at a rider's height, or
# (BMX XXX's Mnco) under the ground: a skater started in the air, or was
# reset into the level. Each is set onto the floor beneath it: the level's
# floors in world space (each instance's mesh, TGVP points and TGFP faces,
# turned by its quaternion x y z w at TGIH words 2-5 and moved to words
# 6-8), up being -y.

class Ground:
    CELL = 4.0

    def __init__(self, geo_chunks, ins_chunks):
        meshes = {}
        for tag, body in geo_chunks:
            if tag != b'TGEO':
                continue
            head = [b for t, b in body if t == b'TGHD']
            if not head:
                continue
            tris = []
            for t, part in body:
                if t != b'TGPT':
                    continue
                p = dict(part)
                if b'TGVP' not in p or b'TGFP' not in p:
                    continue
                v = [struct.unpack_from('<3f', p[b'TGVP'], 12 * i) for i in range(len(p[b'TGVP']) // 12)]
                for k in range(len(p[b'TGFP']) // 8):
                    _, a, b, c = struct.unpack_from('<4H', p[b'TGFP'], 8 * k)
                    if max(a, b, c) < len(v):
                        tris.append((v[a], v[b], v[c]))
            meshes[struct.unpack_from('<I', head[0], 60)[0]] = tris
        self.grid = {}
        for tag, body in ins_chunks:
            if tag != b'TGIN':
                continue
            head = [b for t, b in body if t == b'TGIH']
            if not head or len(head[0]) < 64:
                continue
            ih = head[0]
            q = struct.unpack_from('<4f', ih, 8)
            pos = struct.unpack_from('<3f', ih, 24)
            for tri in meshes.get(struct.unpack_from('<I', ih, 60)[0], ()):
                w = [tuple(r + o for r, o in zip(rotate(q, p), pos)) for p in tri]
                n = cross(sub(w[1], w[0]), sub(w[2], w[0]))
                length = (n[0] ** 2 + n[1] ** 2 + n[2] ** 2) ** 0.5
                if length < 1e-9 or abs(n[1]) < 0.5 * length:
                    continue                # walls and slivers: no floor
                xs = [p[0] for p in w]
                zs = [p[2] for p in w]
                for gx in range(int(min(xs) // self.CELL), int(max(xs) // self.CELL) + 1):
                    for gz in range(int(min(zs) // self.CELL), int(max(zs) // self.CELL) + 1):
                        self.grid.setdefault((gx, gz), []).append(w)

    def under(self, x, y, z):
        """The floor a point should stand on: the one beneath it, else --
        a point with nothing beneath is under the ground (BMX XXX's Mnco
        has reset points 8 m down) -- the nearest up to 10 m above it."""
        h = self.floor(x, y, z, above=0.0, below=30.0)
        return h if h is not None else self.floor(x, y, z, above=10.0, below=0.0)

    def floor(self, x, y, z, above=3.0, below=30.0):
        """The highest floor from `above` over (x, y, z) down to `below`
        under it, or None."""
        best = None
        for a, b, c in self.grid.get((int(x // self.CELL), int(z // self.CELL)), ()):
            d = (b[0] - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (b[2] - a[2])
            if abs(d) < 1e-12:
                continue
            u = ((x - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (z - a[2])) / d
            v = ((b[0] - a[0]) * (z - a[2]) - (x - a[0]) * (b[2] - a[2])) / d
            if u < 0 or v < 0 or u + v > 1:
                continue
            h = a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1])
            if y - above <= h <= y + below and (best is None or h < best):
                best = h
        return best


def rotate(q, v):
    x, y, z, w = q
    tx, ty, tz = 2 * (y * v[2] - z * v[1]), 2 * (z * v[0] - x * v[2]), 2 * (x * v[1] - y * v[0])
    return (v[0] + w * tx + (y * tz - z * ty), v[1] + w * ty + (z * tx - x * tz),
            v[2] + w * tz + (x * ty - y * tx))


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


GROUND = None                               # the level being converted


def snap_spawn(tag, body):
    """A spawn or reset point (SDPT 0x005846BE) set onto its floor."""
    if GROUND is None or tag != b'SDPT' or len(body) < 0x1C:
        return None
    if struct.unpack_from('<I', body, 4)[0] != SDPT_SPAWN:
        return None
    x, y, z = struct.unpack_from('<3f', body, 0x10)
    h = GROUND.under(x, y, z)
    if h is None or abs(h - y) < 0.05:
        return None
    body = bytearray(body)
    struct.pack_into('<f', body, 0x14, h)
    return bytes(body)


def convert_level(src_dir, dst_dir):
    global GROUND
    os.makedirs(dst_dir, exist_ok=True)
    load_texture_names(src_dir)
    names = sorted(os.listdir(src_dir))
    geo = [n for n in names if n.upper().endswith('.GEO')]
    ins = [n for n in names if n.upper().endswith('.INS')]
    GROUND = None
    if geo and ins:
        GROUND = Ground(read_level_form(os.path.join(src_dir, geo[0]))[0],
                        read_level_form(os.path.join(src_dir, ins[0]))[0])
    start = None
    for name in names:
        if os.path.splitext(name)[1].upper() == '.DAT':
            start = find_start(read_form(os.path.join(src_dir, name))[0])
    if start and GROUND:
        (x, y, z), rot = start
        h = GROUND.under(x, y, z)
        if h is not None:
            print('  start set on its floor: %.2f m down' % (h - y))
            start = ((x, h, z), rot)
    template = start_template() if start else None
    if not start:
        print('  (no PLAYERONE start point: the player will start at the origin)')
    for name in names:
        src = os.path.join(src_dir, name)
        dst = os.path.join(dst_dir, name)
        ext = os.path.splitext(name)[1].upper()
        if ext == '.GEO':
            chunks, tail, xxx = read_level_form(src)
            water = mark_water(chunks, xxx)      # before the surfaces are mapped
            n = walk(chunks, mati_xxx if xxx else mati)
            trees = 0 if xxx else walk(chunks, tgci)
            attrs = walk(chunks, surface_attrs)
            if template:
                chunks.append(template[0])
            write_form(dst, chunks, tail)
            print('  %-16s %d materials re-laid, %d collision trees rebuilt, %d grind/vert tables, %d water surfaces%s'
                  % (name, n, trees, attrs, water, ', start marker added' if template else ''))
        elif ext == '.INS':
            chunks, tail, xxx = read_level_form(src)
            n = 0 if xxx else walk(chunks, tgih)
            kept = [c for c in chunks if not is_collectible(c)]
            dropped = len(chunks) - len(kept)
            chunks = kept
            if template:
                chunks.append(place_start(template[1], template[0], *start))
            write_form(dst, chunks, tail)
            print('  %-16s %d instances re-laid, %d collectibles removed%s' % (
                name, n, dropped, ', start at (%.1f, %.1f, %.1f)' % start[0] if template else ''))
        elif ext == '.TXR':
            chunks, tail, xxx = read_level_form(src)
            write_form(dst, chunks, tail)
            print('  %-16s %s' % (name, 'decompressed' if xxx else 'copied'))
        elif ext == '.DAT':
            chunks, tail = read_form(src)
            n = walk(chunks, sdpt)
            snapped = walk(chunks, snap_spawn)
            if snapped:
                print('  %-16s %d spawn points set on their floors' % ('', snapped))
            # Visibility sets (ZPVS): BMX 2's PVSO records are not Aggressive
            # Inline's, and read as its ("ziffChunkReadData: Clamping size")
            # Train Yard and Airport crashed. The game draws without them.
            pvs = sum(1 for t, _ in chunks if t == b'ZPVS')
            chunks = [c for c in chunks if c[0] != b'ZPVS']
            write_form(dst, chunks, tail)
            print('  %-16s %d data points re-laid%s' % (name, n, ', visibility sets dropped' if pvs else ''))
        else:
            shutil.copyfile(src, dst)
            print('  %-16s copied' % name)
    # Every Aggressive Inline level has motions.zal, its objects' animation
    # library, and the game loads it from the level's folder; missing, it is
    # a "dirty disc". The tutorial's, nearly empty, stands in.
    motions = os.path.join(dst_dir, 'motions.zal')
    if not os.path.exists(motions):
        shutil.copyfile(os.path.join(os.path.dirname(AI_TEMPLATE), '..', 'TUT', 'motions.zal'), motions)
        print('  %-16s the tutorial\'s' % 'motions.zal')


def point_scene(game_scene_txt, mod_media, scene, level):
    """The game's Scene.txt with SCENE's FILE pointed at the ported level --
    the mod's own, when an earlier port already made one, so several levels
    can share a mod."""
    own = os.path.join(mod_media, 'Scene.txt')
    text = open(own if os.path.exists(own) else game_scene_txt, 'rb').read().decode('latin1')
    key = 'NAME\t"%s"' % scene
    at = text.find(key)
    if at < 0:
        raise SystemExit('no scene %s in %s' % (scene, game_scene_txt))
    line = text.find('FILE', at)
    start = text.find('"', line) + 1
    end = text.find('"', start)
    text = text[:start] + 'assets\\levels\\%s\\%s.zit' % (level, level.lower()) + text[end:]
    with open(os.path.join(mod_media, 'Scene.txt'), 'wb') as f:
        f.write(text.encode('latin1'))


def game_tag(game_folder):
    name = os.path.basename(os.path.normpath(game_folder)).lower()
    return 'xxx' if 'xxx' in name else 'dm2' if 'mirra' in name else 'zax'


def add_level(mod, display, scene, folder):
    """A line in the mod's levels.txt: Free Skate name | scene | folder."""
    path = os.path.join(mod, 'levels.txt')
    lines = []
    if os.path.exists(path):
        lines = [l for l in open(path, encoding='utf-8').read().splitlines()
                 if l.strip() and not l.startswith('#') and l.split('|')[1].strip() != scene]
    lines.append('%s | %s | %s' % (display, scene, folder))
    with open(path, 'w', encoding='utf-8') as f:
        f.write('# Levels this mod adds: Free Skate name | scene | folder below assets/levels\n')
        f.write('\n'.join(lines) + '\n')


def main(argv):
    args = [a for a in argv if not a.startswith('--')]
    scene, display = 'MOVIE', None
    if '--as' in argv:
        scene = argv[argv.index('--as') + 1]
        args.remove(scene)
    if '--add' in argv:
        display = argv[argv.index('--add') + 1]
        args.remove(display)
    if len(args) != 3:
        raise SystemExit(__doc__)
    game, level, mod = args
    levels = os.path.join(game, 'media', 'assets', 'levels')
    match = [d for d in os.listdir(levels) if d.lower() == level.lower()]
    if not match:
        raise SystemExit('no level %s in %s' % (level, levels))
    level = match[0]
    media = os.path.join(mod, 'media')
    folder = ('%s_%s' % (game_tag(game), level)).lower() if display else level.lower()
    print('%s -> %s' % (os.path.join(levels, level), os.path.join(mod, folder)))
    convert_level(os.path.join(levels, level), os.path.join(media, 'assets', 'levels', folder))
    if display:
        scene = folder.upper().replace('_', '')
        # The level's challenge scripts, <scene>.bin and <scene>T.bin, read
        # from its folder by name; missing, they are a "dirty disc". Free
        # roam has none: the Zoo's empty one (60 bytes) stands in for both.
        empty = os.path.join(os.path.dirname(AI_TEMPLATE), '..', 'ZOO', 'ZOO.bin')
        for bin_name in (scene + '.bin', scene + 'T.bin'):
            shutil.copyfile(empty, os.path.join(media, 'assets', 'levels', folder, bin_name))
        add_level(mod, display, scene, folder)
        print('  levels.txt       %s: scene %s' % (display, scene))
        return
    game_scene = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'game',
                              'media', 'Scene.txt')
    point_scene(game_scene, media, scene, level.lower())
    print('  Scene.txt        %s -> %s' % (scene, level.lower()))


if __name__ == '__main__':
    main(sys.argv[1:])
