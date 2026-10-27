/*
 * The Free Skate menu for added levels (zaxis.h): tools/build_level_menu.py
 * in C, writing the same bytes. mods\!levels-menu holds the game's
 * Scene.txt with a SCENE per added level, its front-end menus (uilib.zal)
 * with a Free Skate page per added level, and levels.txt, which the game
 * reads at start (recomp_manual.c, sub_00050050).
 */
#include <windows.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zaxis.h"

typedef struct {
    char name[128], scene[64], folder[128], zit[128];
} Level;

typedef struct {
    uint8_t *p;
    size_t   n, cap;
    int      oom;
} Buf;

static void put(Buf *b, const void *d, size_t n)
{
    if (b->oom)
        return;
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        uint8_t *grown;
        while (cap < b->n + n)
            cap *= 2;
        grown = (uint8_t *)realloc(b->p, cap);
        if (!grown) {
            b->oom = 1;
            return;
        }
        b->p = grown;
        b->cap = cap;
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}

static void puts_(Buf *b, const char *s)
{
    put(b, s, strlen(s));
}

static void putf_(Buf *b, const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    puts_(b, line);
}

/* The first `pat` in d[from:to], or -1. */
static long find(const uint8_t *d, size_t from, size_t to, const char *pat)
{
    size_t n = strlen(pat), i;
    if (to < n)
        return -1;
    for (i = from; i + n <= to; i++)
        if (d[i] == (uint8_t)pat[0] && !memcmp(d + i, pat, n))
            return (long)i;
    return -1;
}

static long rfind(const uint8_t *d, size_t from, size_t to, const char *pat)
{
    size_t n = strlen(pat), i;
    if (to < from + n)
        return -1;
    for (i = to - n + 1; i-- > from;)
        if (!memcmp(d + i, pat, n))
            return (long)i;
    return -1;
}

static void trim(char *s)
{
    char *a = s, *e;
    while (isspace((unsigned char)*a))
        a++;
    memmove(s, a, strlen(a) + 1);
    for (e = s + strlen(s); e > s && isspace((unsigned char)e[-1]); e--)
        ;
    *e = 0;
}

static int by_dir_name(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

#define MAX_LEVELS 256

static int read_levels(const char *mods, Level *out)
{
    char pattern[MAX_PATH], *names[512];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int nn = 0, n = 0, i;

    snprintf(pattern, sizeof pattern, "%s\\*", mods);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.'
            && fd.cFileName[0] != '_' && fd.cFileName[0] != '!' && nn < 512)
            names[nn++] = _strdup(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    qsort(names, (size_t)nn, sizeof names[0], by_dir_name);
    for (i = 0; i < nn; i++) {
        char path[MAX_PATH], line[1024];
        FILE *f;
        snprintf(path, sizeof path, "%s\\%s\\levels.txt", mods, names[i]);
        f = fopen(path, "r");
        while (f && fgets(line, sizeof line, f) && n < MAX_LEVELS) {
            char *a, *b, dir[MAX_PATH];
            Level *l = &out[n];
            line[strcspn(line, "\r\n")] = 0;
            if (!line[0] || line[0] == '#')
                continue;
            a = strchr(line, '|');
            b = a ? strchr(a + 1, '|') : NULL;
            if (!b)
                continue;
            *a = *b = 0;
            snprintf(l->name, sizeof l->name, "%s", line);
            snprintf(l->scene, sizeof l->scene, "%s", a + 1);
            snprintf(l->folder, sizeof l->folder, "%s", b + 1);
            trim(l->name);
            trim(l->scene);
            trim(l->folder);
            if (!l->name[0])
                continue;
            snprintf(dir, sizeof dir, "%s\\%s\\media\\assets\\levels\\%s\\*.zit", mods, names[i],
                     l->folder);
            h = FindFirstFileA(dir, &fd);
            if (h == INVALID_HANDLE_VALUE) {
                setup_log("  %s: its level files are missing, left out", l->name);
                continue;
            }
            snprintf(l->zit, sizeof l->zit, "%s", fd.cFileName);
            FindClose(h);
            n++;
        }
        if (f)
            fclose(f);
        free(names[i]);
    }
    return n;
}

static int scene_txt(const char *ai, const Level *lv, int n, Buf *out)
{
    char path[MAX_PATH];
    size_t size, end;
    uint8_t *t;
    long at;
    int i;

    snprintf(path, sizeof path, "%s\\media\\Scene.txt", ai);
    t = (uint8_t *)zx_read_file(path, &size);
    if (!t)
        return 0;
    for (end = size; end > 0 && isspace(t[end - 1]); end--)
        ;
    /* The game stops reading at the file's last line, END: the added
     * scenes go before it. */
    at = rfind(t, 0, end, "\nEND");
    if (at < 0) {
        free(t);
        return 0;
    }
    put(out, t, (size_t)at + 1);
    for (i = 0; i < n; i++)
        putf_(out, "SCENE\r\n{\r\n\tNAME\t\"%s\"\r\n\tFILE\t\"assets\\levels\\%s\\%s\"\r\n}\r\n",
              lv[i].scene, lv[i].folder, lv[i].zit);
    put(out, t + at + 1, size - (size_t)at - 1);
    free(t);
    return !out->oom;
}

#define T6 "\t\t\t\t\t\t"
#define PAGES_PER_ACTION 8

/* The Free Skate page script, with a page for each added level. */
static int patch_freeskate(const uint8_t *text, size_t size, const Level *lv, int n, Buf *out)
{
    long start = find(text, 0, size, "ACTION \"Script\" \"FreeskateOnChange\"");
    long end = start < 0 ? -1 : find(text, (size_t)start, size, "ACTION \"Script\" \"MultiplayerOnChange\"");
    Buf block = { 0 }, b2 = { 0 }, actions = { 0 };
    size_t at;
    long k, done, line;
    int g, groups, i, m, visible;
    const char *env = getenv("AR_MENU_VISIBLE");
    char from[64], to[64];

    if (start < 0 || end < 0)
        return 0;

    /* Every Xbox icon row (the ones with Zoo's "s") gains the added levels. */
    at = (size_t)start;
    for (;;) {
        long i0 = find(text, at, (size_t)end, "SET Items {\""), j;
        if (i0 < 0)
            break;
        j = find(text, (size_t)i0, (size_t)end, "\"}");
        if (j < 0)
            return 0;
        put(&block, text + at, (size_t)j - at);
        if (find(text, (size_t)i0 + 12, (size_t)j, "\\ns") >= 0
            || find(text, (size_t)i0 + 12, (size_t)j, "\\nS") >= 0)
            for (i = 0; i < n; i++)
                puts_(&block, "\\na");
        at = (size_t)j;
    }
    put(&block, text + at, (size_t)end - at);

    /* VisibleCount icons show: up to ten. */
    visible = env && atoi(env) ? atoi(env) : (9 + n < 10 ? 9 + n : 10);
    snprintf(from, sizeof from, "SET VisibleCount {9}");
    snprintf(to, sizeof to, "SET VisibleCount {%d}", visible);
    at = 0;
    for (;;) {
        long r = find(block.p, at, block.n, from);
        if (r < 0)
            break;
        put(&b2, block.p + at, (size_t)r - at);
        puts_(&b2, to);
        at = (size_t)r + strlen(from);
    }
    put(&b2, block.p + at, block.n - at);
    free(block.p);
    block = b2;
    memset(&b2, 0, sizeof b2);

    /* Pages 9 on go to the AddedLevels actions, eight pages each. */
    groups = (n + PAGES_PER_ACTION - 1) / PAGES_PER_ACTION;
    k = find(block.p, 0, block.n, T6 "GOTO SelectedIndex");
    if (k < 0)
        return 0;
    put(&b2, block.p, (size_t)k);
    puts_(&b2, T6 "IF SelectedIndex > {8} GOTO AddedLevels\r\n");
    put(&b2, block.p + k, block.n - (size_t)k);
    free(block.p);
    block = b2;
    memset(&b2, 0, sizeof b2);

    done = find(block.p, 0, block.n, ":Done");
    if (done < 0)
        return 0;
    line = rfind(block.p, 0, (size_t)done, "\n") + 1;
    put(&b2, block.p, (size_t)line);
    puts_(&b2, T6 ":AddedLevels\r\n");
    for (g = 0; g < groups; g++) {
        if (g + 1 < groups)
            putf_(&b2, T6 "IF SelectedIndex > {%d} GOTO AddedLevels%d\r\n",
                  8 + (g + 1) * PAGES_PER_ACTION, g + 2);
        putf_(&b2, T6 "PLAY AddedLevels%d\r\n" T6 "GOTO Complete\r\n", g + 1);
        if (g + 1 < groups)
            putf_(&b2, T6 ":AddedLevels%d\r\n", g + 2);
    }
    puts_(&b2, "\r\n");
    put(&b2, block.p + line, block.n - (size_t)line);
    free(block.p);
    block = b2;

    for (g = 0; g < groups; g++) {
        Buf pages = { 0 };
        puts_(&pages, T6 "GOTO SelectedIndex\r\n");
        for (m = 0; m < PAGES_PER_ACTION && g * PAGES_PER_ACTION + m < n; m++) {
            int idx = g * PAGES_PER_ACTION + m, r;
            char upper[128];
            for (r = 0; lv[idx].name[r] && r < (int)sizeof upper - 1; r++)
                upper[r] = (char)toupper((unsigned char)lv[idx].name[r]);
            upper[r] = 0;
            putf_(&pages, T6 ":%d\r\n", 9 + idx);
            putf_(&pages, T6 "SET .SelectedLevel {\"%s\"}\r\n", lv[idx].scene);
            putf_(&pages, T6 "SET .LevelData.LevelName.TextLabel.Text {\"%s\"}\r\n", upper);
            puts_(&pages, T6 "SET Items {\"o\\nr\\nu\\nw\\ne\\nq\\np\\ns\\ni");
            for (r = 0; r < n; r++)
                puts_(&pages, r == idx ? "\\nA" : "\\na");
            puts_(&pages, "\"}\r\n" T6 "GOTO Done\r\n");
        }
        puts_(&pages, T6 ":Done\r\n" T6 "CALL inlineMenuPopulateLevelData\r\n"
                      T6 "ABORT StartFlythruMovie\r\n" T6 "CALL inlineMenuStopMovie\r\n"
                      T6 "SET Root.FrontEnd.Movie.Material {\"FrontEnd.still_bkg_01\"}\r\n");
        putf_(&actions, "ACTION \"Script\" \"AddedLevels%d\"\r\n", g + 1);
        puts_(&actions, "\t\t\t\t{\r\n\t\t\t\t\t//////////////////////////////\r\n"
                        "\t\t\t\t\t// Property\r\n\t\t\t\t\tStartDelay\t0\r\n\t\t\t\t\tScript\t{\r\n");
        put(&actions, pages.p, pages.n);
        puts_(&actions, "\t\t\t\t\t\t}\r\n\t\t\t\t}\r\n\t\t\t\t");
        free(pages.p);
    }

    put(out, text, (size_t)start);
    put(out, block.p, block.n);
    put(out, actions.p, actions.n);
    put(out, text + end, size - (size_t)end);
    free(block.p);
    free(actions.p);
    return !out->oom;
}

/* uilib.zal: ZDIR {DIDF count, GRID name hashes, DLEN flags<<28 | length,
 * OFST offsets}, then the entries, each at a 16-byte boundary. Both copies
 * of the front end's script (entries 23 and 26) are patched. */
static int uilib(const char *ai, const Level *lv, int n, Buf *out)
{
    char path[MAX_PATH];
    size_t size, head_n, pos;
    uint8_t *d, *head;
    uint32_t count, k, *dlen, *ofst, *placed, *order;
    Buf *entries;
    int ok = 1;

    snprintf(path, sizeof path, "%s\\media\\assets\\uilib.zal", ai);
    d = (uint8_t *)zx_read_file(path, &size);
    if (!d || size < 32 || memcmp(d, "ZDIR", 4)) {
        free(d);
        return 0;
    }
    memcpy(&count, d + 16, 4);
    head_n = (size_t)*(uint32_t *)(d + 4) + 8;
    if (!count || count > 100000 || head_n > size || 32 + 12 * (size_t)count + 16 > head_n) {
        free(d);
        return 0;
    }
    dlen = (uint32_t *)malloc(count * 4);
    ofst = (uint32_t *)malloc(count * 4);
    placed = (uint32_t *)malloc(count * 4);
    order = (uint32_t *)malloc(count * 4);
    entries = (Buf *)calloc(count, sizeof *entries);
    head = (uint8_t *)malloc(head_n);
    if (!dlen || !ofst || !placed || !order || !entries || !head)
        ok = 0;
    if (ok) {
        memcpy(dlen, d + 32 + 4 * count + 8, count * 4);
        memcpy(ofst, d + 32 + 8 * count + 16, count * 4);
        memcpy(head, d, head_n);
        for (k = 0; ok && k < count; k++) {
            size_t len = dlen[k] & 0x0FFFFFFFu;
            if (ofst[k] > size || len > size - ofst[k]) {
                ok = 0;
                break;
            }
            if (find(d + ofst[k], 0, len, "ACTION \"Script\" \"FreeskateOnChange\"") >= 0)
                ok = patch_freeskate(d + ofst[k], len, lv, n, &entries[k]);
            else
                put(&entries[k], d + ofst[k], len);
            ok = ok && !entries[k].oom;
        }
    }
    if (ok) {
        uint32_t i, j;
        /* The entries in their original order on disc (a stable sort). */
        for (k = 0; k < count; k++)
            order[k] = k;
        for (i = 1; i < count; i++)
            for (j = i; j > 0 && ofst[order[j - 1]] > ofst[order[j]]; j--) {
                uint32_t t = order[j];
                order[j] = order[j - 1];
                order[j - 1] = t;
            }
        pos = (head_n + 15) & ~(size_t)15;
        for (i = 0; i < count; i++) {
            placed[order[i]] = (uint32_t)pos;
            pos = (pos + entries[order[i]].n + 15) & ~(size_t)15;
        }
        for (k = 0; k < count; k++) {
            uint32_t len = (dlen[k] & 0xF0000000u) | (uint32_t)entries[k].n;
            memcpy(head + 32 + 4 * count + 8 + 4 * k, &len, 4);
            memcpy(head + 32 + 8 * count + 16 + 4 * k, &placed[k], 4);
        }
        put(out, head, head_n);
        for (i = 0; i < count; i++) {
            static const uint8_t zeros[16];
            while (out->n < placed[order[i]])
                put(out, zeros, placed[order[i]] - out->n > 16 ? 16 : placed[order[i]] - out->n);
            put(out, entries[order[i]].p, entries[order[i]].n);
        }
        while (out->n & 15)
            put(out, "\0", 1);
        ok = !out->oom;
    }
    for (k = 0; entries && k < count; k++)
        free(entries[k].p);
    free(entries);
    free(dlen);
    free(ofst);
    free(placed);
    free(order);
    free(head);
    free(d);
    return ok;
}

int zx_build_level_menu(const char *mods, const char *ai, char *err, size_t errn)
{
    static Level lv[MAX_LEVELS];
    char out[MAX_PATH], path[MAX_PATH];
    Buf scene = { 0 }, ui = { 0 };
    FILE *f;
    int n = read_levels(mods, lv), i, ok;

    snprintf(out, sizeof out, "%s\\!levels-menu", mods);
    zx_remove_tree(out);
    if (!n)
        return 0;
    snprintf(path, sizeof path, "%s\\media\\assets", out);
    if (!zx_make_dirs(path)) {
        snprintf(err, errn, "Could not create %s.", path);
        return -1;
    }
    ok = scene_txt(ai, lv, n, &scene);
    if (!ok)
        snprintf(err, errn, "Could not add the levels to the game's Scene.txt.");
    else if (!(ok = uilib(ai, lv, n, &ui)))
        snprintf(err, errn, "Could not add the levels to the game's menus (uilib.zal).");
    if (ok) {
        snprintf(path, sizeof path, "%s\\media\\Scene.txt", out);
        ok = zx_write_file(path, scene.p, scene.n);
        snprintf(path, sizeof path, "%s\\media\\assets\\uilib.zal", out);
        ok = ok && zx_write_file(path, ui.p, ui.n);
        snprintf(path, sizeof path, "%s\\levels.txt", out);
        f = ok ? fopen(path, "w") : NULL;
        if (f) {
            fprintf(f, "# Built by aggro_setup: Free Skate name | scene | folder\n");
            for (i = 0; i < n; i++)
                fprintf(f, "%s | %s | %s\n", lv[i].name, lv[i].scene, lv[i].folder);
            ok = fclose(f) == 0;
        } else
            ok = 0;
        if (!ok)
            snprintf(err, errn, "Could not write the level menu into %s.", out);
    }
    free(scene.p);
    free(ui.p);
    if (!ok) {
        zx_remove_tree(out);
        return -1;
    }
    for (i = 0; i < n; i++)
        setup_log("  Free Skate page %d: %s", 9 + i, lv[i].name);
    return n;
}
