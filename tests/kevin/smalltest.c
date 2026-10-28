

int factorial(int n) {
    if (n == 0) return 1;
    return n * factorial(n - 1);
}

int main() {
    int result = factorial(5);
    printf("%d\n", result);
    char *message = malloc(100);
    if (message == NULL) {
        printf("Memory allocation failed\n");
        return 1;
    }
    *message = '\0';
    strcat(message, "Calculating factorial... ");
    puts(message);

    printf("The factorial of 5 is %d", result);
    
    free(message);

    return 0;
}
