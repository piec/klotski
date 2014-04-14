#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>

#include <glib.h>

//#define MULTI_MOVES

/* Number of layers to compare against in search tree */
#define N_LAYERS 3

struct piece_cl {
    const char *name;
    int h, w;
};
const struct piece_cl pieces_cl[] = {
    {  "1x1", 1, 1 }, /* 1 */
    { "hori", 1, 2 }, /* 2 */
    { "vert", 2, 1 }, /* 3 */
    {  "2x2", 2, 2 }, /* 4 */
};

struct piece_t {
    int id;
    int ref;

    const struct piece_cl *cl;
    int x, y;
};

struct state_t {
    int id;
    int ref;

    int h, w; /* hauteur, largeur */
    int n; /* n pieces */
    char *flat;
    char *flat_id;
#ifdef DEBUG
    int buf_n;
#endif
    struct state_t *prev;
    int n_moves;

    struct piece_t *pieces[0];
};

static struct piece_t * new_piece() {
    struct piece_t *p = calloc(1, sizeof(*p));
    p->ref = 1;
    return p;
}

static struct piece_t * dup_piece(const struct piece_t *piece) {
    struct piece_t *p = calloc(1, sizeof(*p));
    memcpy(p, piece, sizeof(*p));
    p->ref = 1;
    return p;
}

static void unref_piece(struct piece_t *p) {
    if (p == NULL) return;

    if ((--p->ref) == 0) {
        free(p);
    }
}

static int g_state_id = 0;

static struct state_t * alloc_state(int n) {
    return malloc(sizeof(struct state_t) + n * sizeof(struct piece_t *));
}
static void free_state(struct state_t *e) {
    free(e);
}

static struct state_t * new_state(int n) {
    if (n <= 0) return NULL;

    struct state_t *e = alloc_state(n);
    assert(e);

    e->ref = 1;
    e->id = g_state_id++;
    e->n = 0;

    e->h = e->w = 0;
    e->flat = NULL;
    e->flat_id = NULL;
#ifdef DEBUG
    e->buf_n = n;
#endif
    e->prev = NULL;
    e->n_moves = 0;

    return e;
}

static void unref_state(struct state_t *e) {
    assert(e);
    if (e == NULL) return;

    if ((--e->ref) == 0) {
        for(int i = 0; i < e->n; i++) {
            unref_piece(e->pieces[i]);
        }

        if (e->flat) {
            free(e->flat);
        }
        if (e->flat_id) {
            free(e->flat_id);
        }
        if (e->prev) {
            unref_state(e->prev);
        }

        free_state(e);
    }
}

static struct state_t * dup_state(const struct state_t *e) {
    assert(e);
    if (e == NULL) return NULL;

    struct state_t *r = alloc_state(e->n);
    r->id = g_state_id++;
    r->ref = 1;
    r->n = e->n;

    for(int i = 0; i < r->n; i++) {
        r->pieces[i] = e->pieces[i];
        r->pieces[i]->ref++;
    }

    r->h = e->h;
    r->w = e->w;
    r->flat = NULL;
    r->flat_id = NULL;
#ifdef DEBUG
    r->buf_n = e->buf_n;
#endif
    r->prev = NULL;
    r->n_moves = e->n_moves;
    return r;
}

static void add_piece(struct state_t *e, struct piece_t *p) {
    assert(e);
    if (e == NULL || p == NULL) return;

#ifdef DEBUG
    assert(e->n < e->buf_n);
#endif
    static int s_id = 0;
    p->id = s_id++;

    e->pieces[e->n] = p;
    e->n++;

    int w = p->x + p->cl->w;
    if (w > e->w) e->w = w;

    int h = p->y + p->cl->h;
    if (h > e->h) e->h = h;
}

static void print_piece(const struct piece_t *p) {
    if (p == NULL) return;
    printf("%4s: %d %d | id=%d ref=%d", p->cl->name, p->x, p->y, p->id, p->ref);
}

static void print_state(const struct state_t *e) {
    assert(e);
    if (e == NULL) return;
    printf("state[%d] = { id=%d\n", e->n, e->id);
#ifdef DEBUG
    printf("  buf_n=%d\n", e->buf_n);
#endif
    printf("  w=%d, h=%d\n", e->w, e->h);
    printf("  n_moves=%d\n", e->n_moves);
    for(int i = 0; i < e->n; i++) {
        printf("  ");
        print_piece(e->pieces[i]);
        printf("\n");
    }
    printf("}\n");
}

static char * flat_state(struct state_t *e, int by_id) {
    assert(e != NULL);

    char *flat = by_id ? e->flat_id : e->flat;

    if (flat == NULL) {
        int buf_n = (e->w + 1) * e->h + 1;
        flat = malloc(buf_n);
        memset(flat, '.', buf_n);

        for(int i = e->w; i < buf_n; i += e->w + 1) {
            flat[i] = '\n';
        }
        flat[buf_n - 1] = 0;

        for(int i = 0; i < e->n; i++) {
            const struct piece_t *p = e->pieces[i];
            for(int u = 0; u < p->cl->w; u++) {
                for(int v = 0; v < p->cl->h; v++) {
                    int index = (e->w + 1) * (p->y + v) + (p->x + u);
                    assert(index < buf_n);
                    if (by_id) {
                        /*flat[index] = '0' + (p->id % 10);*/
                        flat[index] = '0' + p->id;
                    } else {
                        flat[index] = p->cl->name[0];
                    }
                }
            }
        }
    }

    if (by_id) {
        e->flat_id = flat;
    } else {
        e->flat = flat;
    }
    return flat;
}

/* types
 * 1 1x1
 * 2 2x1 horiz
 * 3 1x2 vert
 * 4 2x2 square */
static struct state_t * load(FILE *f) {
    if (f == NULL) return NULL;
    struct state_t *e = NULL;

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    int start = 1;
    int n;
    int n_found = 0;
    while ((linelen = getline(&line, &linecap, f)) > 0) {
        if (isspace(line[0]))
            continue;

        if (start) {
            if (sscanf(line, "%d\n", &n) == 1) {
                //printf("n=%d\n", n);
                e = new_state(n);
                start = 0;
            }
        }

        int type, x, y;
        if (sscanf(line, "%d %d %d\n", &type, &x, &y) == 3) {
            //printf("%d %d %d\n", type, x, y);
            if (type > 0) {
                n_found++;
                struct piece_t *p = new_piece();
                p->x = x - 1;
                p->y = y - 1;
                p->cl = &pieces_cl[type - 1];
                add_piece(e, p);
            }
        }
    }

    return e;
}

static int piece_collide(const struct piece_t *p, const struct piece_t *q) {
    for(int u = 0; u < p->cl->w; u++) {
        for(int v = 0; v < p->cl->h; v++) {
            int x = p->x + u;
            int y = p->y + v;

            if ( (x >= q->x && x < q->x + q->cl->w)
              && (y >= q->y && y < q->y + q->cl->h)) {
                return 1;
            }
        }
    }

    return 0;
}

/* right, down, left, up */
static const int g_offset[][2] = { {1, 0}, {0, 1}, {-1, 0}, {0, -1} };

static int can_move(const struct state_t *e, struct piece_t *p, unsigned dir) {
    if (e == NULL || p == NULL || dir >= 4 ) return -1;
    const int *off = g_offset[dir];

    int x_sv = p->x;
    int y_sv = p->y;
    p->x += off[0];
    p->y += off[1];

    int ok = (p->x >= 0) && (p->y >= 0)
        && (p->x + p->cl->w <= e->w)
        && (p->y + p->cl->h <= e->h);

    if (ok) {
        int collide = 0;
        for(int i = 0; i < e->n; i++) {
            const struct piece_t *q = e->pieces[i];
            if (p == q) continue;

            if (piece_collide(p, q)) {
                collide = 1;
                break;
            }
        }
        ok = !collide;
    }

    p->x = x_sv;
    p->y = y_sv;
    return ok;
}

static struct piece_t * piece_move(struct piece_t *p, int dir) {
    //assert(p != NULL && dir < 4 );
    const int *off = g_offset[dir];

    struct piece_t *r = NULL;
    if (p->ref > 1) {
        r = dup_piece(p);
        unref_piece(p);
    } else {
        r = p;
    }

    r->x += off[0];
    r->y += off[1];
    return r;
}

static struct state_t * state_move(struct state_t *e, int piece, int dir1, int dir2) {
    assert(e != NULL && piece < e->n);
    struct state_t *f = dup_state(e);
    int dirs[] = {dir1, dir2, -1};
    
    int i = 0;
    for(int *dir = dirs; *dir != -1; dir++) {
        struct piece_t *p = f->pieces[piece];
        if (can_move(f, p, *dir)) {
            f->pieces[piece] = piece_move(p, *dir);
        } else {
            unref_state(f);
            return NULL;
        }
        i++;
    }
    f->n_moves++;
    return f;
}

static int is_solved(const struct state_t *e) {
    return e->pieces[1]->x == 1 && e->pieces[1]->y == 3;
}

GHashTable *g_table[N_LAYERS] = {NULL};
static void see(struct state_t *e) {
    if (e == NULL) return;
    g_hash_table_add(g_table[N_LAYERS - 1], e);
}

static int seen(const struct state_t *e) {
    for (int i = 0; i < N_LAYERS; i++) {
        if (g_hash_table_contains(g_table[i], e)) {
            return 1;
        }
    }
    return 0;
}

static void cycle() {
    GHashTable *table = g_table[0];
    g_hash_table_remove_all(table);

    for(int i = 0; i < N_LAYERS - 1; i++) {
        g_table[i] = g_table[i + 1];
    }
    g_table[N_LAYERS - 1] = table;
}
#undef N

static guint state_hash(gconstpointer key) {
    const struct state_t *e = key;

    char *str = flat_state(e, 0);
    //return str[0] | (str[1] << 8) | (str[2] << 16) | (str[3] << 24);
    return str[0] | (str[8] << 8) | (str[12] << 16) | (str[20] << 24);
}

static gboolean state_equals(gconstpointer a_, gconstpointer b_) {
    const struct state_t *a = a_;
    const struct state_t *b = b_;
    return strcmp(flat_state(a, 0), flat_state(b, 0)) == 0;
}

static struct state_t * solve(struct state_t *start) {
    GQueue *queue = g_queue_new();
    g_queue_push_head(queue, start);

    int last_n_moves = 0;
    struct state_t *e;
    while ( (e = g_queue_pop_tail(queue)) != NULL ) {
        if (is_solved(e)) {
            return e;
        }

        if(e->n_moves != last_n_moves) { /* new layer in search */
            cycle();
            last_n_moves = e->n_moves;
        }

        for(int i = 0; i < e->n; i++) {
            struct piece_t *p = e->pieces[i];
#ifdef MULTI_MOVES /* moves of 1 or 2 units */
            for(int dir1 = 0; dir1 < 4; dir1++) {
                for(int dir2 = -1; dir2 < 4; dir2++) {
                    if(dir2 == -1
                       || dir1 == dir2
                       || (dir1 % 2) != (dir2 % 2)) {
                        struct state_t *f = state_move(e, i, dir1, dir2);
                        if(f) {
                            if (seen(f)) {
                                unref_state(f);
                            } else {
                                e->ref++;
                                f->prev = e;
                                see(f);
                                g_queue_push_head(queue, f);
                            }
                        }
                    }
                }
            }
#else /* only moves of 1 unit */
            for(int dir1 = 0; dir1 < 4; dir1++) {
                struct state_t *f = state_move(e, i, dir1, -1);
                if(f) {
                    if (seen(f)) {
                        unref_state(f);
                    } else {
                        e->ref++;
                        f->prev = e;
                        see(f);
                        g_queue_push_head(queue, f);
                    }
                }
            }
#endif
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;

    FILE *f = fopen(argv[1], "r");
    if (f == NULL) return 2;

    for (int i = 0; i < sizeof(g_table)/sizeof(g_table[0]); i++) {
        g_table[i] = g_hash_table_new_full(state_hash, state_equals, unref_state, NULL);
    }


    struct state_t *state = load(f);
    see(state);
    print_state(state);
    printf("level =\n%s\n", flat_state(state, 1));

    struct timeval t1, t2;
    gettimeofday(&t1, NULL);
    struct state_t *s = solve(state);
    gettimeofday(&t2, NULL);
    struct state_t *t = s;
    if (t) {
        int n = 0;
        while (t->prev) { 
            printf("n=%d:\n%s\n", n, flat_state(t, 1));
            t = t->prev;
            n++;
        }
        printf("n=%d solved =\n%s\n", n, flat_state(t, 1));
    }
    //print_state(s);
    printf("n states=%d\n", g_state_id);

    double elapsed = (t2.tv_sec - t1.tv_sec) * 1000.0;
    elapsed += (t2.tv_usec - t1.tv_usec) / 1000.0;
    printf("time: %lf\n", elapsed);


    return 0;
}
