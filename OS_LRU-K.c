#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define MAXF 10   /* max frames supported */
#define MAXK 16   /* cap for K */

static void print_row(const int *frames, int F) {
    for (int f = 0; f < F; ++f) {
        printf("%d", frames[f]);
        if (f + 1 < F) printf("\t");
    }
    printf("\n");
}

/* Shift history right (up to K-1) and insert new timestamp at [0] */
static void update_history_on_hit(int fidx, int K, int *t,
                                  int refcnt[], int hist[][MAXK]) {
    int use = refcnt[fidx] < (K - 1) ? refcnt[fidx] : (K - 1);
    for (int i = use; i >= 1; --i) hist[fidx][i] = hist[fidx][i - 1];
    hist[fidx][0] = ++(*t);
    if (refcnt[fidx] < K) refcnt[fidx]++;
}

static void install_new_page(int fidx, int page, int K, int *t,
                             int frames[], int refcnt[], int hist[][MAXK]) {
    frames[fidx] = page;
    refcnt[fidx] = 1;
    for (int i = 0; i < K; ++i) hist[fidx][i] = 0;
    hist[fidx][0] = ++(*t);
}

/* Choose LRU-K victim:
   - Compare the K-th most recent timestamp (hist[f][K-1]); if page has <K refs, treat as 0.
   - Smaller is older → evict.
   - Tie-breaker: older most-recent time (hist[f][0] smaller). */
static int choose_victim(int F, int K,
                         const int frames[], const int refcnt[], const int hist[][MAXK]) {
    int best = -1;
    long long bestK = LLONG_MAX;
    long long bestMost = LLONG_MAX;

    for (int f = 0; f < F; ++f) {
        if (frames[f] == -1) continue; /* should not happen if called only when full */
        long long kth = (refcnt[f] >= K) ? hist[f][K - 1] : 0LL;
        long long most = (refcnt[f] > 0) ? hist[f][0] : 0LL;

        if (kth < bestK || (kth == bestK && most < bestMost)) {
            bestK = kth;
            bestMost = most;
            best = f;
        }
    }
    return best;
}

/* Simple selection-order print for summary (MRU -> LRU by hist[][0]) */
static void print_hot_cold_summary(int F, int K,
                                   const int frames[], const int refcnt[], const int hist[][MAXK],
                                   int totalFaults) {
    /* Build an order array 0..F-1 and select by decreasing hist[][0] */
    int order[MAXF];
    for (int i = 0; i < F; ++i) order[i] = i;

    /* selection sort by descending most-recent timestamp */
    for (int i = 0; i < F; ++i) {
        int best = i;
        for (int j = i + 1; j < F; ++j) {
            if (hist[order[j]][0] > hist[order[best]][0])
                best = j;
        }
        int tmp = order[i]; order[i] = order[best]; order[best] = tmp;
    }

    printf("\nLRU-%d using hashtable and doubly linked lists\n", K);
    /* HOT list */
    int hotCount = 0;
    for (int idx = 0; idx < F; ++idx)
        if (frames[order[idx]] != -1 && refcnt[order[idx]] >= K) hotCount++;
    printf("HOT >=K (MRU -> LRU)[%d]: ", hotCount);
    for (int idx = 0, printed = 0; idx < F; ++idx) {
        int f = order[idx];
        if (frames[f] != -1 && refcnt[f] >= K) {
            printf("%d(r%d) ", frames[f], refcnt[f]);
            printed++;
        }
    }
    printf("\n");

    /* COLD list */
    int coldCount = 0;
    for (int idx = 0; idx < F; ++idx)
        if (frames[order[idx]] != -1 && refcnt[order[idx]] < K) coldCount++;
    printf("COLD <K (MRU -> LRU)[%d]: ", coldCount);
    for (int idx = 0, printed = 0; idx < F; ++idx) {
        int f = order[idx];
        if (frames[f] != -1 && refcnt[f] < K) {
            printf("%d(r%d) ", frames[f], refcnt[f]);
            printed++;
        }
    }
    printf("\n");
    printf("Page Faults: %d\n", totalFaults);
}

int main(void) {
    int F, K, P;

    printf("Enter number of frames: ");
    if (scanf("%d", &F) != 1 || F <= 0 || F > MAXF) return 0;

    printf("Enter K (for LRU-K): ");
    if (scanf("%d", &K) != 1 || K <= 0) K = 1;
    if (K > MAXK) K = MAXK;

    printf("Enter number of pages: ");
    if (scanf("%d", &P) != 1 || P <= 0) return 0;

    int *pages = (int*)malloc(sizeof(int) * P);
    if (!pages) { perror("malloc"); return 1; }

    printf("Enter reference string: ");
    for (int i = 0; i < P; ++i) if (scanf("%d", &pages[i]) != 1) { free(pages); return 0; }

    /* state */
    int frames[MAXF], refcnt[MAXF], hist[MAXF][MAXK];
    int faults = 0, t = 0;

    for (int f = 0; f < F; ++f) {
        frames[f] = -1;
        refcnt[f] = 0;
        for (int i = 0; i < K; ++i) hist[f][i] = 0;
    }

    for (int i = 0; i < P; ++i) {
        int p = pages[i];

        /* hit? */
        int hitIdx = -1;
        for (int f = 0; f < F; ++f) {
            if (frames[f] == p) { hitIdx = f; break; }
        }

        if (hitIdx != -1) {
            /* HIT */
            update_history_on_hit(hitIdx, K, &t, refcnt, hist);
        } else {
            /* MISS */
            faults++;

            /* empty slot? */
            int emptyIdx = -1;
            for (int f = 0; f < F; ++f) if (frames[f] == -1) { emptyIdx = f; break; }

            if (emptyIdx != -1) {
                install_new_page(emptyIdx, p, K, &t, frames, refcnt, hist);
            } else {
                /* evict LRU-K victim */
                int victim = choose_victim(F, K, frames, refcnt, hist);
                install_new_page(victim, p, K, &t, frames, refcnt, hist);
            }
        }

        /* print frame row after this reference */
        print_row(frames, F);
    }

    printf("\nTotal Page Faults = %d\n", faults);

    /* Optional summary like your earlier output */
    print_hot_cold_summary(F, K, frames, refcnt, hist, faults);

    free(pages);
    return 0;
}
