/* image.c: the game's data object, read from the player's WW.EXE and mapped where the game had it.
 *
 * WW.EXE is a DOS/4GW-bound LE executable (findings/executables.md). Object 2 (data, linear
 * 0x60000, vsize 0x33100) is laid out from its pages and its internal fixups applied, the same
 * rule as tools/le/leparse.py: a 32-bit offset fixup gets the target object's base plus the
 * offset. src/ww.c's globals are bound to these addresses by port/gen/syms.ld, so the C module
 * sees the data exactly as the game's code did. The 25 fixups in the data object that point at
 * code all point into the clib and the sound library (port/README.md), which the port replaces,
 * so their values are left as the game's addresses. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "machine.h"

static uint32_t u32(const uint8_t *b, size_t o) { return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }
static uint16_t u16(const uint8_t *b, size_t o) { return b[o] | b[o + 1] << 8; }

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc(*len);
    if (fread(b, 1, *len, f) != *len) {
        free(b);
        b = NULL;
    }
    fclose(f);
    return b;
}

/* the LE header: at e_lfanew, or after the DOS/4GW stub (a second MZ further in, "bound") */
static size_t find_le(const uint8_t *d, size_t len, size_t *base)
{
    size_t x, l = u32(d, 0x3c);

    *base = 0;
    if (l + 2 < len && d[l] == 'L' && d[l + 1] == 'E')
        return l;
    for (x = 2; x + 0x40 < len; x += 2) {
        if (d[x] != 'M' || d[x + 1] != 'Z')
            continue;
        l = u32(d, x + 0x3c);
        if (l > 0 && x + l + 2 < len && d[x + l] == 'L' && d[x + l + 1] == 'E') {
            *base = x;
            return x + l;
        }
    }
    return 0;
}

int image_load(const char *exe)
{
    size_t len, bound, h, objtab, pmap, fpages, frecs, dpages, psize, npages, lastpage;
    uint8_t *file = slurp(exe, &len), *d, *img;
    uint32_t base[2], vsize[2], pidx[2], pcnt[2];
    unsigned k, i;
    long nfix = 0;

    if (!file) {
        fprintf(stderr, "port: cannot read %s\n", exe);
        return -1;
    }
    h = find_le(file, len, &bound);
    if (!h) {
        fprintf(stderr, "port: %s has no LE header\n", exe);
        return -1;
    }
    d = file + bound;
    h -= bound;
    npages = u32(d, h + 0x14);
    psize = u32(d, h + 0x28);
    lastpage = u32(d, h + 0x2c);
    objtab = h + u32(d, h + 0x40);
    pmap = h + u32(d, h + 0x48);
    fpages = h + u32(d, h + 0x68);
    frecs = h + u32(d, h + 0x6c);
    dpages = u32(d, h + 0x80);           /* from the start of the LE's own MZ */
    if (u32(d, h + 0x44) != 2) {
        fprintf(stderr, "port: %s has %u objects, not WW.EXE 1.1's 2\n", exe, u32(d, h + 0x44));
        return -1;
    }
    for (i = 0; i < 2; i++) {
        vsize[i] = u32(d, objtab + 24 * i);
        base[i] = u32(d, objtab + 24 * i + 4);
        pidx[i] = u32(d, objtab + 24 * i + 12);
        pcnt[i] = u32(d, objtab + 24 * i + 16);
    }
    if (base[1] != DATA_BASE || vsize[1] != DATA_SIZE) {
        fprintf(stderr, "port: data object at %#x size %#x, expected %#x size %#x (not WW.EXE 1.1?)\n",
                base[1], vsize[1], DATA_BASE, DATA_SIZE);
        return -1;
    }
    img = mmap((void *)DATA_BASE, DATA_MAP, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (img != (uint8_t *)DATA_BASE) {
        perror("port: mmap of the data object at 0x60000");
        return -1;
    }
    for (k = 0; k < pcnt[1]; k++) {
        unsigned page = pidx[1] + k;                 /* 1-based logical page */
        size_t e = pmap + 4 * (page - 1), off, n;
        unsigned num = d[e] << 16 | d[e + 1] << 8 | d[e + 2];

        off = dpages + (num - 1) * psize;
        n = page == npages ? lastpage : psize;
        if (k * psize + n > vsize[1])
            n = vsize[1] - k * psize;
        memcpy(img + k * psize, d + off, n);
    }
    /* all pages first: a fixup that crosses into the next page must not be overwritten by its copy */
    for (k = 0; k < pcnt[1]; k++) {
        unsigned page = pidx[1] + k;
        size_t p, end;

        p = frecs + u32(d, fpages + 4 * (page - 1));
        end = frecs + u32(d, fpages + 4 * page);
        while (p < end) {
            unsigned src = d[p], flg = d[p + 1], cnt = 1, tobj, stype, j;
            uint32_t toff = 0;
            int16_t one = 0;
            size_t offs;

            p += 2;
            if (src & 0x20) {
                cnt = d[p++];
                offs = 0;
            } else {
                one = (int16_t)u16(d, p);
                p += 2;
                offs = 1;
            }
            if (flg & 3) {
                fprintf(stderr, "port: external fixup on page %u\n", page);
                return -1;
            }
            if (flg & 0x40) {
                tobj = u16(d, p);
                p += 2;
            } else
                tobj = d[p++];
            stype = src & 0x0f;
            if (stype != 2) {
                if (flg & 0x10) {
                    toff = u32(d, p);
                    p += 4;
                } else {
                    toff = u16(d, p);
                    p += 2;
                }
            }
            for (j = 0; j < cnt; j++) {
                int so = offs ? one : (int16_t)u16(d, p + 2 * j);
                long at = (long)k * psize + so;
                uint32_t tgt = base[tobj - 1] + toff;

                if (stype == 7 && at >= 0 && at + 4 <= (long)vsize[1]) {
                    memcpy(img + at, &tgt, 4);
                    nfix++;
                } else if (stype != 7) {
                    fprintf(stderr, "port: fixup type %u in the data object\n", stype);
                    return -1;
                }
            }
            if (!offs)
                p += 2 * cnt;
        }
    }
    free(file);
    if (getenv("PORT_VERBOSE"))
        fprintf(stderr, "port: data object %#x-%#x from %s, %ld fixups\n",
                DATA_BASE, DATA_BASE + DATA_SIZE, exe, nfix);
    return 0;
}
