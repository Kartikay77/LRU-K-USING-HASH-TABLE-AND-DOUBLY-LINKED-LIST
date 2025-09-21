#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define MAXF 32   /* max frames */
#define MAXK 32   /* max K     */

/* print one line (frame 0 .. frame F-1), tabs between columns */
static void print_row(const int *frames, int F) {
    for (int f = 0; f < F; ++f) {
        printf("%d", frames[f]);
        if (f + 1 < F) printf("\t");
    }
    printf("\n");
}

/* On a hit: shift history right up to K-1 and insert new timestamp at [0] */
static void hit_update(int f, int K, int *timeCounter, int refcnt[], int hist[][MAXK]) {
    int limit = (refcnt[f] < (K - 1)) ? refcnt[f] : (K - 1);
    for (int i = limit; i >= 1; --i) hist[f][i] = hist[f][i - 1];
    hist[f][0] = ++(*timeCounter);
    if (refcnt[f] < K) refcnt[f]++;
}

/* On a miss: install page p into frame f and set most recent timestamp */
static void install_page(int f, int p, int K, int *timeCounter,
                         int frames[], int refcnt[], int hist[][MAXK]) {
    frames[f] = p;
    refcnt[f] = 1;
    for (int i = 0; i < K; ++i) hist[f][i] = 0;
    hist[f][0] = ++(*timeCounter);
}

/* Choose LRU-K victim among full frames:
   - Compare K-th most recent timestamp (hist[f][K-1]); if refcnt[f] < K, treat as 0.
   - Smaller K-th ts => older => evict.
   - Tie-breaker: smaller most-recent ts (hist[f][0]). */
static int choose_victim(int F, int K,
                         const int frames[], const int refcnt[], const int hist[][MAXK]) {
    int best = -1;
    long long bestK = LLONG_MAX;
    long long bestMost = LLONG_MAX;

    for (int f = 0; f < F; ++f) {
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
    for (int i = 0; i < P; ++i) {
        if (scanf("%d", &pages[i]) != 1) { free(pages); return 0; }
    }

    /* state arrays (no hash tables) */
    int frames[MAXF], refcnt[MAXF], hist[MAXF][MAXK];
    int faults = 0, t = 0;

    /* init frames and histories */
    for (int f = 0; f < F; ++f) {
        frames[f] = -1;
        refcnt[f] = 0;
        for (int i = 0; i < K; ++i) hist[f][i] = 0;
    }

    /* simulate */
    for (int i = 0; i < P; ++i) {
        int p = pages[i];

        /* linear hit check (no hash) */
        int hit = -1;
        for (int f = 0; f < F; ++f) {
            if (frames[f] == p) { hit = f; break; }
        }

        if (hit != -1) {
            /* HIT */
            hit_update(hit, K, &t, refcnt, hist);
        } else {
            /* MISS */
            faults++;

            /* empty frame? */
            int empty = -1;
            for (int f = 0; f < F; ++f) {
                if (frames[f] == -1) { empty = f; break; }
            }

            if (empty != -1) {
                install_page(empty, p, K, &t, frames, refcnt, hist);
            } else {
                int victim = choose_victim(F, K, frames, refcnt, hist);
                install_page(victim, p, K, &t, frames, refcnt, hist);
            }
        }

        /* step-by-step line (same style as before) */
        print_row(frames, F);
    }

    printf("\nTotal Page Faults = %d\n", faults);

    free(pages);
    return 0;
}
