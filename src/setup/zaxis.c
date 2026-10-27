/*
 * Z-Axis level conversion (zaxis.h): tools/zaxis_port.py in C, for a setup
 * that needs no Python. The formats, and why each change is made, are
 * described there; this follows it step for step and writes the same
 * bytes (the spawn heights in double precision, as Python computes them,
 * rounded to float only when stored).
 */
#include <windows.h>
#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zaxis.h"

/* ---- memory: everything a level needs, freed at once ------------------ */

typedef struct Block {
    struct Block *next;
    size_t used, cap;
    uint8_t data[1];
} Block;

typedef struct {
    Block  *blocks;
    void  **owned;               /* malloc'd buffers (files read) */
    int     nowned, capowned;
    jmp_buf fail;
    char   *err;
    size_t  errn;
} Ctx;

static void fail(Ctx *c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->err, c->errn, fmt, ap);
    va_end(ap);
    longjmp(c->fail, 1);
}

static void *alloc(Ctx *c, size_t n)
{
    Block *b = c->blocks;
    n = (n + 15) & ~(size_t)15;
    if (!b || b->used + n > b->cap) {
        size_t cap = n > (4u << 20) ? n : (4u << 20);
        b = (Block *)malloc(sizeof *b + cap);
        if (!b)
            fail(c, "Out of memory.");
        b->next = c->blocks;
        b->used = 0;
        b->cap = cap;
        c->blocks = b;
    }
    b->used += n;
    return b->data + b->used - n;
}

static void own(Ctx *c, void *p)
{
    if (c->nowned == c->capowned) {
        int cap = c->capowned ? c->capowned * 2 : 16;
        void **grown = (void **)realloc(c->owned, (size_t)cap * sizeof *grown);
        if (!grown) {
            free(p);
            fail(c, "Out of memory.");
        }
        c->owned = grown;
        c->capowned = cap;
    }
    c->owned[c->nowned++] = p;
}

static void ctx_free(Ctx *c)
{
    while (c->blocks) {
        Block *b = c->blocks;
        c->blocks = b->next;
        free(b);
    }
    while (c->nowned)
        free(c->owned[--c->nowned]);
    free(c->owned);
    c->owned = NULL;
    c->capowned = 0;
}

/* ---- files -------------------------------------------------------------- */

int zx_make_dirs(const char *path)
{
    char p[MAX_PATH], *s;
    snprintf(p, sizeof p, "%s", path);
    for (s = p; *s; s++)
        if (*s == '/')
            *s = '\\';
    for (s = p + 3; *s; s++)
        if (*s == '\\') {
            *s = 0;
            CreateDirectoryA(p, NULL);
            *s = '\\';
        }
    CreateDirectoryA(p, NULL);
    return (GetFileAttributesA(p) & FILE_ATTRIBUTE_DIRECTORY) != 0
           && GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

int zx_write_file(const char *path, const void *data, size_t size)
{
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wrote = 0;
    int ok;
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    ok = WriteFile(f, data, (DWORD)size, &wrote, NULL) && wrote == (DWORD)size;
    return CloseHandle(f) && ok;
}

void *zx_read_file(const char *path, size_t *size)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER sz;
    void *buf = NULL;
    DWORD got = 0;

    *size = 0;
    if (f == INVALID_HANDLE_VALUE)
        return NULL;
    if (GetFileSizeEx(f, &sz) && sz.QuadPart >= 0 && sz.QuadPart < (1LL << 31)
        && (buf = malloc(sz.QuadPart ? (size_t)sz.QuadPart : 1)) != NULL
        && ReadFile(f, buf, (DWORD)sz.QuadPart, &got, NULL) && got == (DWORD)sz.QuadPart)
        *size = (size_t)sz.QuadPart;
    else {
        free(buf);
        buf = NULL;
    }
    CloseHandle(f);
    return buf;
}

int zx_remove_tree(const char *path)
{
    char pattern[MAX_PATH], child[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    DWORD attr = GetFileAttributesA(path);

    if (attr == INVALID_FILE_ATTRIBUTES)
        return 1;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY) || (attr & FILE_ATTRIBUTE_REPARSE_POINT))
        return (attr & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryA(path) : DeleteFileA(path);
    snprintf(pattern, sizeof pattern, "%s\\*", path);
    h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
                continue;
            snprintf(child, sizeof child, "%s\\%s", path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesA(child, fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            zx_remove_tree(child);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryA(path);
}

static uint8_t *read_src(Ctx *c, ArSrc *s, const char *path, size_t *size)
{
    void *p = ar_src_read(s, path, size);
    if (!p)
        fail(c, "Could not read %s from the game.", path);
    own(c, p);
    return (uint8_t *)p;
}

static uint8_t *read_host(Ctx *c, const char *path, size_t *size)
{
    void *p = zx_read_file(path, size);
    if (!p)
        fail(c, "Could not read %s.", path);
    own(c, p);
    return (uint8_t *)p;
}

/* ---- chunks ------------------------------------------------------------- */

typedef struct Chunk Chunk;
typedef struct {
    Chunk *v;
    int    n, cap;
} List;

struct Chunk {
    char           tag[4];
    const uint8_t *data;
    uint32_t       size;
    int            cont;         /* a container: kids, not data */
    List           kids;
};

static int is(const Chunk *k, const char *tag)
{
    return !memcmp(k->tag, tag, 4);
}

static int is_container(const uint8_t *tag)
{
    static const char *const names[] = { "TGEO", "TGPT", "TMTB", "TGIN", "TIVC", "TIVU",
                                         "TIFN", "TIVN", "ZDYN", "ZPVS", "TXRC" };
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!memcmp(tag, names[i], 4))
            return 1;
    return 0;
}

static void push(Ctx *c, List *l, const Chunk *k)
{
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 8;
        Chunk *v = (Chunk *)alloc(c, (size_t)cap * sizeof *v);
        if (l->n)
            memcpy(v, l->v, (size_t)l->n * sizeof *v);
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n++] = *k;
}

static uint32_t u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t u16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static int16_t s16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static double f32(const uint8_t *p) { float v; memcpy(&v, p, 4); return (double)v; }
static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static void putf(uint8_t *p, double v) { float f = (float)v; memcpy(p, &f, 4); }

static void parse(Ctx *c, const uint8_t *d, size_t off, size_t end, List *out, int depth)
{
    if (depth > 64)
        fail(c, "Chunks nest too deeply.");
    while (off + 8 <= end) {
        Chunk k;
        uint32_t size = u32(d + off + 4);
        memset(&k, 0, sizeof k);
        memcpy(k.tag, d + off, 4);
        if (size > end - off - 8)
            fail(c, "A %.4s chunk runs past its parent.", k.tag);
        k.size = size;
        k.data = d + off + 8;
        if (is_container(d + off)) {
            k.cont = 1;
            parse(c, d, off + 8, off + 8 + size, &k.kids, depth + 1);
        }
        push(c, out, &k);
        off += 8 + (size_t)size;
    }
    if (off != end)
        fail(c, "%u stray bytes in a chunk.", (unsigned)(end - off));
}

static size_t list_size(const List *l)
{
    size_t n = 0;
    int i;
    for (i = 0; i < l->n; i++)
        n += 8 + (l->v[i].cont ? list_size(&l->v[i].kids) : l->v[i].size);
    return n;
}

static uint8_t *build(const List *l, uint8_t *p)
{
    int i;
    for (i = 0; i < l->n; i++) {
        const Chunk *k = &l->v[i];
        uint32_t size = (uint32_t)(k->cont ? list_size(&k->kids) : k->size);
        memcpy(p, k->tag, 4);
        put32(p + 4, size);
        p += 8;
        if (k->cont)
            p = build(&k->kids, p);
        else {
            memcpy(p, k->data, size);
            p += size;
        }
    }
    return p;
}

typedef struct {
    List           chunks;
    const uint8_t *tail;
    size_t         tailn;
    int            xxx;
} Form;

static void read_form(Ctx *c, const uint8_t *d, size_t n, const char *name, Form *f)
{
    uint32_t size;
    memset(f, 0, sizeof *f);
    if (n < 8 || memcmp(d, "FORM", 4))
        fail(c, "%s is not a level file of the expected kind.", name);
    size = u32(d + 4);
    if (size > n - 8)
        fail(c, "%s is cut short.", name);
    parse(c, d, 8, 8 + (size_t)size, &f->chunks, 0);
    f->tail = d + 8 + size;
    f->tailn = n - 8 - size;
}

static void write_form(Ctx *c, const char *path, const Form *f)
{
    size_t body = list_size(&f->chunks);
    uint8_t *out = (uint8_t *)alloc(c, 8 + body + f->tailn);
    memcpy(out, "FORM", 4);
    put32(out + 4, (uint32_t)body);
    build(&f->chunks, out + 8);
    if (f->tailn)
        memcpy(out + 8 + body, f->tail, f->tailn);
    if (!zx_write_file(path, out, 8 + body + f->tailn))
        fail(c, "Could not write %s.", path);
}

/* ---- BMX XXX's compression ----------------------------------------------- */

/* LZO1X, written from the format (the LZO libraries are GPL). */
static uint8_t *lzo1x(Ctx *c, const uint8_t *src, size_t srcn, size_t outn)
{
    uint8_t *out = (uint8_t *)alloc(c, outn ? outn : 1);
    size_t ip = 0, op = 0, t = 0;
    enum { LOOP, FIRST, MATCH, DONE } state = LOOP;

#define NEED(k) do { if (ip + (k) > srcn) fail(c, "Compressed data is cut short."); } while (0)
#define LIT(k) do { NEED(k); if (op + (k) > outn) fail(c, "Compressed data is longer than it says."); \
                    memcpy(out + op, src + ip, (k)); op += (k); ip += (k); } while (0)
#define COPY(pos, k) do { size_t _p = (pos), _k = (k); \
                    if (_p >= op || op + _k > outn) fail(c, "Compressed data is damaged."); \
                    while (_k--) out[op++] = out[_p++]; } while (0)
#define LENGTH(tt, base) do { if (!(tt)) { NEED(1); while (src[ip] == 0) { (tt) += 255; ip++; NEED(1); } \
                    (tt) += (base) + src[ip]; ip++; } } while (0)

    NEED(1);
    if (src[0] > 17) {
        t = src[0] - 17u;
        ip = 1;
        LIT(t);
        if (t >= 4)
            state = FIRST;
        else {
            NEED(1);
            t = src[ip++];
            state = MATCH;
        }
    }
    for (;;) {
        if (state == LOOP) {
            NEED(1);
            t = src[ip++];
            if (t >= 16) {
                state = MATCH;
                continue;
            }
            LENGTH(t, 15);
            t += 3;
            LIT(t);
            state = FIRST;
        }
        if (state == FIRST) {
            size_t back;
            NEED(1);
            t = src[ip++];
            if (t >= 16) {
                state = MATCH;
                continue;
            }
            NEED(1);
            back = (1 + 0x0800) + (t >> 2) + ((size_t)src[ip] << 2);
            ip++;
            if (back > op)
                fail(c, "Compressed data is damaged.");
            COPY(op - back, 3);
            state = DONE;
        }
        if (state == MATCH) {
            size_t back;
            if (t >= 64) {
                NEED(1);
                back = 1 + ((t >> 2) & 7) + ((size_t)src[ip] << 3);
                ip++;
                if (back > op)
                    fail(c, "Compressed data is damaged.");
                COPY(op - back, (t >> 5) + 1);
            } else if (t >= 32) {
                t &= 31;
                LENGTH(t, 31);
                NEED(2);
                back = 1 + ((size_t)(src[ip] | src[ip + 1] << 8) >> 2);
                ip += 2;
                if (back > op)
                    fail(c, "Compressed data is damaged.");
                COPY(op - back, t + 2);
            } else if (t >= 16) {
                size_t hi = (t & 8) << 11, d;
                t &= 7;
                LENGTH(t, 7);
                NEED(2);
                d = (size_t)(src[ip] | src[ip + 1] << 8) >> 2;
                ip += 2;
                if (hi + d == 0)
                    break;                              /* end of stream */
                back = hi + d + 0x4000;
                if (back > op)
                    fail(c, "Compressed data is damaged.");
                COPY(op - back, t + 2);
            } else {
                NEED(1);
                back = 1 + (t >> 2) + ((size_t)src[ip] << 2);
                ip++;
                if (back > op)
                    fail(c, "Compressed data is damaged.");
                COPY(op - back, 2);
            }
            state = DONE;
        }
        if (state == DONE) {
            if (ip < 2)
                fail(c, "Compressed data is damaged.");
            t = src[ip - 2] & 3u;
            if (t == 0) {
                state = LOOP;
                continue;
            }
            LIT(t);
            NEED(1);
            t = src[ip++];
            state = MATCH;
        }
    }
    if (op != outn)
        fail(c, "Compressed data: %u bytes out, %u expected.", (unsigned)op, (unsigned)outn);
#undef NEED
#undef LIT
#undef COPY
#undef LENGTH
    return out;
}

static void unwrap(Ctx *c, List *l)
{
    int i;
    for (i = 0; i < l->n; i++) {
        Chunk *k = &l->v[i];
        if (is(k, "ZLZO")) {
            List inner;
            uint32_t size;
            uint8_t *data;
            if (k->cont || k->size < 4)
                fail(c, "A compressed chunk is damaged.");
            size = u32(k->data);
            data = lzo1x(c, k->data + 4, k->size - 4, size);
            memset(&inner, 0, sizeof inner);
            parse(c, data, 0, size, &inner, 0);
            if (!inner.n)
                fail(c, "A compressed chunk is empty.");
            *k = inner.v[0];
        }
        if (k->cont)
            unwrap(c, &k->kids);
    }
}

/* BMX XXX's chunks that Aggressive Inline has no reader for. */
static void drop_xxx(List *l)
{
    static const char *const tags[] = { "TGFV", "ZFPH", "ZFPD", "ZFPT", "ZSCR", "ZSCD", "WSMC", "ZPEC" };
    int i, j, k = 0;
    for (i = 0; i < l->n; i++) {
        int gone = 0;
        for (j = 0; j < (int)(sizeof tags / sizeof tags[0]); j++)
            if (is(&l->v[i], tags[j]))
                gone = 1;
        if (gone)
            continue;
        if (l->v[i].cont)
            drop_xxx(&l->v[i].kids);
        l->v[k++] = l->v[i];
    }
    l->n = k;
}

/* A level file in Aggressive Inline's terms: BMX XXX's decompressed. */
static void read_level_form(Ctx *c, const uint8_t *d, size_t n, const char *name, Form *f)
{
    int i;
    read_form(c, d, n, name, f);
    for (i = 0; i < f->chunks.n; i++)
        if (is(&f->chunks.v[i], "ZLZO"))
            f->xxx = 1;
    if (f->xxx) {
        unwrap(c, &f->chunks);
        drop_xxx(&f->chunks);
        f->tailn = 0;                  /* stale buffer bytes past the FORM */
    }
}

/* Every leaf, depth first; fn rewrites one and returns 1, or returns 0. */
static int walk(Ctx *c, List *l, int (*fn)(Ctx *, Chunk *))
{
    int i, n = 0;
    for (i = 0; i < l->n; i++)
        n += l->v[i].cont ? walk(c, &l->v[i].kids, fn) : fn(c, &l->v[i]);
    return n;
}

static uint8_t *rewrite(Ctx *c, Chunk *k, uint32_t size)
{
    uint8_t *p = (uint8_t *)alloc(c, size ? size : 1);
    k->data = p;
    k->size = size;
    return p;
}

static const Chunk *first(const List *l, const char *tag)
{
    int i;
    for (i = 0; i < l->n; i++)
        if (is(&l->v[i], tag))
            return &l->v[i];
    return NULL;
}

/* ---- the record conversions ---------------------------------------------- */

static const uint8_t SURFACE_DM2[16] = { 0, 2, 1, 3, 0xA, 4, 5, 0xD, 1, 0, 0, 0xA, 0, 2, 2, 0 };

static uint32_t surface_bmx(uint32_t v, int xxx)
{
    if (xxx && v == 7)
        return 4;                       /* BMX XXX's snow: grass */
    return v < 16 ? SURFACE_DM2[v] : 0;
}

static int mark_water(Ctx *c, List *l, int xxx)
{
    int i, j, k, count = 0;
    uint32_t word = xxx ? 7 : 6;
    for (i = 0; i < l->n; i++) {
        Chunk *geo = &l->v[i];
        const Chunk *head;
        char name[33], squeezed[33];
        int n = 0;
        if (!is(geo, "TGEO") || !geo->cont)
            continue;
        head = first(&geo->kids, "TGHD");
        name[0] = 0;
        if (head && !head->cont) {
            uint32_t a = head->size > 28 ? head->size - 28 : 0;
            if (a > 32)
                a = 32;
            memcpy(name, head->data + 28, a);
            name[a] = 0;
        }
        for (j = 0; name[j]; j++)
            if (name[j] != '_')
                squeezed[n++] = (char)tolower((unsigned char)name[j]);
        squeezed[n] = 0;
        if (!strstr(squeezed, "watersurface"))
            continue;
        for (j = 0; j < geo->kids.n; j++) {
            Chunk *table = &geo->kids.v[j];
            if (!is(table, "TMTB") || !table->cont)
                continue;
            for (k = 0; k < table->kids.n; k++) {
                Chunk *m = &table->kids.v[k];
                if (is(m, "MATI") && !m->cont && m->size > 4 * word) {
                    const uint8_t *old = m->data;
                    uint8_t *p = rewrite(c, m, m->size);
                    memcpy(p, old, m->size);
                    put32(p + 4 * word, 6);              /* BMX water */
                    count++;
                }
            }
        }
    }
    return count;
}

static int mati_xxx(Ctx *c, Chunk *k)
{
    const uint8_t *old;
    uint8_t *p;
    if (!is(k, "MATI") || k->size != 160)
        return 0;
    old = k->data;
    p = rewrite(c, k, 160);
    memcpy(p, old, 160);
    put32(p + 28, surface_bmx(u32(old + 28), 1));
    return 1;
}

static int mati_dm2(Ctx *c, Chunk *k)
{
    uint32_t w[40];
    uint32_t s, stages;
    uint8_t *p;
    int i;
    if (!is(k, "MATI") || k->size != 156)
        return 0;
    memcpy(w, k->data, 156);
    w[6] = surface_bmx(w[6], 0);
    stages = w[10] < 4 ? w[10] : 4;
    for (s = 0; s < stages; s++) {
        uint32_t *t = &w[11 + 7 * s];
        if (t[4]) {
            uint32_t flags = t[0] & 0xFF, blend = (t[0] >> 8) & 0xF;
            t[0] = 1 | (blend << 16);
            t[1] |= flags;
            t[2] &= 0xFFFF0000u;
            t[5] = 1;
            t[6] = 1;
        }
    }
    p = rewrite(c, k, 160);
    put32(p, w[0]);
    put32(p + 4, w[15]);
    for (i = 1; i < 39; i++)
        put32(p + 4 * (i + 1), w[i]);
    return 1;
}

static uint32_t face_type_bmx(uint8_t t)
{
    if (t <= 3)
        return t;
    return t >= 7 && t <= 0xB ? 0xD : 0;
}

static int surface_attrs(Ctx *c, Chunk *k)
{
    uint32_t head, entries, kind, width, count, old_w, new_w, r;
    size_t start;
    const uint8_t *old;
    uint8_t *p;
    int edges = is(k, "TGFE");
    if ((!edges && !is(k, "TGFT")) || k->size < 8)
        return 0;
    head = u32(k->data);
    entries = u32(k->data + 4);
    kind = head & 0xF;
    width = (head >> 4) & 0x3FF;
    count = head >> 14;
    old_w = edges ? 8 : 12;
    new_w = edges ? 24 : 28;
    if (width != old_w)
        return 0;
    start = 8 + ((2 * (size_t)entries + 15) & ~(size_t)15);
    if (start > k->size || start + (size_t)count * old_w > k->size)
        fail(c, "A grind/vert table is cut short.");
    old = k->data;
    p = rewrite(c, k, (uint32_t)(start + (size_t)count * new_w));
    memcpy(p, old, start);
    put32(p, kind | (new_w << 4) | (count << 14));
    for (r = 0; r < count; r++) {
        const uint8_t *rec = old + start + (size_t)r * old_w;
        uint8_t *o = p + start + (size_t)r * new_w;
        memset(o, 0, new_w);
        put32(o, u32(rec));
        if (edges)
            put32(o + 8, u32(rec + 4));
        else
            put32(o + 12, face_type_bmx(rec[8]));
    }
    return 1;
}

/* TGCI: BMX 2's collision tree (leaves inline) in Aggressive Inline's
 * layout (8-byte leaves into face lists after the nodes). */
typedef struct {
    int      leaf;
    uint8_t  kind;
    int16_t  split;
    int      a, b, li;
} CiNode;

typedef struct {
    const uint8_t *data;
    size_t         size;
    CiNode        *nodes;
    int            nn, capn;
    const uint8_t **faces;       /* each leaf's u16 face numbers */
    uint16_t      *counts;
    int            nl, capl;
} CiTree;

static int ci_emit(Ctx *c, CiTree *t, uint32_t i, int depth)
{
    size_t at = 4 * (size_t)i;
    int me;
    if (depth > 200 || at + 4 > t->size)
        fail(c, "A collision tree is damaged.");
    if (t->nn == t->capn) {
        int cap = t->capn ? t->capn * 2 : 64;
        CiNode *v = (CiNode *)alloc(c, (size_t)cap * sizeof *v);
        if (t->nn)
            memcpy(v, t->nodes, (size_t)t->nn * sizeof *v);
        t->nodes = v;
        t->capn = cap;
    }
    me = t->nn++;
    memset(&t->nodes[me], 0, sizeof t->nodes[me]);
    if (t->data[at] == 0) {
        uint16_t n = u16(t->data + at + 2);
        if (at + 4 + 2 * (size_t)n > t->size)
            fail(c, "A collision tree is damaged.");
        if (t->nl == t->capl) {
            int cap = t->capl ? t->capl * 2 : 64;
            const uint8_t **f = (const uint8_t **)alloc(c, (size_t)cap * sizeof *f);
            uint16_t *cn = (uint16_t *)alloc(c, (size_t)cap * sizeof *cn);
            if (t->nl) {
                memcpy(f, t->faces, (size_t)t->nl * sizeof *f);
                memcpy(cn, t->counts, (size_t)t->nl * sizeof *cn);
            }
            t->faces = f;
            t->counts = cn;
            t->capl = cap;
        }
        t->nodes[me].leaf = 1;
        t->nodes[me].li = t->nl;
        t->faces[t->nl] = t->data + at + 4;
        t->counts[t->nl] = n;
        t->nl++;
    } else {
        int a, b;
        if (at + 8 > t->size)
            fail(c, "A collision tree is damaged.");
        t->nodes[me].kind = t->data[at];
        t->nodes[me].split = s16(t->data + at + 2);
        a = ci_emit(c, t, u16(t->data + at + 4), depth + 1);
        b = ci_emit(c, t, u16(t->data + at + 6), depth + 1);
        t->nodes[me].a = a;
        t->nodes[me].b = b;
    }
    return me;
}

static int tgci(Ctx *c, Chunk *k)
{
    CiTree t;
    uint32_t ident, size, pos;
    size_t total;
    uint8_t *p, *o;
    int i;
    if (!is(k, "TGCI") || k->size < 8)
        return 0;
    ident = u32(k->data);
    size = u32(k->data + 4);
    if (size > k->size - 8)
        size = k->size - 8;
    memset(&t, 0, sizeof t);
    t.data = k->data + 8;
    t.size = size;
    ci_emit(c, &t, 0, 0);
    total = (size_t)t.nn * 8;
    for (i = 0; i < t.nl; i++)
        total += 2 * (size_t)t.counts[i];
    p = rewrite(c, k, (uint32_t)(8 + total));
    put32(p, ident);
    put32(p + 4, (uint32_t)total);
    o = p + 8;
    pos = (uint32_t)t.nn * 4;            /* the face lists' start, in u16s */
    {
        uint32_t *starts = (uint32_t *)alloc(c, (size_t)(t.nl ? t.nl : 1) * sizeof *starts);
        for (i = 0; i < t.nl; i++) {
            starts[i] = pos;
            pos += t.counts[i];
        }
        for (i = 0; i < t.nn; i++, o += 8) {
            const CiNode *n = &t.nodes[i];
            memset(o, 0, 8);
            if (n->leaf) {
                put16(o + 2, t.counts[n->li]);
                put32(o + 4, starts[n->li]);
            } else {
                o[0] = n->kind;
                put16(o + 2, (uint16_t)n->split);
                put16(o + 4, (uint16_t)(2 * n->a));
                put16(o + 6, (uint16_t)(2 * n->b));
            }
        }
    }
    for (i = 0; i < t.nl; i++) {
        memcpy(o, t.faces[i], 2 * (size_t)t.counts[i]);
        o += 2 * (size_t)t.counts[i];
    }
    return 1;
}

static int tgih(Ctx *c, Chunk *k)
{
    const uint8_t *old;
    uint8_t *p;
    if (!is(k, "TGIH") || k->size != 108)
        return 0;
    old = k->data;
    p = rewrite(c, k, 112);
    memcpy(p, old, 76);
    memset(p + 76, 0, 4);
    memcpy(p + 80, old + 76, 32);
    return 1;
}

#define SDPT_ENV   0x0C136653u
#define SDPT_PATH  0x06E53984u
#define SDPT_SPAWN 0x005846BEu

static int sdpt(Ctx *c, Chunk *k)
{
    uint32_t kind;
    const uint8_t *old;
    uint8_t *p;
    if (!is(k, "SDPT") || k->size < 0x2C)
        return 0;
    kind = u32(k->data + 4);
    old = k->data;
    if (kind == SDPT_ENV && k->size == 64) {
        double a = f32(old + 56), b = f32(old + 60);
        double nearer = b < a ? b : a, farther = b > a ? b : a;
        p = rewrite(c, k, 72);
        memcpy(p, old, 56);
        putf(p + 56, 24.0);
        putf(p + 60, nearer);
        putf(p + 64, farther);
        putf(p + 68, farther * 1.05);
        return 1;
    }
    if (kind == SDPT_PATH && (k->size == 100 || k->size == 104)) {
        int i, n = 0;
        p = rewrite(c, k, 80);
        memcpy(p, old, 56);
        memset(p + 56, 0, 24);
        for (i = 0; i < 11 && n < 6; i++) {
            uint32_t link = u32(old + 56 + 4 * i);
            if (link)
                put32(p + 56 + 4 * n++, link);
        }
        return 1;
    }
    return 0;
}

/* The ELF (PJW) hash of an upper-cased name. */
static uint32_t name_hash(const char *s)
{
    uint32_t h = 0, g;
    for (; *s; s++) {
        h = (h << 4) + (uint8_t)toupper((unsigned char)*s);
        g = h & 0xF0000000u;
        if (g)
            h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

/* ---- the ground under a point --------------------------------------------- */

typedef struct {
    int64_t key;
    int    *idx;
    int     n, cap, used;
} Cell;

typedef struct {
    Cell   *cells;
    size_t  ncells, used;
    double *tris;                /* 9 doubles each: a, b, c in world space */
    size_t  ntri, captri;
} Ground;

#define CELL 4.0

static uint64_t mix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static Cell *cell_find(Ground *g, int64_t key, int make)
{
    size_t i;
    if (make && (g->used + 1) * 2 > g->ncells) {
        size_t n = g->ncells ? g->ncells * 2 : 4096, k;
        Cell *grown = (Cell *)calloc(n, sizeof *grown);
        if (!grown)
            return NULL;
        for (k = 0; k < g->ncells; k++)
            if (g->cells[k].used) {
                size_t j = mix64((uint64_t)g->cells[k].key) & (n - 1);
                while (grown[j].used)
                    j = (j + 1) & (n - 1);
                grown[j] = g->cells[k];
            }
        free(g->cells);
        g->cells = grown;
        g->ncells = n;
    }
    if (!g->ncells)
        return NULL;
    i = mix64((uint64_t)key) & (g->ncells - 1);
    while (g->cells[i].used) {
        if (g->cells[i].key == key)
            return &g->cells[i];
        i = (i + 1) & (g->ncells - 1);
    }
    if (!make)
        return NULL;
    g->cells[i].used = 1;
    g->cells[i].key = key;
    g->used++;
    return &g->cells[i];
}

static int64_t cell_key(double gx, double gz)
{
    return ((int64_t)gx << 32) ^ (int64_t)(uint32_t)(int32_t)gz;
}

static void ground_free(Ground *g)
{
    size_t i;
    for (i = 0; i < g->ncells; i++)
        free(g->cells[i].idx);
    free(g->cells);
    free(g->tris);
    memset(g, 0, sizeof *g);
}

static void rotate(const double q[4], const double v[3], double r[3])
{
    double x = q[0], y = q[1], z = q[2], w = q[3];
    double tx = 2 * (y * v[2] - z * v[1]), ty = 2 * (z * v[0] - x * v[2]), tz = 2 * (x * v[1] - y * v[0]);
    r[0] = v[0] + w * tx + (y * tz - z * ty);
    r[1] = v[1] + w * ty + (z * tx - x * tz);
    r[2] = v[2] + w * tz + (x * ty - y * tx);
}

typedef struct {
    uint32_t hash;
    double  *tris;               /* 9 each, mesh space */
    int      n;
} Mesh;

static int ground_build(Ctx *c, Ground *g, const List *geo, const List *ins)
{
    Mesh *meshes = (Mesh *)alloc(c, (size_t)(geo->n ? geo->n : 1) * sizeof *meshes);
    int nm = 0, i, j;

    memset(g, 0, sizeof *g);
    for (i = 0; i < geo->n; i++) {
        const Chunk *k = &geo->v[i], *head;
        Mesh *m;
        if (!is(k, "TGEO") || !k->cont || !(head = first(&k->kids, "TGHD")) || head->cont
            || head->size < 64)
            continue;
        m = &meshes[nm++];
        m->hash = u32(head->data + 60);
        m->n = 0;
        m->tris = NULL;
        {
            int cap = 0;
            for (j = 0; j < k->kids.n; j++) {
                const Chunk *part = &k->kids.v[j], *vp = NULL, *fp = NULL;
                int t;
                uint32_t nv, f;
                if (!is(part, "TGPT") || !part->cont)
                    continue;
                for (t = 0; t < part->kids.n; t++) {     /* the last of each, as a dict */
                    if (is(&part->kids.v[t], "TGVP") && !part->kids.v[t].cont)
                        vp = &part->kids.v[t];
                    if (is(&part->kids.v[t], "TGFP") && !part->kids.v[t].cont)
                        fp = &part->kids.v[t];
                }
                if (!vp || !fp)
                    continue;
                nv = vp->size / 12;
                for (f = 0; f < fp->size / 8; f++) {
                    uint16_t a = u16(fp->data + 8 * f + 2), b = u16(fp->data + 8 * f + 4),
                             cc = u16(fp->data + 8 * f + 6);
                    uint16_t idx[3];
                    int e;
                    if (a >= nv || b >= nv || cc >= nv)
                        continue;
                    if (m->n == cap) {
                        double *grown;
                        cap = cap ? cap * 2 : 256;
                        grown = (double *)alloc(c, (size_t)cap * 9 * sizeof *grown);
                        if (m->n)
                            memcpy(grown, m->tris, (size_t)m->n * 9 * sizeof *grown);
                        m->tris = grown;
                    }
                    idx[0] = a; idx[1] = b; idx[2] = cc;
                    for (e = 0; e < 3; e++) {
                        m->tris[m->n * 9 + e * 3 + 0] = f32(vp->data + 12 * idx[e]);
                        m->tris[m->n * 9 + e * 3 + 1] = f32(vp->data + 12 * idx[e] + 4);
                        m->tris[m->n * 9 + e * 3 + 2] = f32(vp->data + 12 * idx[e] + 8);
                    }
                    m->n++;
                }
            }
        }
    }
    for (i = 0; i < ins->n; i++) {
        const Chunk *k = &ins->v[i], *head;
        const Mesh *m = NULL;
        double q[4], pos[3];
        uint32_t hash;
        int t;
        if (!is(k, "TGIN") || !k->cont || !(head = first(&k->kids, "TGIH")) || head->cont
            || head->size < 64)
            continue;
        for (t = 0; t < 4; t++)
            q[t] = f32(head->data + 8 + 4 * t);
        for (t = 0; t < 3; t++)
            pos[t] = f32(head->data + 24 + 4 * t);
        hash = u32(head->data + 60);
        for (t = nm - 1; t >= 0; t--)                    /* the last, as a dict */
            if (meshes[t].hash == hash) {
                m = &meshes[t];
                break;
            }
        if (!m)
            continue;
        for (t = 0; t < m->n; t++) {
            double w[9], e1[3], e2[3], n[3], length, minx, maxx, minz, maxz, gx, gz;
            int e;
            for (e = 0; e < 3; e++) {
                double r[3];
                rotate(q, &m->tris[t * 9 + e * 3], r);
                w[e * 3 + 0] = r[0] + pos[0];
                w[e * 3 + 1] = r[1] + pos[1];
                w[e * 3 + 2] = r[2] + pos[2];
            }
            for (e = 0; e < 3; e++) {
                e1[e] = w[3 + e] - w[e];
                e2[e] = w[6 + e] - w[e];
            }
            n[0] = e1[1] * e2[2] - e1[2] * e2[1];
            n[1] = e1[2] * e2[0] - e1[0] * e2[2];
            n[2] = e1[0] * e2[1] - e1[1] * e2[0];
            length = pow(pow(n[0], 2.0) + pow(n[1], 2.0) + pow(n[2], 2.0), 0.5);   /* as Python: n ** 2 */
            if (length < 1e-9 || fabs(n[1]) < 0.5 * length)
                continue;                                /* walls and slivers */
            if (g->ntri == g->captri) {
                size_t cap = g->captri ? g->captri * 2 : 4096;
                double *grown = (double *)realloc(g->tris, cap * 9 * sizeof *grown);
                if (!grown)
                    return 0;
                g->tris = grown;
                g->captri = cap;
            }
            memcpy(&g->tris[g->ntri * 9], w, sizeof w);
            minx = fmin(fmin(w[0], w[3]), w[6]);
            maxx = fmax(fmax(w[0], w[3]), w[6]);
            minz = fmin(fmin(w[2], w[5]), w[8]);
            maxz = fmax(fmax(w[2], w[5]), w[8]);
            for (gx = floor(minx / CELL); gx <= floor(maxx / CELL); gx++)
                for (gz = floor(minz / CELL); gz <= floor(maxz / CELL); gz++) {
                    Cell *cl = cell_find(g, cell_key(gx, gz), 1);
                    if (!cl)
                        return 0;
                    if (cl->n == cl->cap) {
                        int cap = cl->cap ? cl->cap * 2 : 8;
                        int *grown = (int *)realloc(cl->idx, (size_t)cap * sizeof *grown);
                        if (!grown)
                            return 0;
                        cl->idx = grown;
                        cl->cap = cap;
                    }
                    cl->idx[cl->n++] = (int)g->ntri;
                }
            g->ntri++;
        }
    }
    return 1;
}

/* The highest floor from `above` over the point down to `below` under it
 * (up is -y: the smallest y). 0 if there is none. */
static int ground_floor(Ground *g, double x, double y, double z, double above, double below, double *out)
{
    Cell *cl = cell_find(g, cell_key(floor(x / CELL), floor(z / CELL)), 0);
    int i, found = 0;
    double best = 0;
    if (!cl)
        return 0;
    for (i = 0; i < cl->n; i++) {
        const double *a = &g->tris[cl->idx[i] * 9], *b = a + 3, *c = a + 6;
        double d = (b[0] - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (b[2] - a[2]), u, v, h;
        if (fabs(d) < 1e-12)
            continue;
        u = ((x - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (z - a[2])) / d;
        v = ((b[0] - a[0]) * (z - a[2]) - (x - a[0]) * (b[2] - a[2])) / d;
        if (u < 0 || v < 0 || u + v > 1)
            continue;
        h = a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1]);
        if (y - above <= h && h <= y + below && (!found || h < best)) {
            best = h;
            found = 1;
        }
    }
    *out = best;
    return found;
}

/* The floor a point should stand on: beneath it, else up to 10 m above. */
static int ground_under(Ground *g, double x, double y, double z, double *h)
{
    return ground_floor(g, x, y, z, 0.0, 30.0, h) || ground_floor(g, x, y, z, 10.0, 0.0, h);
}

static Ground *s_ground;

static int snap_spawn(Ctx *c, Chunk *k)
{
    double x, y, z, h;
    const uint8_t *old;
    uint8_t *p;
    if (!s_ground || !is(k, "SDPT") || k->size < 0x1C || u32(k->data + 4) != SDPT_SPAWN)
        return 0;
    x = f32(k->data + 0x10);
    y = f32(k->data + 0x14);
    z = f32(k->data + 0x18);
    if (!ground_under(s_ground, x, y, z, &h) || fabs(h - y) < 0.05)
        return 0;
    old = k->data;
    p = rewrite(c, k, k->size);
    memcpy(p, old, k->size);
    putf(p + 0x14, h);
    return 1;
}

/* ---- the start object ---------------------------------------------------- */

typedef struct {
    int    found;
    double pos[3], rot[4];
} Start;

static void find_start(const List *dat, Start *s)
{
    uint32_t want = name_hash("PLAYERONE");
    int i, t;
    memset(s, 0, sizeof *s);
    for (i = 0; i < dat->n; i++) {
        const Chunk *k = &dat->v[i];
        if (is(k, "SDPT") && !k->cont && k->size >= 0x2C && u32(k->data + 4) == SDPT_SPAWN
            && u32(k->data + 8) == want) {
            s->found = 1;
            for (t = 0; t < 3; t++)
                s->pos[t] = f32(k->data + 0x10 + 4 * t);
            for (t = 0; t < 4; t++)
                s->rot[t] = f32(k->data + 0x1C + 4 * t);
            return;
        }
    }
}

/* The start marker (a TGEO) and object (a TGIN) from the player's Movie Lot. */
static void start_template(Ctx *c, const char *ai_dir, Chunk *marker, Chunk *obj)
{
    char path[MAX_PATH];
    uint32_t want = name_hash("SCRIPTOBJ_PLAYERONE");
    const uint8_t *d;
    size_t n;
    Form geo, ins;
    int i, j, got = 0;

    snprintf(path, sizeof path, "%s\\media\\assets\\levels\\MOVIE\\Movie.GEO", ai_dir);
    d = read_host(c, path, &n);
    read_form(c, d, n, "Movie.GEO", &geo);
    for (i = 0; i < geo.chunks.n && !got; i++) {
        const Chunk *k = &geo.chunks.v[i];
        if (!is(k, "TGEO") || !k->cont)
            continue;
        for (j = 0; j < k->kids.n; j++)
            if (is(&k->kids.v[j], "TGHD") && !k->kids.v[j].cont && k->kids.v[j].size >= 64
                && u32(k->kids.v[j].data + 60) == want) {
                *marker = *k;
                got = 1;
                break;
            }
    }
    if (!got)
        fail(c, "Aggressive Inline's Movie Lot has no start marker.");
    snprintf(path, sizeof path, "%s\\media\\assets\\levels\\MOVIE\\Movie.INS", ai_dir);
    d = read_host(c, path, &n);
    read_form(c, d, n, "Movie.INS", &ins);
    for (i = 0, got = 0; i < ins.chunks.n && !got; i++) {
        const Chunk *k = &ins.chunks.v[i];
        if (!is(k, "TGIN") || !k->cont)
            continue;
        for (j = 0; j < k->kids.n; j++)
            if (is(&k->kids.v[j], "TGIH") && !k->kids.v[j].cont && k->kids.v[j].size >= 64
                && u32(k->kids.v[j].data + 60) == want) {
                *obj = *k;
                got = 1;
                break;
            }
    }
    if (!got)
        fail(c, "Aggressive Inline's Movie Lot has no start object.");
}

static Chunk place_start(Ctx *c, const Chunk *obj, const Chunk *marker, const Start *s)
{
    const Chunk *head = first(&marker->kids, "TGHD");
    double lo[3], hi[3];
    Chunk out = *obj;
    int i, t;

    if (!head || head->cont || head->size < 24)
        fail(c, "Aggressive Inline's start marker is damaged.");
    for (t = 0; t < 3; t++) {
        lo[t] = f32(head->data + 4 * t);
        hi[t] = f32(head->data + 12 + 4 * t);
    }
    memset(&out.kids, 0, sizeof out.kids);
    for (i = 0; i < obj->kids.n; i++) {
        Chunk k = obj->kids.v[i];
        if (is(&k, "TGIH") && !k.cont && k.size >= 60) {
            const uint8_t *old = k.data;
            uint8_t *p = rewrite(c, &k, k.size);
            memcpy(p, old, k.size);
            /* Instance rotation is stored y, x, z, w (Movie Lot's starts). */
            putf(p + 8, s->rot[1]);
            putf(p + 12, s->rot[0]);
            putf(p + 16, s->rot[2]);
            putf(p + 20, s->rot[3]);
            for (t = 0; t < 3; t++) {
                putf(p + 24 + 4 * t, s->pos[t]);
                putf(p + 36 + 4 * t, s->pos[t] + lo[t]);
                putf(p + 48 + 4 * t, s->pos[t] + hi[t]);
            }
        }
        push(c, &out.kids, &k);
    }
    return out;
}

/* ---- collectibles --------------------------------------------------------- */

static int is_collectible(const Chunk *k)
{
    static const char *const prefixes[] = {
        "pickup", "coin", "animobj_pickup", "dynamics_letter_",
        "chip", "banana", "doughnut", "fourty", "lunchbox", "dollarsign",
        "bikepart", "boombox", "boom_box", "musicnote", "soundwave",
        "rotatebox", "innerglow", "outerglow", "checkpoint_", "ball_0" };
    const Chunk *head;
    char name[256];
    uint32_t at, n, i;
    if (!is(k, "TGIN") || !k->cont || !(head = first(&k->kids, "TGIH")) || head->cont)
        return 0;
    at = head->size == 112 ? 80 : 76;
    if (head->size <= at)
        return 0;
    for (n = 0; at + n < head->size && n < sizeof name - 1 && head->data[at + n]; n++)
        name[n] = (char)tolower(head->data[at + n]);
    name[n] = 0;
    for (i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++)
        if (!strncmp(name, prefixes[i], strlen(prefixes[i])))
            return 1;
    return 0;
}

/* ---- a level --------------------------------------------------------------- */

static int ext_is(const char *name, const char *ext)
{
    const char *dot = strrchr(name, '.');
    return dot && !_stricmp(dot, ext);
}

static int by_name(const void *a, const void *b)
{
    return strcmp(((const ArSrcEntry *)a)->name, ((const ArSrcEntry *)b)->name);
}

static void convert_level(Ctx *c, ArSrc *src, const char *src_dir, const char *dst_dir,
                          const char *ai_dir)
{
    ArSrcEntry *names;
    int n, i, have_start = 0;
    char path[MAX_PATH * 2], dst[MAX_PATH];
    Ground ground;
    int have_ground = 0;
    Start start;
    Chunk marker, obj;
    const uint8_t *d;
    size_t size;

    names = ar_src_list(src, src_dir, &n);
    if (!names)
        fail(c, "The game has no folder %s.", src_dir);
    own(c, names);
    qsort(names, (size_t)n, sizeof *names, by_name);
    if (!zx_make_dirs(dst_dir))
        fail(c, "Could not create %s.", dst_dir);

    {
        const char *geo = NULL, *ins = NULL;
        for (i = 0; i < n; i++) {
            if (names[i].dir)
                continue;
            if (!geo && ext_is(names[i].name, ".GEO"))
                geo = names[i].name;
            if (!ins && ext_is(names[i].name, ".INS"))
                ins = names[i].name;
        }
        if (geo && ins) {
            Form fg, fi;
            snprintf(path, sizeof path, "%s/%s", src_dir, geo);
            d = read_src(c, src, path, &size);
            read_level_form(c, d, size, geo, &fg);
            snprintf(path, sizeof path, "%s/%s", src_dir, ins);
            d = read_src(c, src, path, &size);
            read_level_form(c, d, size, ins, &fi);
            if (!ground_build(c, &ground, &fg.chunks, &fi.chunks)) {
                ground_free(&ground);
                fail(c, "Out of memory.");
            }
            have_ground = 1;
        }
    }
    s_ground = have_ground ? &ground : NULL;

    memset(&start, 0, sizeof start);
    for (i = 0; i < n; i++) {
        Form f;
        if (names[i].dir || !ext_is(names[i].name, ".DAT"))
            continue;
        snprintf(path, sizeof path, "%s/%s", src_dir, names[i].name);
        d = read_src(c, src, path, &size);
        read_form(c, d, size, names[i].name, &f);
        find_start(&f.chunks, &start);
    }
    if (start.found && have_ground) {
        double h;
        if (ground_under(&ground, start.pos[0], start.pos[1], start.pos[2], &h))
            start.pos[1] = h;
    }
    if (start.found) {
        start_template(c, ai_dir, &marker, &obj);
        have_start = 1;
    } else
        setup_log("    (no start point: the player starts at the level's centre)");

    for (i = 0; i < n; i++) {
        const char *name = names[i].name;
        Form f;
        if (names[i].dir)
            continue;
        snprintf(path, sizeof path, "%s/%s", src_dir, name);
        snprintf(dst, sizeof dst, "%s\\%s", dst_dir, name);
        d = read_src(c, src, path, &size);
        if (ext_is(name, ".GEO")) {
            read_level_form(c, d, size, name, &f);
            mark_water(c, &f.chunks, f.xxx);
            walk(c, &f.chunks, f.xxx ? mati_xxx : mati_dm2);
            if (!f.xxx)
                walk(c, &f.chunks, tgci);
            walk(c, &f.chunks, surface_attrs);
            if (have_start)
                push(c, &f.chunks, &marker);
            write_form(c, dst, &f);
        } else if (ext_is(name, ".INS")) {
            int k, m = 0;
            read_level_form(c, d, size, name, &f);
            if (!f.xxx)
                walk(c, &f.chunks, tgih);
            for (k = 0; k < f.chunks.n; k++)
                if (!is_collectible(&f.chunks.v[k]))
                    f.chunks.v[m++] = f.chunks.v[k];
            f.chunks.n = m;
            if (have_start) {
                Chunk placed = place_start(c, &obj, &marker, &start);
                push(c, &f.chunks, &placed);
            }
            write_form(c, dst, &f);
        } else if (ext_is(name, ".TXR")) {
            read_level_form(c, d, size, name, &f);
            write_form(c, dst, &f);
        } else if (ext_is(name, ".DAT")) {
            int k, m = 0;
            read_form(c, d, size, name, &f);
            walk(c, &f.chunks, sdpt);
            walk(c, &f.chunks, snap_spawn);
            for (k = 0; k < f.chunks.n; k++)             /* visibility sets: not AI's */
                if (!is(&f.chunks.v[k], "ZPVS"))
                    f.chunks.v[m++] = f.chunks.v[k];
            f.chunks.n = m;
            write_form(c, dst, &f);
        } else if (!zx_write_file(dst, d, size))
            fail(c, "Could not write %s.", dst);
    }
    s_ground = NULL;
    if (have_ground)
        ground_free(&ground);

    /* The level's animation library; the tutorial's, nearly empty, stands in. */
    snprintf(dst, sizeof dst, "%s\\motions.zal", dst_dir);
    if (GetFileAttributesA(dst) == INVALID_FILE_ATTRIBUTES) {
        snprintf(path, sizeof path, "%s\\media\\assets\\levels\\TUT\\motions.zal", ai_dir);
        d = read_host(c, path, &size);
        if (!zx_write_file(dst, d, size))
            fail(c, "Could not write %s.", dst);
    }
}

/* A line in the mod's levels.txt: Free Skate name | scene | folder. */
static int add_level(const char *mod, const char *display, const char *scene, const char *folder)
{
    char path[MAX_PATH], line[1024], kept[64][512];
    int n = 0, i;
    FILE *f;

    snprintf(path, sizeof path, "%s\\levels.txt", mod);
    f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof line, f) && n < 63) {
            char *bar1, *bar2, field[256], *s, *e;
            line[strcspn(line, "\r\n")] = 0;
            for (s = line; *s == ' ' || *s == '\t'; s++)
                ;
            if (!*s || line[0] == '#')
                continue;
            bar1 = strchr(line, '|');
            bar2 = bar1 ? strchr(bar1 + 1, '|') : NULL;
            if (bar1) {
                size_t len = (size_t)((bar2 ? bar2 : line + strlen(line)) - (bar1 + 1));
                if (len >= sizeof field)
                    len = sizeof field - 1;
                memcpy(field, bar1 + 1, len);
                field[len] = 0;
                for (s = field; *s == ' ' || *s == '\t'; s++)
                    ;
                for (e = s + strlen(s); e > s && (e[-1] == ' ' || e[-1] == '\t'); e--)
                    ;
                *e = 0;
                if (!strcmp(s, scene))
                    continue;
            }
            snprintf(kept[n++], sizeof kept[0], "%s", line);
        }
        fclose(f);
    }
    f = fopen(path, "w");
    if (!f)
        return 0;
    fprintf(f, "# Levels this mod adds: Free Skate name | scene | folder below assets/levels\n");
    for (i = 0; i < n; i++)
        fprintf(f, "%s\n", kept[i]);
    fprintf(f, "%s | %s | %s\n", display, scene, folder);
    return fclose(f) == 0;
}

int zx_port_level(ArSrc *src, const char *tag, const char *level, const char *mod_dir,
                  const char *display, const char *ai_dir, char *err, size_t errn)
{
    static Ctx c;                /* one level at a time */
    char src_dir[MAX_PATH], folder[MAX_PATH], scene[MAX_PATH], dst[MAX_PATH], path[MAX_PATH];
    ArSrcEntry *levels;
    int n, i, k = 0;
    const uint8_t *d;
    size_t size;

    memset(&c, 0, sizeof c);
    c.err = err;
    c.errn = errn;
    if (setjmp(c.fail)) {
        s_ground = NULL;
        ctx_free(&c);
        return 0;
    }
    levels = ar_src_list(src, "media/assets/levels", &n);
    if (!levels)
        fail(&c, "The game has no media/assets/levels folder.");
    own(&c, levels);
    for (i = 0; i < n; i++)
        if (levels[i].dir && !_stricmp(levels[i].name, level))
            break;
    if (i == n)
        fail(&c, "The game has no level %s.", level);
    snprintf(src_dir, sizeof src_dir, "media/assets/levels/%s", levels[i].name);
    snprintf(folder, sizeof folder, "%s_%s", tag, levels[i].name);
    for (i = 0; folder[i]; i++) {
        folder[i] = (char)tolower((unsigned char)folder[i]);
        if (folder[i] != '_')
            scene[k++] = (char)toupper((unsigned char)folder[i]);
    }
    scene[k] = 0;
    snprintf(dst, sizeof dst, "%s\\media\\assets\\levels\\%s", mod_dir, folder);
    convert_level(&c, src, src_dir, dst, ai_dir);

    /* The level's challenge scripts, <scene>.bin and <scene>T.bin, read from
     * its folder by name; free roam has none: the Zoo's empty one stands in. */
    snprintf(path, sizeof path, "%s\\media\\assets\\levels\\ZOO\\ZOO.bin", ai_dir);
    d = read_host(&c, path, &size);
    snprintf(path, sizeof path, "%s\\%s.bin", dst, scene);
    if (!zx_write_file(path, d, size))
        fail(&c, "Could not write %s.", path);
    snprintf(path, sizeof path, "%s\\%sT.bin", dst, scene);
    if (!zx_write_file(path, d, size))
        fail(&c, "Could not write %s.", path);
    if (!add_level(mod_dir, display, scene, folder))
        fail(&c, "Could not write %s\\levels.txt.", mod_dir);
    ctx_free(&c);
    return 1;
}
