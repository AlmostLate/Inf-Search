#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cstr.h"
#include "growbuf.h"
#include "stemmer.h"

typedef struct {
    char          *pool;
    int            pool_len;
    unsigned int   num_terms;
    unsigned int   num_docs;
    unsigned long long fwd_off;
    unsigned long long dict_off;
    unsigned long long post_off;
    unsigned int  *doc_title_off;
    unsigned short *doc_title_len;
    unsigned int  *doc_url_off;
    unsigned short *doc_url_len;
    unsigned int  *term_pool_off;
    unsigned char *term_len;
    unsigned int  *term_post_off;
    unsigned int  *term_post_cnt;
    unsigned char *post_data;
    int            post_data_len;
} Index;

static unsigned int read_u32(const unsigned char *p) {
    return (unsigned int)p[0]
         | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16)
         | ((unsigned int)p[3] << 24);
}

static unsigned short read_u16(const unsigned char *p) {
    return (unsigned short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}

static unsigned long long read_u64(const unsigned char *p) {
    unsigned long long lo = read_u32(p);
    unsigned long long hi = read_u32(p + 4);
    return lo | (hi << 32);
}

static int load_index(const char *path, Index *idx) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long file_sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char *raw = (unsigned char *)malloc((size_t)file_sz);
    fread(raw, 1, (size_t)file_sz, f);
    fclose(f);

    if (memcmp(raw, "PSCI", 4) != 0) { free(raw); return -2; }

    idx->num_terms = read_u32(raw + 8);
    idx->num_docs  = read_u32(raw + 12);
    idx->fwd_off   = read_u64(raw + 16);
    idx->dict_off  = read_u64(raw + 24);
    idx->post_off  = read_u64(raw + 32);

    idx->pool_len = (int)(idx->fwd_off - 40);
    idx->pool = (char *)malloc((size_t)idx->pool_len);
    memcpy(idx->pool, raw + 40, (size_t)idx->pool_len);

    idx->doc_title_off = (unsigned int *)malloc(idx->num_docs * sizeof(unsigned int));
    idx->doc_title_len = (unsigned short *)malloc(idx->num_docs * sizeof(unsigned short));
    idx->doc_url_off   = (unsigned int *)malloc(idx->num_docs * sizeof(unsigned int));
    idx->doc_url_len   = (unsigned short *)malloc(idx->num_docs * sizeof(unsigned short));

    const unsigned char *fp = raw + idx->fwd_off;
    for (unsigned int i = 0; i < idx->num_docs; i++) {
        idx->doc_title_off[i] = read_u32(fp); fp += 4;
        idx->doc_title_len[i] = read_u16(fp); fp += 2;
        idx->doc_url_off[i]   = read_u32(fp); fp += 4;
        idx->doc_url_len[i]   = read_u16(fp); fp += 2;
    }

    idx->term_pool_off = (unsigned int *)malloc(idx->num_terms * sizeof(unsigned int));
    idx->term_len      = (unsigned char *)malloc(idx->num_terms);
    idx->term_post_off = (unsigned int *)malloc(idx->num_terms * sizeof(unsigned int));
    idx->term_post_cnt = (unsigned int *)malloc(idx->num_terms * sizeof(unsigned int));

    const unsigned char *dp = raw + idx->dict_off;
    for (unsigned int i = 0; i < idx->num_terms; i++) {
        idx->term_pool_off[i] = read_u32(dp); dp += 4;
        idx->term_len[i]      = *dp; dp += 1;
        idx->term_post_off[i] = read_u32(dp); dp += 4;
        idx->term_post_cnt[i] = read_u32(dp); dp += 4;
    }

    idx->post_data_len = (int)(idx->dict_off - idx->post_off);
    idx->post_data = (unsigned char *)malloc((size_t)idx->post_data_len);
    memcpy(idx->post_data, raw + idx->post_off, (size_t)idx->post_data_len);

    free(raw);
    return 0;
}

static void free_index(Index *idx) {
    free(idx->pool);
    free(idx->doc_title_off); free(idx->doc_title_len);
    free(idx->doc_url_off);   free(idx->doc_url_len);
    free(idx->term_pool_off); free(idx->term_len);
    free(idx->term_post_off); free(idx->term_post_cnt);
    free(idx->post_data);
}

static int dict_find(Index *idx, const char *term) {
    int lo = 0, hi = (int)idx->num_terms - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const char *mid_term = idx->pool + idx->term_pool_off[mid];
        int c = cstr_cmp(term, mid_term);
        if (c == 0) return mid;
        if (c < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return -1;
}

static void decode_postings(Index *idx, int term_idx, IntBuf *out) {
    unsigned int off = idx->term_post_off[term_idx];
    unsigned int cnt = idx->term_post_cnt[term_idx];
    const unsigned char *p = idx->post_data + off;

    unsigned int prev = 0;
    for (unsigned int i = 0; i < cnt; i++) {
        unsigned int val = read_u32(p);
        p += 4;
        if (i == 0) {
            prev = val;
        } else {
            prev += val;
        }
        ib_push(out, (int)prev);
    }
}

static void all_docs(Index *idx, IntBuf *out) {
    for (unsigned int i = 0; i < idx->num_docs; i++)
        ib_push(out, (int)i);
}

typedef enum {
    TOK_TERM, TOK_AND, TOK_OR, TOK_NOT, TOK_LPAREN, TOK_RPAREN, TOK_EOF
} TokType;

typedef struct {
    TokType type;
    char    term[256];
} Token;

typedef struct {
    const char *src;
    int         pos;
    Token       cur;
} Parser;

static void _skip_spaces(Parser *p) {
    while (p->src[p->pos] == ' ' || p->src[p->pos] == '\t')
        p->pos++;
}

static void next_token(Parser *p) {
    _skip_spaces(p);
    char c = p->src[p->pos];

    if (c == '\0' || c == '\n' || c == '\r') {
        p->cur.type = TOK_EOF;
        return;
    }
    if (c == '(') { p->cur.type = TOK_LPAREN; p->pos++; return; }
    if (c == ')') { p->cur.type = TOK_RPAREN; p->pos++; return; }
    if (c == '!') { p->cur.type = TOK_NOT; p->pos++; return; }
    if (c == '|' && p->src[p->pos + 1] == '|') {
        p->cur.type = TOK_OR; p->pos += 2; return;
    }
    if (c == '&' && p->src[p->pos + 1] == '&') {
        p->cur.type = TOK_AND; p->pos += 2; return;
    }

    int n = 0;
    while (p->src[p->pos] && p->src[p->pos] != ' ' && p->src[p->pos] != '\t'
           && p->src[p->pos] != '(' && p->src[p->pos] != ')'
           && p->src[p->pos] != '!' && p->src[p->pos] != '\n'
           && p->src[p->pos] != '\r'
           && !(p->src[p->pos] == '|' && p->src[p->pos + 1] == '|')
           && !(p->src[p->pos] == '&' && p->src[p->pos + 1] == '&')
           && n < 250) {
        p->cur.term[n++] = p->src[p->pos++];
    }
    p->cur.term[n] = '\0';
    p->cur.type = TOK_TERM;
}

static void parser_init(Parser *p, const char *query) {
    p->src = query;
    p->pos = 0;
    next_token(p);
}

static IntBuf parse_expr(Parser *p, Index *idx, int min_prec);

static IntBuf parse_atom(Parser *p, Index *idx) {
    IntBuf result;
    ib_init(&result);

    if (p->cur.type == TOK_NOT) {
        next_token(p);
        IntBuf inner = parse_atom(p, idx);
        IntBuf all;
        ib_init(&all);
        all_docs(idx, &all);
        ib_subtract(&all, &inner, &result);
        ib_free(&all);
        ib_free(&inner);
        return result;
    }

    if (p->cur.type == TOK_LPAREN) {
        next_token(p);
        result = parse_expr(p, idx, 1);
        if (p->cur.type == TOK_RPAREN)
            next_token(p);
        return result;
    }

    if (p->cur.type == TOK_TERM) {
        char term[256];
        int i = 0;
        while (p->cur.term[i]) {
            term[i] = (p->cur.term[i] >= 'A' && p->cur.term[i] <= 'Z')
                     ? (char)(p->cur.term[i] + 32)
                     : p->cur.term[i];
            i++;
        }
        term[i] = '\0';
        stem_english(term);

        int ti = dict_find(idx, term);
        if (ti >= 0)
            decode_postings(idx, ti, &result);

        next_token(p);
        return result;
    }

    return result;
}

static int get_prec(TokType t) {
    if (t == TOK_OR) return 1;
    if (t == TOK_AND) return 2;
    return 0;
}

static int is_implicit_and(TokType t) {
    return t == TOK_TERM || t == TOK_NOT || t == TOK_LPAREN;
}

static IntBuf parse_expr(Parser *p, Index *idx, int min_prec) {
    IntBuf left = parse_atom(p, idx);

    for (;;) {
        TokType op = p->cur.type;
        int prec;

        if (op == TOK_AND || op == TOK_OR) {
            prec = get_prec(op);
        } else if (is_implicit_and(op)) {
            op = TOK_AND;
            prec = 2;
        } else {
            break;
        }

        if (prec < min_prec) break;

        if (p->cur.type == TOK_AND || p->cur.type == TOK_OR)
            next_token(p);

        IntBuf right = parse_expr(p, idx, prec + 1);

        IntBuf merged;
        ib_init(&merged);
        if (op == TOK_AND) {
            ib_intersect(&left, &right, &merged);
        } else {
            ib_union(&left, &right, &merged);
        }
        ib_free(&left);
        ib_free(&right);
        left = merged;
    }

    return left;
}

static void print_raw(Index *idx, unsigned int off, int len) {
    if (off + len <= (unsigned int)idx->pool_len)
        fwrite(idx->pool + off, 1, (size_t)len, stdout);
}

static void print_json_escaped(Index *idx, unsigned int off, int len) {
    if (off + len > (unsigned int)idx->pool_len) return;
    const char *s = idx->pool + off;
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (c == '"') fputs("\\\"", stdout);
        else if (c == '\\') fputs("\\\\", stdout);
        else if (c == '\n') fputs("\\n", stdout);
        else if (c == '\r') fputs("\\r", stdout);
        else if (c == '\t') fputs("\\t", stdout);
        else fputc(c, stdout);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <index_file> [--json]\n", argv[0]);
        return 1;
    }

    int json_mode = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) json_mode = 1;
    }

    Index idx;
    if (load_index(argv[1], &idx) != 0) {
        fprintf(stderr, "Failed to load index: %s\n", argv[1]);
        return 1;
    }
    fprintf(stderr, "Index loaded: %u docs, %u terms\n", idx.num_docs, idx.num_terms);

    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        clock_t t0 = clock();

        Parser parser;
        parser_init(&parser, line);
        IntBuf result = parse_expr(&parser, &idx, 1);

        clock_t t1 = clock();
        double ms = (double)(t1 - t0) / CLOCKS_PER_SEC * 1000.0;

        if (json_mode) {
            printf("{\"query\":\"");
            for (int ci = 0; line[ci]; ci++) {
                if (line[ci] == '"') fputs("\\\"", stdout);
                else if (line[ci] == '\\') fputs("\\\\", stdout);
                else fputc(line[ci], stdout);
            }
            printf("\",\"count\":%d,\"time_ms\":%.3f,\"results\":[", result.len, ms);
            int limit = result.len < 50 ? result.len : 50;
            for (int i = 0; i < limit; i++) {
                if (i > 0) printf(",");
                printf("{\"id\":%d,\"title\":\"", result.data[i]);
                print_json_escaped(&idx, idx.doc_title_off[result.data[i]], idx.doc_title_len[result.data[i]]);
                printf("\",\"url\":\"");
                print_json_escaped(&idx, idx.doc_url_off[result.data[i]], idx.doc_url_len[result.data[i]]);
                printf("\"}");
            }
            printf("]}\n");
        } else {
            printf("Query: %s\n", line);
            printf("Results: %d (%.3f ms)\n", result.len, ms);
            int limit = result.len < 50 ? result.len : 50;
            for (int i = 0; i < limit; i++) {
                printf("  %d. ", i + 1);
                print_raw(&idx, idx.doc_title_off[result.data[i]], idx.doc_title_len[result.data[i]]);
                printf("\n     ");
                print_raw(&idx, idx.doc_url_off[result.data[i]], idx.doc_url_len[result.data[i]]);
                printf("\n");
            }
            if (result.len > 50)
                printf("  ... and %d more results\n", result.len - 50);
            printf("\n");
        }

        fflush(stdout);
        ib_free(&result);
    }

    free_index(&idx);
    return 0;
}
