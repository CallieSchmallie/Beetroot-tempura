/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Beetroot Tempura: canvases in Flash, plus platform glue.
 *
 * Canvas N (1-8) lives in user-Flash sector N-1, the sector FM1 Open uses
 * for project slot N. Each record carries a CRC and a commit word written
 * last, so an interrupted save reads back as empty rather than corrupt.
 * The sample bank, settings sector and staging sector are never touched. */
/* Control-task code: optimise for size (the FM-1 app area is about 444 KB). */
/* Size-optimised: TP_SMALL on each function (the FM-1's clang 4 has no
 * "#pragma clang attribute", which earlier builds relied on). */
#include "tempura.h"
#include <string.h>
#ifndef FM1_HOST_TEST
#include "fm1_device.h"
#endif

#define STORE_MAGIC 0x53525442U /* "BTRS" */
struct record_head { uint32_t magic, crc, commit; };
_Static_assert(sizeof(struct record_head) + sizeof(struct tp_canvas) <= FM1_FLASH_SECTOR_BYTES, "canvas fits one sector");

TP_SMALL static int read_head(unsigned slot, struct record_head *h)
{
    if (slot >= TP_SLOTS || !fm1_flash_size()) return -1;
    int r = fm1_flash_read(slot * FM1_FLASH_SECTOR_BYTES, h, sizeof *h);
    if (r) return -1;
    if (h->magic != STORE_MAGIC || h->commit != 0) return 1;
    return 0;
}
TP_SMALL int tempura_store_load(unsigned slot, struct tp_canvas *c)
{
    struct record_head h;
    int r = read_head(slot, &h);
    if (r) return r;
    if (fm1_flash_read(slot * FM1_FLASH_SECTOR_BYTES + sizeof h, c, sizeof *c)) return -1;
    /* A canvas saved by an older version is shorter: its CRC covers only its
     * own length, and the settings added since then take their defaults. */
    unsigned bytes = c->bytes;
    if (c->magic != TP_CANVAS_MAGIC || bytes < offsetof(struct tp_canvas, g) || bytes > sizeof *c ||
        tempura_crc32(0, c, bytes) != h.crc) return 1;
    if (bytes < sizeof *c) {
        static struct tp_canvas fresh;
        tempura_canvas_default(&fresh);
        memcpy(&fresh, c, bytes);
        *c = fresh;
    }
    tempura_canvas_sanitize(c);
    return 0;
}
TP_SMALL int tempura_store_name(unsigned slot, char name[13])
{
    struct tp_canvas c;
    if (tempura_store_load(slot, &c)) return 1;
    memcpy(name, c.name, 12);
    name[12] = 0;
    return 0;
}
TP_SMALL int tempura_store_save(unsigned slot, const struct tp_canvas *c)
{
    if (slot >= TP_SLOTS || !fm1_flash_size()) return -1;
    static struct tp_canvas copy; /* stable input while Flash is written */
    copy = *c;
    struct record_head h = {STORE_MAGIC, tempura_crc32(0, &copy, sizeof copy), 0xffffffffU};
    uint32_t base = slot * FM1_FLASH_SECTOR_BYTES;
    int r = fm1_flash_erase(base);
    if (!r) r = fm1_flash_write(base, &h, sizeof h);
    if (!r) r = fm1_flash_write(base + sizeof h, &copy, sizeof copy);
    uint32_t commit = 0;
    if (!r) r = fm1_flash_write(base + offsetof(struct record_head, commit), &commit, sizeof commit);
    return r ? -1 : 0;
}

/* Brief exclusion of the audio interrupt while pointers are swapped. */
#ifdef FM1_HOST_TEST
TP_SMALL void tempura_lock(void) {}
TP_SMALL void tempura_unlock(void) {}
#else
static uint32_t lock_state;
static unsigned lock_depth;
TP_SMALL void tempura_lock(void)
{
    uint32_t s = fm1_device_irq_save();
    if (lock_depth++ == 0) lock_state = s;
}
TP_SMALL void tempura_unlock(void)
{
    if (lock_depth && --lock_depth == 0) fm1_device_irq_restore(lock_state);
}
#endif

