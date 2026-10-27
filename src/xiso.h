/*
 * Reading an Xbox disc image (XDVDFS), for the first-run game import
 * (game_import.c): a release build must take the player's .iso without any
 * other tool installed. Read-only; no video partition, no timestamps.
 */
#ifndef AR_XISO_H
#define AR_XISO_H

#include <stddef.h>
#include <stdint.h>

typedef struct DrXiso DrXiso;

/* Open an image. NULL on failure, with a reason for the player in err. */
DrXiso *ar_xiso_open(const char *path, char *err, size_t errn);
void ar_xiso_close(DrXiso *x);

/* A file in the image's root (e.g. "default.xbe"), read whole into a malloc'd
 * buffer. NULL if absent or unreadable. */
void *ar_xiso_read_root_file(DrXiso *x, const char *name, size_t *size);

/* A folder's entries by path from the root ("media/assets/levels"; '/' or
 * '\\', any case), malloc'd; NULL with *count 0 if there is no such folder. */
typedef struct {
    char     name[256];
    int      dir;
    uint32_t size;
} ArXisoEntry;
ArXisoEntry *ar_xiso_list(DrXiso *x, const char *dir, int *count);

/* A file by path from the root, read whole into a malloc'd buffer; NULL if
 * absent or unreadable. */
void *ar_xiso_read_file(DrXiso *x, const char *path, size_t *size);

/* Total bytes of every file in the image. */
uint64_t ar_xiso_total_bytes(DrXiso *x);

/* Extract every file under `dest` (created as needed), calling `progress`
 * after each chunk; a non-zero return from it cancels. 0 on success; on
 * failure or cancel a reason goes in err. */
typedef int (*DrXisoProgress)(void *ctx, uint64_t done, uint64_t total);
int ar_xiso_extract_all(DrXiso *x, const char *dest, DrXisoProgress progress,
                        void *ctx, char *err, size_t errn);

#endif /* AR_XISO_H */
