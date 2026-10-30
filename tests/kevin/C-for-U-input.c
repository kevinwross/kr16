int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}

int main(void) {
    int sum;
    int times = 0;
    int values[5];
    sum = 0;
    for (int i = 0; i < 5; i = i + 1) {
        values[i] = i * i;
        sum = sum + values[i];
    }
    printf("sum=%d fact=%d\n", sum, fact(5));
    do {
        printf("Wow: It's great! #%d\n", times++);
    } while (times < 5);
    return 0;
}
