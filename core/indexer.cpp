#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>

#include "cstr.h"
#include "growbuf.h"
#include "robinmap.h"
#include "stemmer.h"

typedef struct {
    char *buf;
    int   len;
    int   cap;
} StrPool;

static void sp_init(StrPool *p) {
    p->cap = 1024 * 1024;
    p->buf = (char *)malloc((size_t)p->cap);
    p->len = 0;
}

static int sp_add(StrPool *p, const char *s, int slen) {
    while (p->len + slen + 1 > p->cap) {
        p->cap = p->cap + p->cap / 2;
        p->buf = (char *)realloc(p->buf, (size_t)p->cap);
    }
    int off = p->len;
    memcpy(p->buf + p->len, s, (size_t)slen);
    p->buf[p->len + slen] = '\0';
    p->len += slen + 1;
    return off;
}

static void sp_free(StrPool *p) {
    free(p->buf);
}

typedef struct {
    int title_off;
    int title_len;
    int url_off;
    int url_len;
} DocInfo;

typedef struct {
    DocInfo *data;
    int      len;
    int      cap;
} DocBuf;

static void db_init(DocBuf *b) {
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

static void db_push(DocBuf *b, DocInfo d) {
    if (b->len == b->cap) {
        int nc = b->cap < 8 ? 8 : b->cap + b->cap / 2;
        b->data = (DocInfo *)realloc(b->data, (size_t)nc * sizeof(DocInfo));
        b->cap = nc;
    }
    b->data[b->len++] = d;
}

static void db_free(DocBuf *b) {
    free(b->data);
}

static int is_token_char(char c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    return 0;
}

static int is_inner_char(char c) {
    return c == '-' || c == '\'';
}

static int is_all_num(const char *s, int len) {
    for (int i = 0; i < len; i++)
        if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

static void process_doc(const char *text, int text_len, int doc_id, RobinMap *inv) {
    char tok[256];
    int i = 0;
    while (i < text_len) {
        while (i < text_len && !is_token_char(text[i])) i++;
        if (i >= text_len) break;
        int tlen = 0;
        while (i < text_len && tlen < 250) {
            char c = text[i];
            if (is_token_char(c)) {
                tok[tlen++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
                i++;
            } else if (is_inner_char(c) && i + 1 < text_len && is_token_char(text[i + 1])) {
                tok[tlen++] = c;
                i++;
            } else {
                break;
            }
        }
        while (tlen > 0 && is_inner_char(tok[tlen - 1])) tlen--;
        while (tlen > 0 && is_inner_char(tok[0])) {
            memmove(tok, tok + 1, (size_t)(tlen - 1));
            tlen--;
        }
        tok[tlen] = '\0';

        if (tlen < 2) continue;
        if (is_all_num(tok, tlen)) continue;

        stem_english(tok);
        tlen = cstr_len(tok);
        if (tlen < 2) continue;

        IntBuf *postings = rm_get_or_create(inv, tok);
        if (postings->len == 0 || postings->data[postings->len - 1] != doc_id)
            ib_push(postings, doc_id);
    }
}

static void _ks_sift(char **a, int start, int end) {
    int root = start;
    while (root * 2 + 1 <= end) {
        int child = root * 2 + 1;
        int sw = root;
        if (cstr_cmp(a[sw], a[child]) < 0) sw = child;
        if (child + 1 <= end && cstr_cmp(a[sw], a[child + 1]) < 0) sw = child + 1;
        if (sw == root) return;
        char *tmp = a[root]; a[root] = a[sw]; a[sw] = tmp;
        root = sw;
    }
}

static void sort_keys(StrBuf *keys) {
    int n = keys->len;
    if (n < 2) return;
    for (int s = (n - 2) / 2; s >= 0; s--)
        _ks_sift(keys->data, s, n - 1);
    for (int e = n - 1; e > 0; e--) {
        char *tmp = keys->data[0]; keys->data[0] = keys->data[e]; keys->data[e] = tmp;
        _ks_sift(keys->data, 0, e - 1);
    }
}

static void write_u8(FILE *f, unsigned char v) { fwrite(&v, 1, 1, f); }
static void write_u16(FILE *f, unsigned short v) { fwrite(&v, 2, 1, f); }
static void write_u32(FILE *f, unsigned int v) { fwrite(&v, 4, 1, f); }
static void write_u64(FILE *f, unsigned long long v) { fwrite(&v, 8, 1, f); }

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <corpus_dir> <index_file>\n", argv[0]);
        return 1;
    }

    const char *corpus_dir = argv[1];
    const char *idx_path   = argv[2];

    clock_t t0 = clock();

    RobinMap inv;
    rm_init(&inv);

    StrPool pool;
    sp_init(&pool);

    DocBuf docs;
    db_init(&docs);

    DIR *dir = opendir(corpus_dir);
    if (!dir) { fprintf(stderr, "Cannot open %s\n", corpus_dir); return 1; }

    struct dirent *ent;
    int doc_id = 0;

    while ((ent = readdir(dir)) != NULL) {
        int nlen = (int)strlen(ent->d_name);
        if (nlen < 5 || strcmp(ent->d_name + nlen - 4, ".txt") != 0) continue;

        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", corpus_dir, ent->d_name);

        FILE *f = fopen(path, "r");
        if (!f) continue;

        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = (char *)malloc((size_t)(sz + 1));
        fread(buf, 1, (size_t)sz, f);
        buf[sz] = '\0';
        fclose(f);

        char *title_start = buf;
        char *p = buf;
        while (*p && *p != '\n') p++;
        int title_len = (int)(p - title_start);
        if (*p) p++;

        char *url_start = p;
        while (*p && *p != '\n') p++;
        int url_len = (int)(p - url_start);
        if (*p) p++;

        char *body = p;
        int body_len = (int)(sz - (body - buf));

        DocInfo di;
        di.title_off = sp_add(&pool, title_start, title_len);
        di.title_len = title_len;
        di.url_off   = sp_add(&pool, url_start, url_len);
        di.url_len   = url_len;
        db_push(&docs, di);

        process_doc(body, body_len, doc_id, &inv);
        doc_id++;
        free(buf);

        if (doc_id % 5000 == 0)
            fprintf(stderr, "Indexed %d documents...\n", doc_id);
    }
    closedir(dir);

    int num_docs  = docs.len;
    int num_terms = inv.count;
    fprintf(stderr, "Documents: %d, Terms: %d\n", num_docs, num_terms);

    StrBuf keys;
    sb_init(&keys);
    rm_keys(&inv, &keys);
    sort_keys(&keys);
    fprintf(stderr, "Sorted %d term keys\n", keys.len);

    int *term_pool_off  = (int *)malloc((size_t)num_terms * sizeof(int));
    int *term_len_arr   = (int *)malloc((size_t)num_terms * sizeof(int));
    for (int i = 0; i < keys.len; i++) {
        int kl = cstr_len(keys.data[i]);
        term_pool_off[i] = sp_add(&pool, keys.data[i], kl);
        term_len_arr[i]  = kl;
    }

    FILE *out = fopen(idx_path, "wb");
    if (!out) { fprintf(stderr, "Cannot open %s for writing\n", idx_path); return 1; }

    char header[40];
    memset(header, 0, 40);
    fwrite(header, 1, 40, out);

    fwrite(pool.buf, 1, (size_t)pool.len, out);

    unsigned long long fwd_off = (unsigned long long)ftell(out);
    for (int i = 0; i < num_docs; i++) {
        write_u32(out, (unsigned int)docs.data[i].title_off);
        write_u16(out, (unsigned short)docs.data[i].title_len);
        write_u32(out, (unsigned int)docs.data[i].url_off);
        write_u16(out, (unsigned short)docs.data[i].url_len);
    }

    unsigned long long post_off = (unsigned long long)ftell(out);
    int *post_rel_off = (int *)malloc((size_t)num_terms * sizeof(int));
    int *post_counts  = (int *)malloc((size_t)num_terms * sizeof(int));

    long long total_postings = 0;
    for (int i = 0; i < keys.len; i++) {
        post_rel_off[i] = (int)((unsigned long long)ftell(out) - post_off);
        IntBuf *pb = rm_find(&inv, keys.data[i]);
        if (!pb || pb->len == 0) {
            post_counts[i] = 0;
            continue;
        }
        ib_sort(pb);
        ib_unique(pb);
        post_counts[i] = pb->len;
        total_postings += pb->len;

        write_u32(out, (unsigned int)pb->data[0]);
        for (int j = 1; j < pb->len; j++) {
            unsigned int delta = (unsigned int)(pb->data[j] - pb->data[j - 1]);
            write_u32(out, delta);
        }
    }

    unsigned long long dict_off = (unsigned long long)ftell(out);
    for (int i = 0; i < keys.len; i++) {
        write_u32(out, (unsigned int)term_pool_off[i]);
        write_u8(out, (unsigned char)term_len_arr[i]);
        write_u32(out, (unsigned int)post_rel_off[i]);
        write_u32(out, (unsigned int)post_counts[i]);
    }

    fseek(out, 0, SEEK_SET);
    fwrite("PSCI", 1, 4, out);
    write_u32(out, 1);
    write_u32(out, (unsigned int)num_terms);
    write_u32(out, (unsigned int)num_docs);
    write_u64(out, fwd_off);
    write_u64(out, dict_off);
    write_u64(out, post_off);
    fclose(out);

    clock_t t1 = clock();
    double elapsed = (double)(t1 - t0) / CLOCKS_PER_SEC;

    long long file_size = 0;
    {
        FILE *tmp = fopen(idx_path, "rb");
        if (tmp) { fseek(tmp, 0, SEEK_END); file_size = ftell(tmp); fclose(tmp); }
    }

    double avg_term_len = 0;
    for (int i = 0; i < keys.len; i++) avg_term_len += term_len_arr[i];
    if (keys.len > 0) avg_term_len /= keys.len;

    fprintf(stderr, "\n=== Indexing Results ===\n");
    fprintf(stderr, "Documents:          %d\n", num_docs);
    fprintf(stderr, "Terms:              %d\n", num_terms);
    fprintf(stderr, "Avg term length:    %.2f\n", avg_term_len);
    fprintf(stderr, "Total postings:     %lld\n", total_postings);
    fprintf(stderr, "Avg postings/term:  %.2f\n", num_terms > 0 ? (double)total_postings / num_terms : 0.0);
    fprintf(stderr, "Index file size:    %lld bytes (%.1f MB)\n", file_size, file_size / (1024.0 * 1024.0));
    fprintf(stderr, "Time:               %.2f s\n", elapsed);
    fprintf(stderr, "Speed per doc:      %.3f ms\n", num_docs > 0 ? elapsed * 1000 / num_docs : 0.0);

    free(term_pool_off);
    free(term_len_arr);
    free(post_rel_off);
    free(post_counts);
    sb_free_deep(&keys);
    sp_free(&pool);
    db_free(&docs);
    rm_free(&inv);

    return 0;
}
