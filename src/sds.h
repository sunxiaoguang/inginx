/* SDSLib 2.0 -- A C dynamic strings library
 *
 * Copyright (c) 2006-2015, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2015, Oran Agra
 * Copyright (c) 2015, Redis Labs, Inc
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __SDS_H
#define __SDS_H

#define SDS_MAX_PREALLOC (1024*1024)

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef char *sds;

/* Note: sdshdr5 is never used, we just access the flags byte directly.
 * However is here to document the layout of type 5 SDS strings. */
struct __attribute__ ((__packed__)) sdshdr5 {
    unsigned char flags; /* 3 lsb of type, and 5 msb of string length */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t len; /* used */
    uint8_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t len; /* used */
    uint16_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr32 {
    uint32_t len; /* used */
    uint32_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr64 {
    uint64_t len; /* used */
    uint64_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};

#define SDS_TYPE_5  0
#define SDS_TYPE_8  1
#define SDS_TYPE_16 2
#define SDS_TYPE_32 3
#define SDS_TYPE_64 4
#define SDS_TYPE_MASK 7
#define SDS_TYPE_BITS 3
#define SDS_HDR_VAR(T,s) struct sdshdr##T *sh = (struct sdshdr##T *)(void*)((s)-(sizeof(struct sdshdr##T)));
#define SDS_HDR(T,s) ((struct sdshdr##T *)((s)-(sizeof(struct sdshdr##T))))
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)
#define sdslen(s) inginxSdslen(s)
#define sdsavail(s) inginxSdsavail(s)
#define sdssetlen(s, snewlen) inginxSdssetlen(s, snewlen)
#define sdsinclen(s, inc) inginxSdsinclen(s, inc)
#define sdsalloc(s) inginxSdsalloc(s)
#define sdssetalloc(s, newlen) inginxSdssetalloc(s, newlen)

#define sdsnewlen(init, initlen) inginxSdsnewlen(init, initlen)
#define sdsnew(init) inginxSdsnew(init)
#define sdsempty inginxSdsempty
#define sdsdup(s) inginxSdsdup(s)
#define sdsfree(s) inginxSdsfree(s)
#define sdsgrowzero(s, len) inginxSdsgrowzero(s, len)
#define sdscatlen(s, t, len) inginxSdscatlen(s, t, len)
#define sdscat(s, t) inginxSdscat(s, t)
#define sdscatsds(s, t) inginxSdscatsds(s, t)
#define sdscpylen(s, t, len) inginxSdscpylen(s, t, len)
#define sdscpy(s, t) inginxSdscpy(s, t)

#define sdscatvprintf(s, fmt, ap) inginxSdscatvprintf(s, fmt, ap)
#define sdscatprintf inginxSdscatprintf
#define sdscatfmt inginxSdscatfmt
#define sdstrim(s, cset) inginxSdstrim(s, cset)
#define sdsrange(s, start, end) inginxSdsrange(s, start, end)
#define sdsupdatelen(s) inginxSdsupdatelen(s)
#define sdsclear(s) inginxSdsclear(s)
#define sdscmp(s1, s2) inginxSdscmp(s1, s2)
#define sdssplitlen(s, len, sep, seplen, count) inginxSdssplitlen(s, len, sep, seplen, count)
#define sdsfreesplitres(tokens, count) inginxSdsfreesplitres(tokens, count)
#define sdstolower(s) inginxSdstolower(s)
#define sdstoupper(s) inginxSdstoupper(s)
#define sdsfromlonglong(value) inginxSdsfromlonglong(value)
#define sdscatrepr(s, p, len) inginxSdscatrepr(s, p, len)
#define sdssplitargs(line, argc) inginxSdssplitargs(line, argc)
#define sdsmapchars(s, from, to, setlen) inginxSdsmapchars(s, from, to, setlen)
#define sdsjoin(argv, argc, sep) inginxSdsjoin(argv, argc, sep)
#define sdsjoinsds(argv, argc, sep, seplen) inginxSdsjoinsds(argv, argc, sep, seplen)

#define sdsMakeRoomFor(s, addlen) inginxSdsMakeRoomFor(s, addlen)
#define sdsIncrLen(s, incr) inginxSdsIncrLen(s, incr)
#define sdsRemoveFreeSpace(s) inginxSdsRemoveFreeSpace(s)
#define sdsAllocSize(s) inginxSdsAllocSize(s)
#define sdsAllocPtr(s) inginxSdsAllocPtr(s)

#define sds_malloc(size) inginxSds_malloc(size)
#define sds_realloc(ptr, size) inginxSds_realloc(ptr, size)
#define sds_free(ptr) inginxSds_free(ptr)

#define sdsTest(argc, argv) inginxSdsTest(argc, argv)

static inline size_t inginxSdslen(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->len;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->len;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->len;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->len;
    }
    return 0;
}

static inline size_t inginxSdsavail(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5: {
            return 0;
        }
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            return sh->alloc - sh->len;
        }
    }
    return 0;
}

static inline void inginxSdssetlen(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len = newlen;
            break;
    }
}

static inline void inginxSdsinclen(sds s, size_t inc) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                unsigned char newlen = SDS_TYPE_5_LEN(flags)+inc;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len += inc;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len += inc;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len += inc;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len += inc;
            break;
    }
}

/* sdsalloc() = sdsavail() + sdslen() */
static inline size_t inginxSdsalloc(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->alloc;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->alloc;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->alloc;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->alloc;
    }
    return 0;
}

static inline void inginxSdssetalloc(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            /* Nothing to do, this type has no total allocation info. */
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->alloc = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->alloc = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->alloc = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->alloc = newlen;
            break;
    }
}

sds inginxSdsnewlen(const void *init, size_t initlen);
sds inginxSdsnew(const char *init);
sds inginxSdsempty(void);
sds inginxSdsdup(const sds s);
void inginxSdsfree(sds s);
sds inginxSdsgrowzero(sds s, size_t len);
sds inginxSdscatlen(sds s, const void *t, size_t len);
sds inginxSdscat(sds s, const char *t);
sds inginxSdscatsds(sds s, const sds t);
sds inginxSdscpylen(sds s, const char *t, size_t len);
sds inginxSdscpy(sds s, const char *t);

sds inginxSdscatvprintf(sds s, const char *fmt, va_list ap);
#ifdef __GNUC__
sds inginxSdscatprintf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
sds inginxSdscatprintf(sds s, const char *fmt, ...);
#endif

sds inginxSdscatfmt(sds s, char const *fmt, ...);
sds inginxSdstrim(sds s, const char *cset);
void inginxSdsrange(sds s, int start, int end);
void inginxSdsupdatelen(sds s);
void inginxSdsclear(sds s);
int inginxSdscmp(const sds s1, const sds s2);
sds *inginxSdssplitlen(const char *s, int len, const char *sep, int seplen, int *count);
void inginxSdsfreesplitres(sds *tokens, int count);
void inginxSdstolower(sds s);
void inginxSdstoupper(sds s);
sds inginxSdsfromlonglong(long long value);
sds inginxSdscatrepr(sds s, const char *p, size_t len);
sds *sdssplitargs(const char *line, int *argc);
sds inginxSdsmapchars(sds s, const char *from, const char *to, size_t setlen);
sds inginxSdsjoin(char **argv, int argc, char *sep);
sds inginxSdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen);

/* Low level functions exposed to the user API */
sds inginxSdsMakeRoomFor(sds s, size_t addlen);
void inginxSdsIncrLen(sds s, int incr);
sds inginxSdsRemoveFreeSpace(sds s);
size_t inginxSdsAllocSize(sds s);
void *inginxSdsAllocPtr(sds s);

/* Export the allocator used by SDS to the program using SDS.
 * Sometimes the program SDS is linked to, may use a different set of
 * allocators, but may want to allocate or free things that SDS will
 * respectively free or allocate. */
void *inginxSds_malloc(size_t size);
void *inginxSds_realloc(void *ptr, size_t size);
void inginxSds_free(void *ptr);

#ifdef REDIS_TEST
int inginxSdsTest(int argc, char *argv[]);
#endif

#ifdef __cplusplus
}
#endif

#endif
