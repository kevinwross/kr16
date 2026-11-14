/* Loop-heavy compiler benchmark. The result is intentionally small enough to
   report through the VM exit status (R0 & 0xFF). */
int work(int n) {
    int i;
    int j;
    int sum = 0;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            sum = sum + i * j + i + j;
        }
    }
    return sum;
}

int main(void) {
    return work(7) & 255;
}
