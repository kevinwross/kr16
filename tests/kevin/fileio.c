#include <stdio.h>

int main(void) {
    FILE *file;
    char buffer[8];
    int value;

    file = fopen("data.txt", "w");
    if (file == 0) return 1;
    if (fwrite("abc\n", 1, 4, file) != 4) return 2;
    if (fclose(file) != 0) return 3;

    file = fopen("data.txt", "r");
    if (file == 0) return 4;
    if (fseek(file, 1, 0) != 0) return 5;
    value = fgetc(file);
    if (value != 'b') return 6;
    if (ftell(file) != 2) return 7;
    if (fgets(buffer, 4, file) == 0) return 8;
    if (buffer[0] != 'c') return 9;
    if (fclose(file) != 0) return 10;

    file = fopen("copy.txt", "w");
    if (file == 0) return 11;
    if (fputs("ok\n", file) != 0) return 12;
    if (fclose(file) != 0) return 13;

    if (rename("copy.txt", "moved.txt") != 0) return 14;
    if (remove("moved.txt") != 0) return 15;
    if (remove("data.txt") != 0) return 16;
    return 0;
}
