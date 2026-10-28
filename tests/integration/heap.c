#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Test-only entry points: the invariant checker lives in tests/support, and the
   two accounting helpers are part of the runtime but not of <stdlib.h>. */
int __kr16_heap_check(void);
int __kr16_heap_used(void);
int __kr16_heap_peak(void);

int failures;

void expect(int ok) {
    if (ok == 0) {
        failures = failures + 1;
    }
}

void main(void) {
    char *p;
    char *q;
    char *r;
    int i;
    int peak;

    /* 1. a freed block is reused instead of appended */
    p = malloc(32);
    q = malloc(32);
    expect(p != 0);
    expect(q != 0);
    expect(p != q);
    free(p);
    r = malloc(32);
    expect(r == p);
    expect(__kr16_heap_check() == 0);
    free(q);
    free(r);
    expect(__kr16_heap_used() == 0);

    /* 2. adjacent frees coalesce into one block a larger request can use */
    p = malloc(16);
    q = malloc(16);
    free(p);
    free(q);
    r = malloc(28);
    expect(r == p);
    expect(__kr16_heap_check() == 0);
    free(r);

    /* 3. churn must not ratchet the footprint up */
    peak = __kr16_heap_peak();
    i = 0;
    while (i < 200) {
        p = malloc(16);
        q = malloc(24);
        r = malloc(8);
        free(q);
        free(p);
        free(r);
        i = i + 1;
    }
    expect(__kr16_heap_check() == 0);
    expect(__kr16_heap_peak() <= peak + 64);
    expect(__kr16_heap_used() == 0);

    /* 4. a hole between live blocks is reusable and stays consistent */
    p = malloc(16);
    q = malloc(16);
    r = malloc(16);
    free(q);
    expect(__kr16_heap_check() == 0);
    q = malloc(8);
    expect(q != 0);
    expect(__kr16_heap_check() == 0);
    free(p);
    free(q);
    free(r);
    expect(__kr16_heap_used() == 0);

    /* 5. realloc grows, shrinks and keeps the payload */
    p = malloc(8);
    p[0] = 'a';
    r = realloc(p, 64);
    expect(r != 0);
    expect(r[0] == 'a');
    expect(__kr16_heap_check() == 0);
    q = realloc(r, 8);
    expect(q == r);
    expect(q[0] == 'a');
    free(q);
    expect(__kr16_heap_used() == 0);

    /* 6. bad requests fail cleanly */
    expect(malloc(0) == 0);
    expect(malloc(20000) == 0);
    expect(calloc(200, 200) == 0);
    expect(__kr16_heap_check() == 0);

    /* 7. the 16 KiB ceiling is enforced and leaves the heap consistent */
    p = malloc(16000);
    expect(p != 0);
    expect(malloc(1000) == 0);
    expect(__kr16_heap_check() == 0);
    free(p);
    expect(__kr16_heap_used() == 0);

    if (failures != 0) {
        printf("heap FAILED %d\n", failures);
        return;
    }
    printf("heap ok peak=%d\n", __kr16_heap_peak());
}
