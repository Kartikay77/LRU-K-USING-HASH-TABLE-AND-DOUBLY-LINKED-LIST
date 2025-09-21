#include <stdio.h>
#include <stdlib.h>

/* ---------------- Types ---------------- */

typedef struct Node {
    struct Node *prev, *next;
    unsigned pageNumber;
    unsigned refcnt;     // references seen (capped at K)
    int slot;            // physical frame index (for printing)
} Node;

typedef struct List {
    Node *front; // MRU
    Node *rear;  // LRU
    unsigned size;
} List;

typedef struct Hash {
    unsigned capacity; // max page id allowed == capacity-1
    Node   **array;    // array[pageNumber] -> Node*
} Hash;

typedef struct Cache {
    unsigned frames;   // total frames
    unsigned used;     // current resident
    unsigned K;        // LRU-K threshold
    unsigned faults;   // page faults
    List cold;         // < K refs
    List hot;          // >= K refs
    Hash *hash;

    /* For per-step output in fixed columns */
    int *frame;        // length = frames, stores page or -1
    unsigned next_free_slot; // next free slot index while filling
} Cache;

/* --------------- List helpers --------------- */
static void list_init(List *l) { l->front = l->rear = NULL; l->size = 0; }

static void list_unlink(List *l, Node *n) {
    if (!n) return;
    if (n->prev) n->prev->next = n->next;
    if (n->next) n->next->prev = n->prev;
    if (l->front == n) l->front = n->next;
    if (l->rear  == n) l->rear  = n->prev;
    n->prev = n->next = NULL;
    if (l->size) l->size--;
}

static void list_push_front(List *l, Node *n) {
    n->prev = NULL;
    n->next = l->front;
    if (l->front) l->front->prev = n;
    l->front = n;
    if (!l->rear) l->rear = n;
    l->size++;
}

static Node* list_pop_rear(List *l) {
    if (!l->rear) return NULL;
    Node *n = l->rear;
    list_unlink(l, n);
    return n;
}

/* --------------- Hash helpers --------------- */
static Hash* hash_create(unsigned capacity) {
    Hash *h = (Hash*)malloc(sizeof(Hash));
    if (!h) { perror("malloc"); exit(1); }
    h->capacity = capacity;
    h->array = (Node**)calloc(capacity, sizeof(Node*));
    if (!h->array) { perror("calloc"); exit(1); }
    return h;
}
static inline Node* hash_get(Hash *h, unsigned page) {
    if (page >= h->capacity) return NULL;
    return h->array[page];
}
static inline void hash_put(Hash *h, unsigned page, Node *n) {
    if (page < h->capacity) h->array[page] = n;
}
static inline void hash_del(Hash *h, unsigned page) {
    if (page < h->capacity) h->array[page] = NULL;
}

/* --------------- Cache helpers --------------- */
static Node* node_new(unsigned page) {
    Node *n = (Node*)malloc(sizeof(Node));
    if (!n) { perror("malloc"); exit(1); }
    n->prev = n->next = NULL;
    n->pageNumber = page;
    n->refcnt = 0;
    n->slot = -1;
    return n;
}

static Cache* cache_create(unsigned frames, unsigned K, unsigned maxPageIdInclusive) {
    Cache *c = (Cache*)malloc(sizeof(Cache));
    if (!c) { perror("malloc"); exit(1); }
    c->frames = frames;
    c->used = 0;
    c->K = (K == 0 ? 1 : K);
    c->faults = 0;
    list_init(&c->cold);
    list_init(&c->hot);
    c->hash = hash_create(maxPageIdInclusive + 1);
    c->frame = (int*)malloc(sizeof(int) * frames);
    if (!c->frame) { perror("malloc"); exit(1); }
    for (unsigned i = 0; i < frames; ++i) c->frame[i] = -1;
    c->next_free_slot = 0;
    return c;
}

static void cache_destroy(Cache *c) {
    Node *cur = c->cold.front;
    while (cur) { Node *nxt = cur->next; free(cur); cur = nxt; }
    cur = c->hot.front;
    while (cur) { Node *nxt = cur->next; free(cur); cur = nxt; }
    free(c->hash->array);
    free(c->hash);
    free(c->frame);
    free(c);
}

static int cache_full(Cache *c) { return c->used >= c->frames; }

/* Evict policy: prefer cold.rear; otherwise hot.rear.
   Returns evicted slot index or -1 if nothing evicted. */
static int cache_evict_one(Cache *c) {
    Node *victim = NULL;
    if (c->cold.size)      victim = list_pop_rear(&c->cold);
    else if (c->hot.size)  victim = list_pop_rear(&c->hot);

    if (!victim) return -1;

    int vslot = victim->slot;
    hash_del(c->hash, victim->pageNumber);
    c->frame[vslot] = -1;        // clear the physical frame
    free(victim);
    // 'used' is adjusted by caller (we typically insert right after)
    return vslot;
}

/* Move node between lists based on refcnt and recency */
static void promote_on_hit(Cache *c, Node *n) {
    if (n->refcnt >= c->K) {
        list_unlink(&c->cold, n);
        list_unlink(&c->hot, n);
        list_push_front(&c->hot, n);
    } else {
        list_unlink(&c->cold, n);
        list_push_front(&c->cold, n);
    }
}

/* Print one line: the frames left->right */
static void print_frames_line(Cache *c) {
    for (unsigned i = 0; i < c->frames; ++i) {
        printf("%d", c->frame[i]);
        if (i + 1 < c->frames) printf("\t");
    }
    printf("\n");
}

/* Reference a page, print state after */
static void cache_reference_and_print(Cache *c, unsigned page) {
    if (page >= c->hash->capacity) {
        fprintf(stderr, "Skip invalid page %u (capacity %u)\n", page, c->hash->capacity);
        print_frames_line(c);
        return;
    }

    Node *n = hash_get(c->hash, page);

    if (!n) {
        // MISS
        c->faults++;

        int slot;
        if (cache_full(c)) {
            slot = cache_evict_one(c);
            // 'used' remains the same (evicted then inserted)
        } else {
            slot = (int)c->next_free_slot++;
            c->used++;
        }

        n = node_new(page);
        n->refcnt = 1;
        n->slot = slot;

        hash_put(c->hash, page, n);
        c->frame[slot] = (int)page;   // place in the physical slot

        list_push_front(&c->cold, n);
        if (n->refcnt >= c->K) promote_on_hit(c, n);

        print_frames_line(c);
        return;
    }

    // HIT
    if (n->refcnt < c->K) n->refcnt++;
    promote_on_hit(c, n);

    // On hit, physical slot doesn't change; just print
    print_frames_line(c);
}

/* Debug print of lists */
static void print_list(const char *name, const List *l) {
    printf("%s (MRU -> LRU)[%u]: ", name, l->size);
    for (const Node *cur = l->front; cur; cur = cur->next) {
        printf("%u(r%u) ", cur->pageNumber, cur->refcnt);
    }
    printf("\n");
}
static void cache_print(Cache *c) {
    print_list("HOT >=K", &c->hot);
    print_list("COLD <K", &c->cold);
    printf("Page Faults: %u\n", c->faults);
}

/* ---------------- Main ---------------- */
int main_2(void) {
    unsigned frames, K, n;

    printf("Enter number of frames: ");
    if (scanf("%u", &frames) != 1) return 0;

    printf("Enter K (for LRU-K): ");
    if (scanf("%u", &K) != 1) return 0;

    printf("Enter number of page references: ");
    if (scanf("%u", &n) != 1) return 0;

    unsigned *seq = (unsigned*)malloc(n * sizeof(unsigned));
    if (!seq) { perror("malloc"); return 1; }

    printf("Enter reference string: ");
    unsigned maxPage = 0;
    for (unsigned i = 0; i < n; i++) {
        scanf("%u", &seq[i]);
        if (seq[i] > maxPage) maxPage = seq[i];
    }

    Cache *cache = cache_create(frames, K, maxPage);

    for (unsigned i = 0; i < n; ++i) {
        cache_reference_and_print(cache, seq[i]);
    }

    /* Final summaries */
    printf("\nTotal Page Faults = %u\n", cache->faults);
    printf("\nLRU-%u using hashtable and doubly linked lists\n", K);
    cache_print(cache);

    cache_destroy(cache);
    free(seq);
    return 0;
}
