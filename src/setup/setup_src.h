/*
 * A game the player gave the setup (aggro_setup.exe): its files, from a
 * folder or straight out of a disc image, by path from the disc's root
 * ("media/assets/levels"). Read-only.
 */
#ifndef AR_SETUP_SRC_H
#define AR_SETUP_SRC_H

#include <stddef.h>
#include <stdint.h>

typedef struct ArSrc ArSrc;

typedef struct {
    char name[256];
    int  dir;
} ArSrcEntry;

/* A folder holding default.xbe, a default.xbe itself, or a disc image.
 * NULL with a reason in err if it is none of those. */
ArSrc *ar_src_open(const char *path, char *err, size_t errn);
void ar_src_close(ArSrc *s);

/* The folder's entries, malloc'd (NULL and 0 if there is no such folder). */
ArSrcEntry *ar_src_list(ArSrc *s, const char *dir, int *count);

/* A whole file, malloc'd; NULL if absent or unreadable. */
void *ar_src_read(ArSrc *s, const char *path, size_t *size);

/* The title ID in default.xbe's certificate; 0 if unreadable. */
uint32_t ar_src_title_id(ArSrc *s);

/* The folder the files are in, or "" for a disc image. */
const char *ar_src_folder(ArSrc *s);

#endif /* AR_SETUP_SRC_H */
