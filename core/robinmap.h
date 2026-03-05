#ifndef ROBINMAP_H
#define ROBINMAP_H

#include <stdlib.h>
#include <string.h>
#include "cstr.h"
#include "growbuf.h"

#define RM_LOAD_MAX 0.70
#define RM_INIT_CAP 4096

typedef struct { char *key; IntBuf postings; int psl; } RMSlot;
typedef struct { RMSlot *slots; int cap; int count; } RobinMap;

static inline unsigned int _rm_xxhash32(const char *key) {
    const unsigned int P1=0x9E3779B1u, P2=0x85EBCA77u, P3=0xC2B2AE3Du, P4=0x27D4EB2Fu, P5=0x165667B1u;
    int len = cstr_len(key);
    unsigned int h = P5 + (unsigned int)len;
    const unsigned char *p = (const unsigned char *)key;
    while (len >= 4) {
        unsigned int k = (unsigned int)p[0]|((unsigned int)p[1]<<8)|((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24);
        k *= P3; k = (k<<17)|(k>>15); k *= P4;
        h ^= k; h = (h<<13)|(h>>19); h = h*5+P1;
        p += 4; len -= 4;
    }
    while (len > 0) { h ^= (unsigned int)(*p)*P5; h = (h<<11)|(h>>21); h *= P1; p++; len--; }
    h ^= h>>15; h *= P2; h ^= h>>13; h *= P3; h ^= h>>16;
    return h;
}

static inline void rm_init(RobinMap *m) { m->cap = RM_INIT_CAP; m->count = 0; m->slots = (RMSlot*)calloc((size_t)m->cap, sizeof(RMSlot)); }

static inline void rm_free(RobinMap *m) {
    for (int i = 0; i < m->cap; i++) { if (m->slots[i].key) { free(m->slots[i].key); ib_free(&m->slots[i].postings); } }
    free(m->slots); m->slots = NULL; m->cap = 0; m->count = 0;
}

static void _rm_insert_slot(RMSlot *slots, int cap, RMSlot entry) {
    unsigned int idx = _rm_xxhash32(entry.key) % (unsigned int)cap;
    int psl = 0;
    for (;;) {
        if (!slots[idx].key) { entry.psl = psl; slots[idx] = entry; return; }
        if (psl > slots[idx].psl) { RMSlot tmp = slots[idx]; entry.psl = psl; slots[idx] = entry; entry = tmp; psl = entry.psl; }
        psl++; idx = (idx+1) % (unsigned int)cap;
    }
}

static void _rm_grow(RobinMap *m) {
    int nc = m->cap + m->cap/2;
    RMSlot *ns = (RMSlot*)calloc((size_t)nc, sizeof(RMSlot));
    for (int i = 0; i < m->cap; i++) if (m->slots[i].key) { m->slots[i].psl = 0; _rm_insert_slot(ns, nc, m->slots[i]); }
    free(m->slots); m->slots = ns; m->cap = nc;
}

static inline IntBuf *rm_get_or_create(RobinMap *m, const char *key) {
    if ((double)m->count / m->cap > RM_LOAD_MAX) _rm_grow(m);
    unsigned int idx = _rm_xxhash32(key) % (unsigned int)m->cap;
    int psl = 0;
    for (;;) {
        if (!m->slots[idx].key) { m->slots[idx].key = cstr_dup(key); ib_init(&m->slots[idx].postings); m->slots[idx].psl = psl; m->count++; return &m->slots[idx].postings; }
        if (cstr_eq(m->slots[idx].key, key)) return &m->slots[idx].postings;
        if (psl > m->slots[idx].psl) {
            RMSlot entry; entry.key = cstr_dup(key); ib_init(&entry.postings); entry.psl = psl;
            RMSlot displaced = m->slots[idx]; m->slots[idx] = entry; m->count++;
            displaced.psl++; _rm_insert_slot(m->slots, m->cap, displaced);
            return &m->slots[idx].postings;
        }
        psl++; idx = (idx+1) % (unsigned int)m->cap;
    }
}

static inline IntBuf *rm_find(RobinMap *m, const char *key) {
    unsigned int idx = _rm_xxhash32(key) % (unsigned int)m->cap;
    int psl = 0;
    for (;;) {
        if (!m->slots[idx].key) return NULL;
        if (psl > m->slots[idx].psl) return NULL;
        if (cstr_eq(m->slots[idx].key, key)) return &m->slots[idx].postings;
        psl++; idx = (idx+1) % (unsigned int)m->cap;
    }
}

typedef void (*rm_iter_fn)(const char *key, IntBuf *postings, void *ctx);

static inline void rm_foreach(RobinMap *m, rm_iter_fn fn, void *ctx) {
    for (int i = 0; i < m->cap; i++) if (m->slots[i].key) fn(m->slots[i].key, &m->slots[i].postings, ctx);
}

static inline void rm_keys(RobinMap *m, StrBuf *out) {
    for (int i = 0; i < m->cap; i++) if (m->slots[i].key) sb_push(out, m->slots[i].key);
}

#endif
