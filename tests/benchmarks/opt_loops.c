/* Benchmarks for the optimization passes added after 1.0:
   induction strength reduction, counted-loop R3 caching, and pointer loops. */
int induction(int n) {
    int i;
    int sum = 0;
    for (i = 0; i < n; i++) {
        sum = sum + i * 3;
    }
    return sum;
}

int pointer_loop(char *p) {
    int n = 0;
    while (*p != 0) {
        n = n + 1;
        p = p + 1;
    }
    return n;
}

int same(char *a, char *b) {
    while (*a != 0 && *b != 0) {
        if (*a != *b) {
            return 0;
        }
        a = a + 1;
        b = b + 1;
    }
    return *a == *b;
}

int main(void) {
    char text[8];
    int i;
    for (i = 0; i < 7; i++) {
        text[i] = 1;
    }
    text[7] = 0;
    return (induction(7) + pointer_loop(text) + same(text, text)) & 255;
}
