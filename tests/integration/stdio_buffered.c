#include <stdio.h>
#include <string.h>

/* The kr16 profile of <stdio.h> stops at printf; fileio.c also defines a
 * formatted stream writer, so declare it here rather than call it implicitly. */
int fprintf(FILE *stream, const char *format, ...);

int main(void) {
    FILE *file;
    char line[32];

    file = fopen("stdio.tmp", "w");
    if (file == 0) return 1;
    if (fwrite("abc", 1, 3, file) != 3) return 2;
    if (ftell(file) != 3) return 3;
    if (fprintf(file, " %d %x %s %c\n", 42, 255, "hi", '!') <= 0) return 4;
    if (fclose(file) != 0) return 5;

    file = fopen("stdio.tmp", "r");
    if (file == 0) return 6;
    if (fgets(line, sizeof line, file) == 0) return 7;
    if (strcmp(line, "abc 42 ff hi !\n") != 0) return 8;
    if (fgetc(file) != EOF) return 9;
    if (!feof(file)) return 10;
    if (ungetc('Z', file) != 'Z') return 11;
    if (feof(file)) return 12;
    if (fgetc(file) != 'Z') return 13;
    if (fclose(file) != 0) return 14;
    if (remove("stdio.tmp") != 0) return 15;

    /* `stdout` is a built-in identifier for the built-in compiler and a macro
     * over a krc-runtime function in krc's sysroot, so print the marker with
     * the routine both toolchains resolve from the shipped runtime. */
    printf("stdio ok\n");
    return 0;
}
