#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- Types ---------------- */

typedef struct Node {
    struct Node *prev, *next;
    unsigned pageNumber;
    unsigned refcnt;      // number of references seen
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
    unsigned frames;   // total frames allowed
    unsigned used;     // nodes currently resident
    unsigned K;        // LRU-K threshold
    unsigned faults;   // page faults count
    List cold;         // < K refs
    List hot;          // >= K refs
    Hash *hash;        // page -> node
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
    h->capacity = capacity;
    h->array = (Node**)calloc(capacity, sizeof(Node*));
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
    n->prev = n->next = NULL;
    n->pageNumber = page;
    n->refcnt = 0;
    return n;
}

static Cache* cache_create(unsigned frames, unsigned K, unsigned maxPageIdInclusive) {
    Cache *c = (Cache*)malloc(sizeof(Cache));
    c->frames = frames;
    c->used = 0;
    c->K = (K == 0 ? 1 : K); // guard
    c->faults = 0;
    list_init(&c->cold);
    list_init(&c->hot);
    c->hash = hash_create(maxPageIdInclusive + 1);
    return c;
}

static void cache_destroy(Cache *c) {
    Node *cur;
    cur = c->cold.front;
    while (cur) { Node *nxt = cur->next; free(cur); cur = nxt; }
    cur = c->hot.front;
    while (cur) { Node *nxt = cur->next; free(cur); cur = nxt; }
    free(c->hash->array);
    free(c->hash);
    free(c);
}

static int cache_full(Cache *c) { return c->used >= c->frames; }

/* Evict policy: prefer cold.rear; otherwise hot.rear */
static void cache_evict_one(Cache *c) {
    Node *victim = NULL;
    if (c->cold.size) {
        victim = list_pop_rear(&c->cold);
    } else if (c->hot.size) {
        victim = list_pop_rear(&c->hot);
    }
    if (victim) {
        hash_del(c->hash, victim->pageNumber);
        free(victim);
        c->used--;
    }
}

/* Move node based on refcnt */
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

/* Reference a page */
static void cache_reference(Cache *c, unsigned page) {
    if (page >= c->hash->capacity) {
        fprintf(stderr, "Invalid page %u (capacity %u)\n", page, c->hash->capacity);
        return;
    }

    Node *n = hash_get(c->hash, page);

    if (!n) {
        // MISS
        c->faults++;
        if (cache_full(c)) cache_evict_one(c);

        n = node_new(page);
        n->refcnt = 1;
        hash_put(c->hash, page, n);
        list_push_front(&c->cold, n);
        c->used++;
        if (n->refcnt >= c->K) {
            promote_on_hit(c, n);
        }
        return;
    }

    // HIT
    if (n->refcnt < c->K) n->refcnt++;
    promote_on_hit(c, n);
}

/* Debug print */
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

int main(void) {
    unsigned frames, K, n;

    printf("Enter number of frames: ");
    scanf("%u", &frames);

    printf("Enter K (for LRU-K): ");
    scanf("%u", &K);

    printf("Enter number of page references: ");
    scanf("%u", &n);

    unsigned *seq = (unsigned*)malloc(n * sizeof(unsigned));
    if (!seq) { perror("malloc"); return 1; }

    printf("Enter %u page numbers: ", n);
    for (unsigned i = 0; i < n; i++) {
        scanf("%u", &seq[i]);
    }

    // compute max page id for hash capacity
    unsigned maxPage = 0;
    for (unsigned i = 0; i < n; ++i)
        if (seq[i] > maxPage) maxPage = seq[i];

    Cache *cache = cache_create(frames, K, maxPage);

    for (unsigned i = 0; i < n; ++i) {
        cache_reference(cache, seq[i]);
    }

    printf("\nLRU-%u using hashtable and doubly linked lists\n", K);
    cache_print(cache);

    cache_destroy(cache);
    free(seq);
    return 0;
}
