/*
 * A game's files from a folder or a disc image (setup_src.h).
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "setup_src.h"
#include "../xiso.h"

struct ArSrc {
    DrXiso *iso;                 /* a disc image, or NULL for a folder */
    char    folder[MAX_PATH];
};

static int ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && !_stricmp(s + a - b, tail);
}

ArSrc *ar_src_open(const char *path, char *err, size_t errn)
{
    ArSrc *s = (ArSrc *)calloc(1, sizeof *s);
    char full[MAX_PATH], xbe[MAX_PATH];
    DWORD attr;

    if (!s) {
        snprintf(err, errn, "Out of memory.");
        return NULL;
    }
    if (!GetFullPathNameA(path, sizeof full, full, NULL))
        snprintf(full, sizeof full, "%s", path);
    attr = GetFileAttributesA(full);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        snprintf(err, errn, "%s was not found.", full);
        free(s);
        return NULL;
    }
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY) && ends_with(full, ".xbe")) {
        char *slash = strrchr(full, '\\');
        if (slash)
            *slash = 0;
        attr = FILE_ATTRIBUTE_DIRECTORY;
    }
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        snprintf(xbe, sizeof xbe, "%s\\default.xbe", full);
        if (GetFileAttributesA(xbe) == INVALID_FILE_ATTRIBUTES) {
            snprintf(err, errn, "%s has no default.xbe: choose the folder with the "
                     "game's default.xbe and media folder in it.", full);
            free(s);
            return NULL;
        }
        snprintf(s->folder, sizeof s->folder, "%s", full);
        return s;
    }
    s->iso = ar_xiso_open(full, err, errn);
    if (!s->iso) {
        free(s);
        return NULL;
    }
    return s;
}

void ar_src_close(ArSrc *s)
{
    if (!s)
        return;
    ar_xiso_close(s->iso);
    free(s);
}

static void host_path(ArSrc *s, const char *rel, char *out, size_t n)
{
    char *p;
    snprintf(out, n, "%s\\%s", s->folder, rel);
    for (p = out; *p; p++)
        if (*p == '/')
            *p = '\\';
}

ArSrcEntry *ar_src_list(ArSrc *s, const char *dir, int *count)
{
    ArSrcEntry *out = NULL;
    int n = 0, cap = 0, i;

    *count = 0;
    if (s->iso) {
        ArXisoEntry *es = ar_xiso_list(s->iso, dir, &n);
        if (!es)
            return NULL;
        out = (ArSrcEntry *)calloc((size_t)n, sizeof *out);
        for (i = 0; out && i < n; i++) {
            memcpy(out[i].name, es[i].name, sizeof out[i].name);
            out[i].dir = es[i].dir;
        }
        free(es);
        *count = out ? n : 0;
        return out;
    } else {
        char pattern[MAX_PATH];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        host_path(s, dir, pattern, sizeof pattern - 2);
        strcat(pattern, "\\*");
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE)
            return NULL;
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
                continue;
            if (n == cap) {
                ArSrcEntry *grown;
                cap = cap ? cap * 2 : 32;
                grown = (ArSrcEntry *)realloc(out, (size_t)cap * sizeof *out);
                if (!grown)
                    break;
                out = grown;
            }
            snprintf(out[n].name, sizeof out[n].name, "%s", fd.cFileName);
            out[n].dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            n++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        *count = n;
        return out;
    }
}

void *ar_src_read(ArSrc *s, const char *path, size_t *size)
{
    char full[MAX_PATH];
    HANDLE f;
    LARGE_INTEGER sz;
    void *buf = NULL;
    DWORD got = 0;

    *size = 0;
    if (s->iso)
        return ar_xiso_read_file(s->iso, path, size);
    host_path(s, path, full, sizeof full);
    f = CreateFileA(full, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
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

uint32_t ar_src_title_id(ArSrc *s)
{
    size_t size;
    uint8_t *x = (uint8_t *)ar_src_read(s, "default.xbe", &size);
    uint32_t base, cert, id = 0;

    if (x && size >= 0x120 && !memcmp(x, "XBEH", 4)) {
        memcpy(&base, x + 0x104, 4);
        memcpy(&cert, x + 0x118, 4);
        if (cert >= base && cert - base + 12 <= size)
            memcpy(&id, x + (cert - base) + 8, 4);
    }
    free(x);
    return id;
}

const char *ar_src_folder(ArSrc *s)
{
    return s->iso ? "" : s->folder;
}
